# VoxLocal 1.1.1

VoxLocal 1.1.1 fixes release installation and updates the supported OBS baseline to OBS Studio 32.2.1.

## Download

Choose one guided setup for your operating system:

- `VoxLocal-Setup-1.1.1-Windows-x64.exe`
- `VoxLocal-Setup-1.1.1-macOS-universal.dmg` for Apple Silicon and Intel Macs
- `VoxLocal-Setup-1.1.1-Linux-x86_64.run`

Close OBS before running the setup. It selects the OBS Studio 32.2.1 plugin directory automatically. On Windows this is `%ProgramData%\obs-studio\plugins`; `%APPDATA%` is not used. Restart OBS when installation finishes.

## Changes

- Built the Windows and macOS plugin against the OBS Studio 32.2.1 SDK and matching OBS dependencies.
- Replaced manual ZIP extraction with native-looking offline setup programs for all three platforms.
- Added automatic OBS plugin-folder detection, an editable installation location, and an uninstall maintenance tool.
- Corrected the Windows destination to `%ProgramData%\obs-studio\plugins`, which OBS Studio 32.2.1 actually scans.
- Combined the Apple Silicon and Intel binaries into a single universal macOS setup.
- Raised the macOS minimum to macOS 13.4, matching OBS Studio 32.2.1 and ONNX Runtime.

## Notes

- Windows and macOS setups are currently unsigned and may require explicit SmartScreen or Gatekeeper confirmation.
- The Linux setup supports native x86-64 OBS installations. Flatpak OBS uses Flatpak's own plugin mechanism.
- FFmpeg must be available locally.
- The model is downloaded during first-run setup and is not bundled in the installer.
- Kick's public chat endpoints are undocumented and may change.
