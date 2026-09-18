#!/usr/bin/env python3
# Copyright (c) 2026 Ben Combee
# SPDX-License-Identifier: MIT
"""Record the game playing itself: an animated GIF, and stills along the way.

Builds the demo (`KABLOOEY_DEMO=1`, see `wscript`), which taps through the
banners and chases the bombs on its own, then pulls frames straight off the
QEMU monitor with `screendump`. `pebble screenshot --gif` exists but records a
fixed window on its own schedule, which is no use when you want a particular
stretch of a wave.

Frames are captured as fast as the monitor allows and stamped with the time
they arrived, then resampled onto an even timeline, so a slow machine changes
the smoothness of the result but never its speed.

Run from the project directory:

    tools/capture.py                        # 12s GIF into screenshots/
    tools/capture.py --seconds 20 --fps 25
    tools/capture.py --stills 3,8,14        # PNGs at those times as well
    tools/capture.py --no-build             # use the pbw already built
"""

import os
import sys

# pebble-tool lives in its own virtualenv (Pillow, libpebble2), so re-exec under
# its interpreter rather than asking anyone to hunt for the path.
try:
    import pebble_tool  # noqa: F401
except ImportError:
    import shutil as _shutil

    _launcher = _shutil.which("pebble")
    if _launcher is None:
        sys.exit("pebble-tool is not on PATH; cannot find an interpreter with pebble_tool.")
    with open(_launcher) as _f:
        _shebang = _f.readline().strip()
    if not _shebang.startswith("#!"):
        sys.exit("Cannot determine pebble-tool's interpreter from {}.".format(_launcher))
    _interpreter = _shebang[2:].strip()
    if os.path.realpath(_interpreter) == os.path.realpath(sys.executable):
        sys.exit("pebble_tool is not importable under {}.".format(_interpreter))
    os.execv(_interpreter, [_interpreter, os.path.abspath(__file__)] + sys.argv[1:])

import argparse
import shutil
import subprocess
import tempfile
import time

from PIL import Image

from pebble_tool.commands.install import ToolAppInstaller
from pebble_tool.commands.screenshot import ScreenshotCommand
from pebble_tool.exceptions import ToolError
from pebble_tool.sdk import get_sdk_persist_dir, sdk_manager, sdk_version
from pebble_tool.util import get_persist_dir
from pebble_tool.util.wsl import maybe_apply_wsl_hacks

PLATFORM = "emery"
DISPLAY = (200, 228)     # the panel inside the screendump's black border
SETTLE_SECONDS = 4.0     # after install: let the app start and the demo tap in


def setup_environment():
    """What pebble_tool.run_tool does before dispatching, so QEMU is on PATH."""
    maybe_apply_wsl_hacks()
    if sdk_version() is not None:
        os.environ["PATH"] = "{}:{}".format(
            os.path.join(get_persist_dir(), "SDKs", sdk_version(), "toolchain", "bin"),
            os.environ["PATH"])
    extra = os.environ.get("PEBBLE_EXTRA_PATH")
    if extra:
        os.environ["PATH"] = "{}:{}".format(extra, os.environ["PATH"])


class DemoCapture(ScreenshotCommand):
    """Borrows pebble-tool's emulator plumbing; supplies its own capture loop."""

    def __init__(self, args):
        super(DemoCapture, self).__init__()
        self.args = args
        self._verbosity = 0  # normally set by BaseCommand.__call__, which we bypass

    def run(self, pbw_path):
        persist_dir = get_sdk_persist_dir(PLATFORM, sdk_manager.get_current_sdk())
        if os.path.exists(persist_dir):
            shutil.rmtree(persist_dir)  # a fresh watch: no stale high score or settings

        pebble = None
        try:
            pebble = self._connect_emulator(PLATFORM, None)
            time.sleep(5)  # pypkjs accepts the connection before it can relay a bundle
            ToolAppInstaller(pebble, pbw_path, quiet=True).install()

            monitor_port = getattr(pebble.transport, "qemu_monitor_port", None)
            if not monitor_port:
                raise ToolError("QEMU monitor port not available; cannot capture frames.")

            print("  letting the demo get going ({:.0f}s)".format(SETTLE_SECONDS))
            time.sleep(SETTLE_SECONDS)
            return self._record(monitor_port)
        finally:
            self._close_pebble_connection(pebble)
            self._shutdown_platform_emulator(PLATFORM, None)

    def _record(self, monitor_port):
        """Every frame the monitor will give us, each with its arrival time."""
        interval = 1.0 / self.args.fps
        frames = []
        temp_dir = tempfile.mkdtemp(prefix="kablooey-capture-")
        try:
            start = time.time()
            deadline = start + self.args.seconds
            index = 0
            while time.time() < deadline:
                due = start + index * interval
                nap = due - time.time()
                if nap > 0:
                    time.sleep(nap)
                path = os.path.join(temp_dir, "frame_{:05d}.ppm".format(index))
                self._qemu_monitor_command(monitor_port, "screendump {}".format(path))
                frame = _read_ppm(path)
                index += 1
                if frame is None:
                    continue
                frames.append((time.time() - start, _crop(frame)))
                os.unlink(path)
            print("  captured {} frames over {:.1f}s ({:.1f}/s)".format(
                len(frames), self.args.seconds, len(frames) / self.args.seconds))
        finally:
            shutil.rmtree(temp_dir, ignore_errors=True)
        if not frames:
            raise ToolError("No frames captured.")
        return frames


def _read_ppm(path, timeout=1.0):
    """Decode a screendump, waiting for QEMU to finish writing it."""
    deadline = time.time() + timeout
    while time.time() < deadline:
        if os.path.exists(path) and os.path.getsize(path) > 0:
            try:
                with Image.open(path) as img:
                    return img.convert("RGB")
            except OSError:
                pass  # still being written
        time.sleep(0.005)
    return None


def _crop(frame):
    """Trim the black border the emulator's screendump puts around the panel."""
    width, height = DISPLAY
    have_width, have_height = frame.size
    if have_width < width or have_height < height:
        raise ToolError("Screendump is {}x{}, smaller than the {}x{} display.".format(
            have_width, have_height, width, height))
    left = (have_width - width) // 2
    top = (have_height - height) // 2
    return frame.crop((left, top, left + width, top + height))


def _resample(frames, fps, seconds):
    """Put the captured frames on an even timeline, nearest frame wins.

    Capture rate wobbles with machine load; playback should not.
    """
    out = []
    count = int(round(fps * seconds))
    position = 0
    for i in range(count):
        want = i / float(fps)
        while position + 1 < len(frames) and frames[position + 1][0] <= want:
            position += 1
        nearer = position
        if position + 1 < len(frames):
            if abs(frames[position + 1][0] - want) < abs(frames[position][0] - want):
                nearer = position + 1
        out.append(frames[nearer][1])
    return out


def _write_gif(images, path, fps):
    os.makedirs(os.path.dirname(path) or ".", exist_ok=True)
    temp_dir = tempfile.mkdtemp(prefix="kablooey-encode-")
    try:
        for i, image in enumerate(images):
            image.save(os.path.join(temp_dir, "frame_{:05d}.png".format(i)))
        pattern = os.path.join(temp_dir, "frame_%05d.png")
        palette = os.path.join(temp_dir, "palette.png")
        rate = "{:.4f}".format(fps)
        # A generated palette rather than ffmpeg's default web one: the wall is
        # four greys and the banners are flat black, which dither would ruin.
        _ffmpeg(["ffmpeg", "-framerate", rate, "-i", pattern,
                 "-vf", "palettegen=max_colors=255:reserve_transparent=1",
                 "-y", palette, "-v", "error"], "palette generation")
        _ffmpeg(["ffmpeg", "-framerate", rate, "-i", pattern, "-i", palette,
                 "-filter_complex",
                 "[0:v][1:v]paletteuse=dither=none:alpha_threshold=128",
                 "-loop", "0", "-y", path, "-v", "error"], "GIF encoding")
    finally:
        shutil.rmtree(temp_dir, ignore_errors=True)
    print("  saved {} ({}x{}, {} frames, {:.1f} KiB)".format(
        path, images[0].size[0], images[0].size[1], len(images),
        os.path.getsize(path) / 1024.0))


def _ffmpeg(command, step):
    result = subprocess.run(command, capture_output=True, text=True)
    if result.returncode != 0:
        raise ToolError("{} failed: {}".format(step, result.stderr.strip()))


def build_demo():
    print("building the demo (KABLOOEY_DEMO=1)")
    env = dict(os.environ, KABLOOEY_DEMO="1")
    result = subprocess.run(["pebble", "build"], env=env, capture_output=True, text=True)
    if result.returncode != 0:
        sys.exit(result.stdout + result.stderr)
    if "DEMO MODE" not in result.stdout:
        sys.exit("The build did not report demo mode; check wscript's demo_defines().")


def main():
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--seconds", type=float, default=12.0, help="how long to record")
    parser.add_argument("--fps", type=float, default=20.0, help="frames per second")
    parser.add_argument("--out", default="screenshots", help="output directory")
    parser.add_argument("--name", default="kablooey", help="base name for the files")
    parser.add_argument("--stills", default="",
                        help="comma-separated seconds to also save as PNG stills")
    parser.add_argument("--no-build", action="store_true",
                        help="use the pbw already in build/, whatever it was built with")
    args = parser.parse_args()

    if not os.path.exists("package.json"):
        sys.exit("Run this from the project directory.")
    if not args.no_build:
        build_demo()

    pbw = os.path.join("build", "kablooey.pbw")
    if not os.path.exists(pbw):
        sys.exit("No {}; build first.".format(pbw))

    setup_environment()
    frames = DemoCapture(args).run(pbw)
    images = _resample(frames, args.fps, args.seconds)

    _write_gif(images, os.path.join(args.out, args.name + ".gif"), args.fps)

    for piece in filter(None, (s.strip() for s in args.stills.split(","))):
        when = float(piece)
        index = min(int(round(when * args.fps)), len(images) - 1)
        path = os.path.join(args.out, "{}-{:g}s.png".format(args.name, when))
        images[index].save(path)
        print("  saved {}".format(path))


if __name__ == "__main__":
    main()
