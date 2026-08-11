# Security policy

Please report vulnerabilities privately through GitHub Security Advisories once the repository is published. Do not include private voice samples, OBS profiles, tokens, or chat logs in a public issue.

VoxLocal treats voice files and configuration as local private data. It does not expose an HTTP server. Its overlay is a local file with a restrictive Content Security Policy. Kick connectivity and first-time model downloads are the only expected network operations.

The Kick connector limits text WebSocket frames to 2 MiB, ignores unrelated event types, deduplicates repeated aliases, and never evaluates chat text as code. Overlay styles and scripts are bundled with the plugin and are not user-injectable.
