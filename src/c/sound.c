// SPDX-FileCopyrightText: 2026 Ben Combee
// SPDX-License-Identifier: MIT

#include "sound.h"

#define PERSIST_KEY_SOUND 1

#define SR 8000            // sample rate of the mix, Hz
#define MIX_VOLUME 70      // stream volume, 0-100
#define MS_TO_SAMPLES(ms) (SR / 1000 * (ms))

// Effect lengths, in ms. Each buffer holds exactly its effect: the mixer pads
// with silence, so no buffer needs a tail of its own.
// Three lengths of splash, picked by how fast the wave throws. A catch should
// finish before the next one lands, and stepping straight from the long one to
// the short one was an audible jump.
#define LEN_CATCH 200
#define LEN_CATCH_MID 155
#define LEN_CATCH_QUICK 110
#define MID_CATCH_GAP_MS 560
#define QUICK_CATCH_GAP_MS 360
#define LEN_MISS 340
#define LEN_POP 150
#define LEN_OVER 1180
#define LEN_BEEP 90

// Stream pacing. Everything queued -- our pending bytes plus the lead we keep
// ahead of real time -- is latency between a catch and its splash, so both are
// kept short. Underruns land in the idle dither, where they cost nothing.
#define PRIME_SAMPLES MS_TO_SAMPLES(40)
#define MAX_CHUNK MS_TO_SAMPLES(100)
#define PEND_CAP MS_TO_SAMPLES(96)
#define MAX_STALLED_PUMPS 30

// While nothing is playing we write nothing at all: the stream stays open but
// the firmware's queue runs dry, so no backlog can build and there is nothing
// to hear. A short tail of true silence follows each effect so its own last
// samples are never the ones the queue runs out on.
#define TAIL_SAMPLES MS_TO_SAMPLES(60)

// Flip to 1 for a pacing summary once a second (pumps, samples generated and
// written, short writes, queue lead). This is what showed that the firmware
// never pushes back and that the lag was its own backlog.
#define AUDIO_DEBUG_LOG 0

static bool s_sound_on = true;
// The watch can mute the speaker system-wide or for Quiet Time. Apps cannot
// override it, so we hold no stream open and report the setting as blocked
// rather than pretending it can be changed. Re-checked once a second.
static bool s_blocked;
static uint32_t s_mute_check_ms;
static bool s_streaming;
static int s_stalled_pumps;

static int8_t s_catch[MS_TO_SAMPLES(LEN_CATCH)];
static int8_t s_catch_mid[MS_TO_SAMPLES(LEN_CATCH_MID)];
static int8_t s_catch_quick[MS_TO_SAMPLES(LEN_CATCH_QUICK)];
static int8_t s_miss[MS_TO_SAMPLES(LEN_MISS)];
static int8_t s_pop[MS_TO_SAMPLES(LEN_POP)];
static int8_t s_over[MS_TO_SAMPLES(LEN_OVER)];
static int8_t s_beep[MS_TO_SAMPLES(LEN_BEEP)];

// Which splash the current wave uses; set by sound_set_tempo().
static const int8_t *s_catch_pcm = s_catch;
static int s_catch_len = MS_TO_SAMPLES(LEN_CATCH);


// --- Voices ----------------------------------------------------------------

#define MAX_VOICES 4

typedef struct {
  const int8_t *pcm;
  int len;
  int pos;
} Voice;

static Voice s_voices[MAX_VOICES];

// --- Rendering helpers -----------------------------------------------------

static const int8_t s_sine16[16] = {
  0, 49, 90, 118, 127, 118, 90, 49, 0, -49, -90, -118, -127, -118, -90, -49,
};

// Pitch of a MIDI note, in millihertz for the lowest octave; shifted up from
// there. Note 0 is C-1 at 8.176Hz.
static const uint16_t s_note_mhz[12] = {
  8176, 8662, 9177, 9723, 10301, 10913, 11562, 12250, 12978, 13750, 14568, 15434,
};

static int32_t note_hz(int midi) {
  return ((int32_t)s_note_mhz[midi % 12] << (midi / 12)) / 1000;
}

static int8_t clamp_pcm(int32_t v) {
  if (v > 127) return 127;
  if (v < -127) return -127;
  return (int8_t)v;
}

typedef enum { TONE_SINE, TONE_TRI } ToneShape;

// Add one note to a buffer. Each note fades in and out, so a sequence has no
// steps at its joins and the buffer ends at exactly zero.
static void render_note(int8_t *buf, int len, int start, int dur, int midi,
                        ToneShape shape, int amp) {
  const int32_t inc = (note_hz(midi) * 65536) / SR;
  const int attack = MS_TO_SAMPLES(4);
  const int release = MS_TO_SAMPLES(20);
  uint32_t phase = 0;

  for (int i = 0; i < dur; i++) {
    const int at = start + i;
    if (at >= len) break;

    int32_t wave;
    if (shape == TONE_SINE) {
      wave = s_sine16[(phase >> 12) & 0xf];
    } else {
      // Triangle straight from the phase ramp.
      const int32_t up = (int32_t)((phase >> 8) & 0xff);  // 0..255
      wave = (up < 128) ? (up * 2 - 128) : (383 - up * 2);
    }
    phase += inc;

    int32_t env = 255;
    if (i < attack) env = 255 * i / attack;
    else if (i > dur - release) env = 255 * (dur - i) / release;

    buf[at] = clamp_pcm(buf[at] + (wave * amp / 127) * env / 255);
  }
}

// Add a burst of filtered noise: the cutoff and the level both fall away, which
// is what makes it read as water or as a blast rather than as hiss.
static void render_noise(int8_t *buf, int len, int start, int dur, uint32_t seed,
                         int alpha_start, int alpha_end, int amp) {
  const int attack = MS_TO_SAMPLES(3);
  const int fade = MS_TO_SAMPLES(15);
  uint32_t rng = seed;
  int32_t lp = 0;

  for (int i = 0; i < dur; i++) {
    const int at = start + i;
    if (at >= len) break;

    rng = rng * 1664525u + 1013904223u;
    const int32_t noise = (int32_t)((rng >> 16) & 0xff) - 128;
    const int32_t alpha = alpha_start - ((alpha_start - alpha_end) * i) / dur;
    lp += ((noise - lp) * alpha) / 256;

    int32_t env;
    if (i < attack) {
      env = 255 * i / attack;
    } else {
      const int32_t t = ((i - attack) * 255) / (dur - attack);
      env = 255 - t;
      env = (env * env) / 255;  // steep tail
    }
    if (i > dur - fade) env = env * (dur - i) / fade;

    buf[at] = clamp_pcm(buf[at] + (lp * amp / 127) * env / 255);
  }
}

// A tone whose pitch slides from one note to another, for droplets and thuds.
static void render_sweep(int8_t *buf, int len, int start, int dur, int hz_from,
                         int hz_to, int amp) {
  uint32_t phase = 0;
  for (int i = 0; i < dur; i++) {
    const int at = start + i;
    if (at >= len) break;

    const int32_t hz = hz_from - ((hz_from - hz_to) * i) / dur;
    phase += (uint32_t)((hz * 65536) / SR);
    const int32_t env = 255 - (255 * i) / dur;
    const int32_t wave = s_sine16[(phase >> 12) & 0xf];
    buf[at] = clamp_pcm(buf[at] + (wave * amp / 127) * env / 255);
  }
}

static void build_effects(void) {
  memset(s_catch, 0, sizeof(s_catch));
  memset(s_catch_mid, 0, sizeof(s_catch_mid));
  memset(s_catch_quick, 0, sizeof(s_catch_quick));
  memset(s_miss, 0, sizeof(s_miss));
  memset(s_pop, 0, sizeof(s_pop));
  memset(s_over, 0, sizeof(s_over));
  memset(s_beep, 0, sizeof(s_beep));

  // Catch: spray with a droplet falling through it.
  const int catch_len = ARRAY_LENGTH(s_catch);
  render_noise(s_catch, catch_len, 0, catch_len, 0x13579bdfu, 150, 30, 100);
  render_sweep(s_catch, catch_len, 0, MS_TO_SAMPLES(110), 480, 150, 55);

  // The same splash, cut down in two steps for waves that arrive faster than
  // it decays. Each keeps the droplet roughly half its length.
  const int mid_len = ARRAY_LENGTH(s_catch_mid);
  render_noise(s_catch_mid, mid_len, 0, mid_len, 0x13579bdfu, 150, 38, 100);
  render_sweep(s_catch_mid, mid_len, 0, MS_TO_SAMPLES(85), 500, 170, 55);

  const int quick_len = ARRAY_LENGTH(s_catch_quick);
  render_noise(s_catch_quick, quick_len, 0, quick_len, 0x13579bdfu, 150, 45, 100);
  render_sweep(s_catch_quick, quick_len, 0, MS_TO_SAMPLES(60), 520, 200, 55);

  // Miss: a dull ground blast, all low end.
  const int miss_len = ARRAY_LENGTH(s_miss);
  render_noise(s_miss, miss_len, 0, miss_len, 0x2468ace0u, 70, 12, 105);
  render_sweep(s_miss, miss_len, 0, MS_TO_SAMPLES(260), 150, 45, 70);

  // Pop: one link of the chain, the same blast in miniature.
  const int pop_len = ARRAY_LENGTH(s_pop);
  render_noise(s_pop, pop_len, 0, pop_len, 0x0f1e2d3cu, 90, 20, 95);
  render_sweep(s_pop, pop_len, 0, MS_TO_SAMPLES(110), 220, 70, 60);

  // End of the game: the run down, a beat of quiet, then the fanfare. Waves
  // that are cleared along the way say nothing at all.
  const int over_len = ARRAY_LENGTH(s_over);
  const int fall = MS_TO_SAMPLES(150);
  render_note(s_over, over_len, 0 * fall, fall, 67, TONE_TRI, 85);
  render_note(s_over, over_len, 1 * fall, fall, 64, TONE_TRI, 85);
  render_note(s_over, over_len, 2 * fall, fall, 60, TONE_TRI, 85);
  render_note(s_over, over_len, 3 * fall, MS_TO_SAMPLES(240), 55, TONE_TRI, 85);

  const int rise = MS_TO_SAMPLES(720);  // fanfare starts here
  const int step = MS_TO_SAMPLES(100);
  render_note(s_over, over_len, rise + 0 * step, step, 72, TONE_SINE, 90);
  render_note(s_over, over_len, rise + 1 * step, step, 76, TONE_SINE, 90);
  render_note(s_over, over_len, rise + 2 * step, step, 79, TONE_SINE, 90);
  render_note(s_over, over_len, rise + 3 * step, MS_TO_SAMPLES(140), 84, TONE_SINE, 90);

  render_note(s_beep, ARRAY_LENGTH(s_beep), 0, ARRAY_LENGTH(s_beep), 81,
              TONE_SINE, 80);
}

// --- Stream ----------------------------------------------------------------

static int8_t s_pend[PEND_CAP];
static int s_pend_len;
static uint32_t s_last_pump_ms;
static int32_t s_frac_ms;     // sub-sample remainder of elapsed time
static int32_t s_bonus;       // one-shot cushion, granted when an effect starts
static int32_t s_lead;        // samples written beyond real time (our latency)
static int32_t s_tail;        // silence still owed after the last voice ended

static uint32_t now_ms(void) {
  time_t s;
  uint16_t ms;
  time_ms(&s, &ms);
  return (uint32_t)s * 1000 + ms;
}

// Idle frames carry a 1-LSB dither rather than digital silence. The amplifier
// powers down when it sees true silence, and waking it swallows the start of
// the next effect -- which is why the first catch of a wave went unheard.
static uint32_t s_dither_rng = 0x02f6e2b1u;

static bool voices_active(void) {
  for (int i = 0; i < MAX_VOICES; i++) {
    if (s_voices[i].pos < s_voices[i].len) return true;
  }
  return false;
}

static void mix_into(int8_t *out, int n) {
  for (int i = 0; i < n; i++) {
    s_dither_rng = s_dither_rng * 1664525u + 1013904223u;
    out[i] = ((s_dither_rng >> 20) & 1) ? 1 : -1;
  }
  for (int v = 0; v < MAX_VOICES; v++) {
    Voice *voice = &s_voices[v];
    if (voice->pos >= voice->len) continue;

    int avail = voice->len - voice->pos;
    if (avail > n) avail = n;
    for (int i = 0; i < avail; i++) {
      out[i] = clamp_pcm((int32_t)out[i] + voice->pcm[voice->pos + i]);
    }
    voice->pos += avail;
  }
}

static void stream_open(void) {
  if (s_streaming || !s_sound_on || s_blocked) return;
  if (!speaker_stream_open(SpeakerPcmFormat_8kHz_8bit, MIX_VOLUME)) {
    APP_LOG(APP_LOG_LEVEL_WARNING, "speaker stream unavailable; using notes");
    return;
  }
  s_streaming = true;
  s_stalled_pumps = 0;
  s_pend_len = 0;
  s_frac_ms = 0;
  s_lead = 0;
  s_bonus = PRIME_SAMPLES;  // a cushion to start with
  s_last_pump_ms = now_ms();
}

static void stream_close(void) {
  if (!s_streaming) return;
  s_streaming = false;
  s_pend_len = 0;
  speaker_stream_close();
}

#if AUDIO_DEBUG_LOG
static uint32_t s_dbg_next_ms;
static uint32_t s_dbg_pumps;
static uint32_t s_dbg_written;
static uint32_t s_dbg_gen;
static int s_dbg_max_pend;
static int s_dbg_short_writes;
#endif

void sound_pump(void) {
  // Poll the watch's mute: it can change under us at any time, including when
  // Quiet Time starts mid-game.
  {
    const uint32_t tick = now_ms();
    if (tick >= s_mute_check_ms) {
      s_mute_check_ms = tick + 1000;
      const bool blocked = speaker_is_muted();
      if (blocked != s_blocked) {
        s_blocked = blocked;
        if (blocked) {
          stream_close();
        } else {
          stream_open();
        }
      }
    }
  }

  if (!s_streaming) return;

  // Nothing playing means nothing written. The queue drains to empty, so the
  // next effect is heard as soon as it is mixed, and the speaker carries no
  // filler for the ear to find.
  const uint32_t now = now_ms();
  const bool busy = voices_active();
  const bool quiet = (!busy && s_tail <= 0 && s_pend_len == 0);

  if (quiet) {
    s_last_pump_ms = now;
    s_frac_ms = 0;
    s_lead = 0;
  } else {
    // Pacing is incremental: each pump generates exactly the audio real time
    // has consumed since the last one, so no drift can accumulate.
    uint32_t dt_ms = now - s_last_pump_ms;
    if (dt_ms > 500) dt_ms = 500;  // after a long stall, resume rather than burst
    s_last_pump_ms = now;

    s_frac_ms += (int32_t)(dt_ms * SR);
    const int32_t real = s_frac_ms / 1000;
    s_frac_ms -= real * 1000;

    int32_t due = real + s_bonus;
    s_bonus = 0;

    if (busy) {
      s_tail = TAIL_SAMPLES;  // the tail is owed from the end of the last voice
    } else {
      if (due > s_tail) due = s_tail;
      s_tail -= due;
    }

    if (due > MAX_CHUNK) due = MAX_CHUNK;
    if (due > PEND_CAP - s_pend_len) due = PEND_CAP - s_pend_len;

    if (due > 0) {
      mix_into(&s_pend[s_pend_len], due);
      s_pend_len += due;
    }
    // The firmware's queue cannot go negative: once it is starved, further
    // deficit buys nothing, so the estimate floors at zero.
    s_lead += due - real;
    if (s_lead < 0) s_lead = 0;

    if (s_pend_len > 0) {
      const int asked = s_pend_len;
      const uint32_t written = speaker_stream_write(s_pend, s_pend_len);
#if AUDIO_DEBUG_LOG
      s_dbg_written += written;
      if ((int)written < asked) s_dbg_short_writes++;
#else
      (void)asked;
#endif
      if (written > 0) {
        s_pend_len -= written;
        if (s_pend_len > 0) memmove(s_pend, s_pend + written, s_pend_len);
        s_stalled_pumps = 0;
      } else if (s_pend_len >= PEND_CAP && ++s_stalled_pumps > MAX_STALLED_PUMPS) {
        // The stream is not draining; give up on it and let the fallback run.
        APP_LOG(APP_LOG_LEVEL_WARNING, "speaker stream stalled; using notes");
        stream_close();
      }
    }

#if AUDIO_DEBUG_LOG
    s_dbg_gen += (due > 0) ? due : 0;
    if (s_pend_len > s_dbg_max_pend) s_dbg_max_pend = s_pend_len;
#endif
  }

#if AUDIO_DEBUG_LOG
  s_dbg_pumps++;
  {
    if (now >= s_dbg_next_ms) {
      // lead_ms is how far ahead of real time we have written: our own share of
      // the latency. Anything beyond it is inside the firmware.
      const int lead_ms = (int)(s_lead * 1000 / SR);
      APP_LOG(APP_LOG_LEVEL_DEBUG,
              "audio: pumps=%d gen=%d wrote=%d short=%d pend=%d maxpend=%d "
              "lead=%dms status=%d busy=%d",
              (int)s_dbg_pumps, (int)s_dbg_gen, (int)s_dbg_written,
              s_dbg_short_writes, s_pend_len, s_dbg_max_pend, lead_ms,
              (int)speaker_get_status(), (int)busy);
      s_dbg_next_ms = now + 1000;
      s_dbg_pumps = 0;
      s_dbg_written = 0;
      s_dbg_gen = 0;
      s_dbg_max_pend = 0;
      s_dbg_short_writes = 0;
    }
  }
#endif
}

// --- Fallback --------------------------------------------------------------

// Used only if the PCM stream is unavailable. These pop on the tail, which is
// the whole reason the stream exists.
static void play_fallback(Sfx sfx) {
  switch (sfx) {
    case SFX_CATCH:
      speaker_play_tone(600, 90, MIX_VOLUME, SpeakerWaveformTriangle);
      break;
    case SFX_MISS:
      speaker_play_tone(90, 260, MIX_VOLUME, SpeakerWaveformSawtooth);
      break;
    case SFX_POP:
      speaker_play_tone(120, 110, MIX_VOLUME, SpeakerWaveformSawtooth);
      break;
    case SFX_GAME_OVER:
      speaker_play_tone(196, 500, MIX_VOLUME, SpeakerWaveformTriangle);
      break;
    case SFX_BEEP:
      speaker_play_tone(880, 90, MIX_VOLUME, SpeakerWaveformSine);
      break;
  }
}

// --- API -------------------------------------------------------------------

void sound_init(void) {
  build_effects();
  s_blocked = speaker_is_muted();
  if (persist_exists(PERSIST_KEY_SOUND)) {
    s_sound_on = persist_read_bool(PERSIST_KEY_SOUND);
  }
}

void sound_deinit(void) {
  persist_write_bool(PERSIST_KEY_SOUND, s_sound_on);
  stream_close();
  speaker_stop();
}

void sound_resume(void) {
  stream_open();
}

void sound_suspend(void) {
  stream_close();
}

void sound_set_tempo(int drop_interval_ms) {
  if (drop_interval_ms <= QUICK_CATCH_GAP_MS) {
    s_catch_pcm = s_catch_quick;
    s_catch_len = ARRAY_LENGTH(s_catch_quick);
  } else if (drop_interval_ms <= MID_CATCH_GAP_MS) {
    s_catch_pcm = s_catch_mid;
    s_catch_len = ARRAY_LENGTH(s_catch_mid);
  } else {
    s_catch_pcm = s_catch;
    s_catch_len = ARRAY_LENGTH(s_catch);
  }
}

bool sound_is_on(void) {
  return s_sound_on;
}

bool sound_blocked_by_watch(void) {
  return s_blocked;
}

bool sound_is_audible(void) {
  return s_sound_on && !s_blocked;
}

void sound_toggle(void) {
  if (s_blocked) return;  // not ours to change
  s_sound_on = !s_sound_on;
  persist_write_bool(PERSIST_KEY_SOUND, s_sound_on);
  if (s_sound_on) {
    stream_open();
    sound_play(SFX_BEEP);
  } else {
    stream_close();
    speaker_stop();
  }
}

void sound_play(Sfx sfx) {
  if (!s_sound_on || s_blocked) return;

  const int8_t *pcm = NULL;
  int len = 0;
  switch (sfx) {
    case SFX_CATCH:      pcm = s_catch_pcm; len = s_catch_len; break;
    case SFX_MISS:       pcm = s_miss;  len = ARRAY_LENGTH(s_miss);  break;
    case SFX_POP:        pcm = s_pop;   len = ARRAY_LENGTH(s_pop);   break;
    case SFX_GAME_OVER:  pcm = s_over;  len = ARRAY_LENGTH(s_over);  break;
    case SFX_BEEP:       pcm = s_beep;  len = ARRAY_LENGTH(s_beep);  break;
  }
  if (!pcm) return;

  if (!s_streaming) {
    play_fallback(sfx);
    return;
  }

  // Starting from silence, re-anchor the clock (idle pumps generated nothing)
  // and give the stream a small cushion, so the effect itself is never the
  // thing that runs the queue dry.
  if (!voices_active()) {
    s_last_pump_ms = now_ms();
    s_frac_ms = 0;
    s_bonus += PRIME_SAMPLES;
  }

  // A free voice if there is one, otherwise the one furthest along.
  Voice *slot = &s_voices[0];
  for (int i = 0; i < MAX_VOICES; i++) {
    if (s_voices[i].pos >= s_voices[i].len) {
      slot = &s_voices[i];
      break;
    }
    if (s_voices[i].len - s_voices[i].pos < slot->len - slot->pos) {
      slot = &s_voices[i];
    }
  }
  slot->pcm = pcm;
  slot->len = len;
  slot->pos = 0;
}
