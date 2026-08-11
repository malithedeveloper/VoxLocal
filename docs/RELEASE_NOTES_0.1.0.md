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

`voxlocal-0.1.0-Linux-x86_64.zip` contains the tested Linux x86-64 plugin. Extract the included `voxlocal` directory to `~/.config/obs-studio/plugins`, then restart OBS.

Windows and macOS source support is included, but signed native packages are not part of this release.

## Notes

- FFmpeg must be installed locally.
- The model is downloaded during first-run setup and is not bundled in the release archive.
- CPU inference works but may be slow enough to be unsuitable for live use. A compatible accelerated ONNX Runtime is recommended.
- Kick's public chat endpoints are undocumented and may change.
