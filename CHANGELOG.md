# Changelog

## 1.1.1 — 2026-08-13

- Updated the Windows and macOS SDK builds to OBS Studio 32.2.1 and its matching 2026-07-15 dependencies.
- Replaced manual ZIP installation with three guided offline setups: Windows x64, macOS universal, and Linux x86-64.
- Added automatic per-user OBS plugin-directory detection and a directory chooser for portable or custom OBS installations.
- Combined Apple Silicon and Intel plugin binaries into one universal macOS setup.
- Raised the macOS requirement to version 13 to match OBS Studio 32.2.1.
- Added tag-driven GitHub Release publishing for the three setup files.

## 0.1.0 — 2026-08-11

- Added native OBS integration for local Chatterbox Multilingual ONNX inference.
- Added resumable, checksum-verified model downloads.
- Added FFmpeg-based audio and video voice-sample preparation.
- Added live Kick chat commands, persona role policies, and automatic reconnection.
- Added playback-based global cooldown, maximum message length, and global TTS enable controls.
- Added moderator and broadcaster `!ttson` and `!ttsoff` commands.
- Added Minimal and Subtitle overlays with Kick chat colors and configurable entrance animations.
- Added native color and font pickers.
- Added complete English and Turkish UI modes.
- Added native OBS plugin packages for Linux x86-64, Windows x64, macOS Apple Silicon, and macOS Intel.
