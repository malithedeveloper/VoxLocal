# VoxLocal 1.1.2

VoxLocal 1.1.2 fixes Windows TTS model startup, makes model loading non-blocking, and gives the streamer explicit control over whether the model enters memory when OBS starts.

## Download

Choose one setup for your operating system:

- `VoxLocal-Setup-1.1.2-Windows-x64.exe`
- `VoxLocal-Setup-1.1.2-macOS-universal.dmg` for Apple Silicon and Intel Macs
- `VoxLocal-Setup-1.1.2-Linux-x86_64.run`

Close OBS before running the setup. The Windows setup uses `%ProgramData%\obs-studio\plugins` and removes VoxLocal's obsolete `%APPDATA%` plugin copy. Existing settings, voices, and downloaded models remain untouched.

## Changes

- Applied the session options required by ONNX Runtime DirectML, fixing model initialization failures on Windows.
- Updated the Windows inference runtime from DirectML 1.22.1 to 1.24.4.
- Automatically chooses the hardware GPU with the most dedicated video memory on Windows. If the GPU or any model session cannot initialize, VoxLocal recreates every session on CPU automatically.
- Detects the available ONNX execution provider on each platform and displays the backend selected for the current session.
- Opens the OBS interface first and initializes the model on a background worker afterward, so loading model weights does not block OBS startup.
- Added three model startup behaviors to Welcome and the dock: **Ask at startup** (default), **Load automatically**, and **Do not load**.
- Added **Load model now** to the dock for manual per-session loading.
- Keeps the VoxLocal dock closed while Welcome is active. Welcome cannot be dismissed with Cancel, Escape, or the window close button, but every page has **Skip** and all setup sections are optional.
- Writes model load and inference failures into the OBS log for useful diagnostics.
- Avoids reading a closed TLS socket after a completed model download.
- Retains the 1.1.1 Turkish tokenizer bounds fix and larger Minimal overlay text.

## Notes

- The startup behavior controls loading the already-downloaded model into memory; it does not delete or redownload model files.
- Windows and macOS setups are unsigned and may require SmartScreen or Gatekeeper confirmation.
- The Linux setup targets native x86-64 OBS installations. Flatpak OBS uses Flatpak's own plugin mechanism.
- FFmpeg must be available locally to import voice samples.
