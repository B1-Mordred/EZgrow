# Changelog

## Unreleased
- Introduced per-chamber configuration (names, dry/wet thresholds, optional profile links) with new NVS keys, migration from legacy soil settings, and validation that enforces sane ranges.
- Added dashboard stale detection with reconnect banner, throttled error toasts, and sensor sparklines that clear after repeated invalid readings.
- Provided AUTO block reason feedback for manual toggles (with pump-on confirmation) and improved config UI with grow profile previews plus current time display.
- Added Wi-Fi connection timeout with automatic fallback access point and periodic retries to keep the main loop responsive.
- Persist Wi-Fi and basic-auth credentials in Preferences and expose them on `/config`, including reset links.
- Protected configuration and relay-changing routes with optional HTTP Basic Auth challenges.
- Updated documentation to describe the new connection flow, setup AP, authentication, and credential reset paths.
- Added grow profile presets (Seedling, Vegetative, Flowering) with a dedicated config tab and preset helper.
- Expanded timezone selection to seven options (UTC, Europe/Berlin, Europe/London, and four U.S. zones) with a System tab and timezone reporting via `/api/status`.
- History chart labels now attempt to use the device timezone reported by `/api/status`, with a safe fallback when unavailable.
- Refreshed the dashboard controls with segmented AUTO/MAN toggles and live sparklines for all sensors.
- Apply Grow Profile presets atomically to avoid other form fields overwriting preset values.
- Improved accessibility and rendering resilience with focus-visible outlines, aria-pressed states on segmented controls, and sparkline rendering guards for tiny canvases.
- Added change detection for relay mode toggles, returning a `changed` flag from `/api/mode` and only persisting configuration when updates are requested.
- Guarded dashboard relay controls during `/api/mode` and `/api/toggle` calls, disabling the segmented buttons and toggles while requests are in-flight and surfacing toast feedback.
- Apply timezone configuration only when a new selection is provided, applying the change once after saving.
- Standardized dashboard sparkline ranges with clamped values for temperature, humidity, and soil moisture to improve readability.
- Documented configuration tabs (environment, lights, automation, grow profiles, system timezone, security) to match the current UI.
