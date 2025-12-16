# Changelog

## Unreleased
- Added Wi-Fi connection timeout with automatic fallback access point and periodic retries to keep the main loop responsive.
- Persist Wi-Fi and basic-auth credentials in Preferences and expose them on `/config`, including reset links.
- Protected configuration and relay-changing routes with optional HTTP Basic Auth challenges.
- Updated documentation to describe the new connection flow, setup AP, authentication, and credential reset paths.
- Added grow profile presets (Seedling, Vegetative, Flowering) with a dedicated config tab and preset helper.
- Expanded timezone selection to seven options (UTC, Europe/Berlin, Europe/London, and four U.S. zones) with a System tab and timezone reporting via `/api/status`.
- Refreshed the dashboard controls with segmented AUTO/MAN toggles and live sparklines for all sensors.
- Apply Grow Profile presets atomically to avoid other form fields overwriting preset values.
- Improved accessibility and rendering resilience with focus-visible outlines, aria-pressed states on segmented controls, and sparkline rendering guards for tiny canvases.
- Added change detection for relay mode toggles, returning a `changed` flag from `/api/mode` and only persisting configuration when updates are requested.
