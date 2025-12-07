# Changelog

## Unreleased
- Added Wi-Fi connection timeout with automatic fallback access point and periodic retries to keep the main loop responsive.
- Persist Wi-Fi and basic-auth credentials in Preferences and expose them on `/config`, including reset links.
- Protected configuration and relay-changing routes with optional HTTP Basic Auth challenges.
- Updated documentation to describe the new connection flow, setup AP, authentication, and credential reset paths.
