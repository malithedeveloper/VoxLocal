<p align="center">
  <img src="assets/voxlocal-logo.png" width="180" alt="VoxLocal logo">
</p>

# VoxLocal

VoxLocal is a native OBS Studio plugin that turns live Kick chat commands into local, multilingual, voice-cloned speech. The interface lives inside OBS and inference stays on the streamer's computer. There is no Python runtime, local web server, cloud TTS account, or separate desktop application.

> VoxLocal uses Kick's public chat transport and channel lookup endpoints. These interfaces are undocumented and may change without notice.

## Features

- Local Chatterbox Multilingual ONNX inference with Turkish and 22 other languages.
- Checksum-verified model download with pause/resume support and detailed byte progress.
- Zero-shot voice cloning from audio or video. FFmpeg extracts the first audio track and stores a validated 24 kHz mono WAV locally.
- Multiple personas, each with its own chat command, language mode, voice sample, and viewer role policy.
- Live Kick chat over its public Pusher transport with duplicate filtering and automatic reconnection.
- Global TTS enable switch, maximum message length, and playback-based global cooldown.
- Moderator and broadcaster commands: `!ttson` and `!ttsoff`.
- Two OBS overlay presets:
  - **Minimal:** a compact KickBot-inspired translucent black message card.
  - **Subtitle:** a centered `username: message` caption using the viewer's Kick chat color.
- Fade, left, right, top, bottom, and instant entrance animations.
- Native font picker, color pickers, and sender-name visibility control.
- English and Turkish interfaces without mixed-language labels.
- TTS audio is sent to the OBS mix and automatically enabled for local monitoring.

## Cooldown behavior

VoxLocal accepts only one viewer TTS request at a time. While that request is being generated, later TTS commands are skipped rather than queued. The global cooldown begins when the generated audio reaches OBS and starts playing. Commands received during the cooldown are also skipped.

Preview requests from the VoxLocal dock are independent from the enabled switch, so a streamer can test a persona while viewer TTS is disabled.

## Requirements

- OBS Studio 31 or newer with the official Browser Source component.
- Windows 10/11 x64, macOS 13+, or x86-64 Linux.
- FFmpeg available on `PATH`, beside OBS, or through `VOXLOCAL_FFMPEG`.
- Enough memory for the quantized Chatterbox model.
- A short, clean recording containing one clearly audible speaker.

VoxLocal attempts DirectML on Windows, CoreML on macOS, and CUDA on Linux when the linked ONNX Runtime exposes the provider. It falls back to CPU inference. CPU generation can be slow; GPU acceleration is recommended for live use.

## Install a release

1. Download the package for your platform from [Releases](../../releases).
2. Extract the included `voxlocal` directory into your per-user OBS plugin directory:

   - Windows: `%APPDATA%\obs-studio\plugins`
   - Linux: `~/.config/obs-studio/plugins`
   - macOS: `~/Library/Application Support/obs-studio/plugins`

3. Restart OBS.
4. Complete the VoxLocal setup wizard.

The first release currently provides a tested Linux x86-64 binary and source code. Windows and macOS builds are checked at the portable core level; native signed plugin packages are planned.

## First run

1. Select English or Turkish.
2. Download and verify the local model. Setup cannot continue before it finishes.
3. Enter the public Kick channel slug.
4. Create a persona with a name, command, language, access policy, and audio or video sample.
5. VoxLocal adds **VoxLocal Overlay** to the current scene.

The settings dock opens automatically. Reopen it from **Tools → VoxLocal Settings** or press `Ctrl+Shift+V`.

Example viewer command:

```text
!voice Hello from chat
```

Moderator and broadcaster controls:

```text
!ttsoff
!ttson
```

The overlay displays a short enabled or disabled notice when either command is accepted.

## Build from source

VoxLocal uses C++20, Qt 6, CMake, ONNX Runtime, FFmpeg, and the OBS plugin API.

```bash
cmake --preset linux
cmake --build --preset linux
ctest --preset linux
cmake --install build/linux --prefix release/voxlocal
```

Required development components are Qt 6 Core, Concurrent, Network, WebSockets, Gui and Widgets; nlohmann-json; OBS headers; `libobs`; and `obs-frontend-api`.

If `ONNXRUNTIME_ROOT` is not set, CMake downloads a pinned ONNX Runtime package. Linux defaults to the CPU package. Point `ONNXRUNTIME_ROOT` at a compatible CUDA build for CUDA inference.

Useful options:

```text
VOXLOCAL_BUILD_PLUGIN=ON|OFF
VOXLOCAL_BUILD_TESTS=ON|OFF
VOXLOCAL_ENABLE_ONNX=ON|OFF
VOXLOCAL_FETCH_ONNXRUNTIME=ON|OFF
```

## Local data and privacy

Settings, prepared voices, and model files are stored under the OBS plugin configuration directory. Voice samples are never uploaded by VoxLocal. Kick connectivity and the first model download are its only expected network operations.

Only clone voices you have the right to use. VoxLocal does not enforce a consent checkbox, but removing that checkbox does not remove your legal or ethical responsibility.

The overlay is bundled local HTML, CSS, and JavaScript with a Content Security Policy that blocks network connections and remote media.

## Project status

The full plugin and ONNX inference path are tested on Linux. GitHub Actions tests the portable core on Windows and macOS and the complete OBS plugin on Linux. See [Architecture](docs/ARCHITECTURE.md), [third-party notices](THIRD_PARTY_NOTICES.md), and the [security policy](SECURITY.md).

## License

VoxLocal is licensed under GPL-2.0-or-later because it links with OBS Studio. Model and dependency licenses are documented separately in [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).
