# VoxLocal architecture

VoxLocal is one OBS plugin module with two layers:

```text
Kick public chat
      │
      ▼
IChatConnector → CommandRouter → global TTS gate → SpeechQueue → ITtsEngine
      │                                  │                 │
      │                                  │                 └─ Chatterbox ONNX Runtime
      │                                  ├─ overlay events
      │                                  └─ 24 kHz mono float PCM
      ▼
versioned local settings          VoxLocal OBS input source
                                         │
                                         ├─ obs_source_output_audio
                                         └─ private Browser Source child
                                                └─ local HTML/CSS/JS, network blocked
```

## Boundaries

- `IChatConnector` keeps transport-specific payloads out of routing. `KickConnector` resolves a channel, subscribes to known Pusher aliases, deduplicates alias deliveries, and reconnects with jittered exponential backoff.
- `CommandRouter` is deterministic and independent from OBS. It normalizes commands with Unicode case folding, evaluates role access, sanitizes text, applies the global character limit, and selects fixed/detected language.
- `ITtsEngine` owns model-specific inference. `ChatterboxEngine` contains the native BPE tokenizer, reference-WAV preprocessing, four ONNX sessions, KV-cache generation loop, and decoder.
- `SpeechQueue` serializes inference because the Chatterbox sessions and a streamer’s audio output should have one owner. Its queue is bounded and cancellable.
- `VoxLocalRuntime` coordinates settings, FFmpeg media-to-WAV voice import, model installation, Kick, routing, and queue events without including OBS headers. Its global gate skips commands while synthesis is active and starts the cooldown only when audio is ready for OBS playback.
- `voxlocal-source.cpp` is the OBS adapter. It renders an internal private Browser Source and is the only component that submits PCM to OBS, preventing doubled audio.

## Local state

`settings.json` has an explicit schema version and is written atomically. FFmpeg converts the first audio track from an imported audio/video file into 24 kHz mono PCM at `voices/<persona UUID>.wav`; the original file is not needed afterward. Model files live below `models/chatterbox-multilingual-onnx/<revision>` and a ready marker is written only after every file passes its pinned SHA-256.

## Extension points

A future platform connector implements `IChatConnector` and emits the same normalized `ChatMessage`. A future TTS backend implements `ITtsEngine` and returns 24 kHz-or-declared float PCM. Neither requires changes to command policies, OBS audio ownership, or the overlay event contract.
