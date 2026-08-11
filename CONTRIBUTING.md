# Contributing

Build with CMake, keep the runtime free of Python/Node dependencies, and run `ctest` before opening a pull request. New chat providers belong behind `IChatConnector`; new synthesis backends belong behind `ITtsEngine`.

Do not commit models, voice samples, generated audio, credentials, OBS profiles, build directories, or `.part` downloads. Tests should use synthetic metadata and must not contain a real person’s voice.

Changes to model files require an immutable upstream revision, exact byte sizes, SHA-256 hashes, and an update to `THIRD_PARTY_NOTICES.md`.
