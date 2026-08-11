# VoxLocal 0.1.0

VoxLocal's first public release brings local multilingual voice-cloned TTS directly into OBS Studio for Kick streams.

## Highlights

- Chatterbox Multilingual ONNX runs locally after the initial model download.
- Audio and video files can be used as zero-shot persona samples.
- Turkish and English are fully supported in the native interface.
- Viewer commands are gated by persona permissions and a playback-based global cooldown.
- Moderators and broadcasters can use `!ttson` and `!ttsoff` live in chat.
- The native OBS source outputs audio, enables local monitoring, and renders Minimal or Subtitle captions.
- Overlay animation, colors, font, sender name, maximum characters, and cooldown are configurable from the VoxLocal dock.

## Download

Choose the package matching your platform:

- `voxlocal-0.1.0-Linux-x86_64.zip`
- `voxlocal-0.1.0-Windows-x64.zip`
- `voxlocal-0.1.0-macOS-arm64.zip` for Apple Silicon
- `voxlocal-0.1.0-macOS-x86_64.zip` for Intel Macs

On Windows and Linux, copy the included `voxlocal` directory to the OBS per-user plugin directory. On macOS, copy `voxlocal.plugin` to `~/Library/Application Support/obs-studio/plugins`. Restart OBS afterward.

## Notes

- FFmpeg must be installed locally.
- Windows and macOS packages are unsigned. macOS may require removing the quarantine attribute from the extracted plugin bundle.
- The model is downloaded during first-run setup and is not bundled in the release archive.
- CPU inference works but may be slow enough to be unsuitable for live use. A compatible accelerated ONNX Runtime is recommended.
- Kick's public chat endpoints are undocumented and may change.
