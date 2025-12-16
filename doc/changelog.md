# Changelog

## Unreleased
- Added Wi-Fi connection timeout with automatic fallback access point and periodic retries to keep the main loop responsive.
- Persist Wi-Fi and basic-auth credentials in Preferences and expose them on `/config`, including reset links.
- Protected configuration and relay-changing routes with optional HTTP Basic Auth challenges.
- Updated documentation to describe the new connection flow, setup AP, authentication, and credential reset paths.
- Added grow profile presets (Seedling, Vegetative, Flowering) with a dedicated config tab and preset helper.
- Added configurable timezone selection (EU/US) with a System tab and timezone reporting via `/api/status`.
- Refreshed the dashboard controls with segmented AUTO/MAN toggles and live sparklines for all sensors.
