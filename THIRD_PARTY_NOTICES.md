# Third-party notices

VoxLocal source code is GPL-2.0-or-later. The following projects and artifacts are used or interoperated with:

- **OBS Studio / libobs / obs-frontend-api / obs-browser** — GPL-2.0-or-later. VoxLocal is an OBS plugin and uses a private Browser Source child.
- **Qt 6** — used under the terms offered by The Qt Company, including LGPL-3.0/GPL options for the relevant modules. Distributors must preserve Qt’s license obligations.
- **nlohmann/json** — MIT License.
- **ONNX Runtime** — MIT License. Release builds may redistribute the official native runtime library.
- **Chatterbox and Chatterbox Multilingual by Resemble AI** — MIT License. VoxLocal downloads the ONNX conversion from `onnx-community/chatterbox-multilingual-ONNX` at revision `452d3f434aa592098f1eedac9099f33642ab2da5`.
- **Hugging Face model hosting** — model files are fetched over HTTPS from the immutable revision above. Their SHA-256 values and byte sizes are embedded in `src/model-manager.cpp`.
- **Kick** is a trademark of its owner. VoxLocal is unofficial, unaffiliated, and relies on public but undocumented channel and Pusher endpoints. No Kick code is redistributed.

No upstream Python package is embedded or executed. VoxLocal does not bundle the Python-only PerTh watermark implementation and therefore does not represent generated audio as watermarked.
