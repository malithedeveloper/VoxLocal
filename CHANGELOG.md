# Changelog

## 1.1.2 — 2026-08-14

- Fixed Windows model startup by applying DirectML's required sequential/no-memory-pattern session settings.
- Updated Windows to Microsoft ONNX Runtime DirectML 1.24.4 and added automatic strongest-GPU selection with full CPU fallback.
- Moved model initialization to a background worker after OBS finishes loading, preventing the OBS interface from freezing during model startup.
- Added `Ask at startup`, `Load automatically`, and `Do not load` model-memory policies to both Welcome and the VoxLocal dock; the default is `Ask at startup`.
- Kept the VoxLocal dock hidden until Welcome finishes and made Welcome non-dismissible, while adding a `Skip` button to every page so optional setup sections never block progress.
- Added a manual `Load model now` action and a visible loaded backend indicator to the dock.
- Logged model initialization and inference failures to the OBS log instead of showing them only in the dock.
- Prevented harmless reads from an already-closed TLS reply after a completed model download.
- Made the Windows setup remove VoxLocal's obsolete `%APPDATA%` plugin copy when installing to `%ProgramData%`, avoiding duplicate-version conflicts.

## 1.1.1 — 2026-08-13

- Updated the Windows and macOS SDK builds to OBS Studio 32.2.1 and its matching 2026-07-15 dependencies.
- Replaced manual ZIP installation with three guided offline setups: Windows x64, macOS universal, and Linux x86-64.
- Added automatic OBS plugin-directory detection and a directory chooser for portable or custom OBS installations.
- Fixed the Windows setup to install under `%ProgramData%\obs-studio\plugins`, the directory scanned by OBS Studio 32.2.1, instead of the unscanned `%APPDATA%` directory.
- Added automatic Windows administrator elevation for writing the system plugin bundle.
- Rebuilt Qt WebSockets against OBS Studio 32.2.1's exact Qt 6.11.1 runtime, fixing the Windows `Module ... not loaded` failure caused by mixing Qt 6.8.3 with OBS.
- Added a release-blocking Windows load test against the official OBS Studio 32.2.1 portable runtime and a Linux unresolved-dependency check.
- Stopped bundling Ubuntu's Qt WebSockets library in the Linux setup, preventing it from overriding OBS's distribution-matched Qt runtime.
- Bundled and registered OBS-matched Qt's Windows Schannel backend, fixing HTTPS model downloads when OBS provides no TLS plugin.
- Added a release-blocking Windows TLS runtime test in addition to the plugin DLL load test.
- Prevented Chatterbox crashes on Turkish `ş`, `ğ`, and `İ` caused by tokenizer IDs beyond the exported ONNX embedding table; accented letters now use safe phonetic fallbacks.
- Increased the Minimal overlay's requester and speech text sizes for readability when the OBS source is scaled down.
- Combined Apple Silicon and Intel plugin binaries into one universal macOS setup.
- Raised the macOS requirement to version 13.4 to match OBS Studio 32.2.1 and ONNX Runtime.
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
