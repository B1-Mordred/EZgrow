# Changelog

## Unreleased
- Added chamber-scoped grow profile selectors with a `/api/grow/apply` endpoint, per-chamber soil inputs, HTML-stripped chamber names (1–24 chars), and updated config helper text for the shared pump.
- Expanded `/api/grow/apply` to accept zero- or one-based chamber identifiers (returning both `chamber_id` and `chamber_idx`) and added chamber `idx` metadata to `/api/status` payloads.
- Introduced per-chamber configuration (names, dry/wet thresholds, optional profile links) with new NVS keys, migration from legacy soil settings, and validation that enforces sane ranges.
- Added dashboard stale detection with reconnect banner, throttled error toasts, and sensor sparklines that clear after repeated invalid readings.
- Provided AUTO block reason feedback for manual toggles (with pump-on confirmation) and improved config UI with grow profile previews plus current time display.
- Added Wi-Fi connection timeout with automatic fallback access point and periodic retries to keep the main loop responsive.
- Persist Wi-Fi and basic-auth credentials in Preferences and expose them on `/config`, including reset links.
- Protected configuration and relay-changing routes with optional HTTP Basic Auth challenges.
- Updated documentation to describe the new connection flow, setup AP, authentication, and credential reset paths.
- Added per-chamber preset previews and confirmation prompts, embedding preset metadata on grow profile selectors for clearer apply actions.
- Added grow profile presets (Seedling, Vegetative, Flowering) with a dedicated config tab and preset helper.
- Added IANA timezone identifiers for all timezone options, exposing `timezone_iana` on `/api/status` and preferring them for dashboard time labels.
- Expanded timezone selection to seven options (UTC, Europe/Berlin, Europe/London, and four U.S. zones) with a System tab and timezone reporting via `/api/status`.
- History chart labels now attempt to use the device timezone reported by `/api/status`, with a safe fallback when unavailable.
- Refreshed the dashboard controls with segmented AUTO/MAN toggles and live sparklines for all sensors.
- Apply Grow Profile presets atomically to avoid other form fields overwriting preset values.
- Improved accessibility and rendering resilience with focus-visible outlines, aria-pressed states on segmented controls, and sparkline rendering guards for tiny canvases.
- Added change detection for relay mode toggles, returning a `changed` flag from `/api/mode` and only persisting configuration when updates are requested.
- Guarded dashboard relay controls during `/api/mode` and `/api/toggle` calls, disabling the segmented buttons and toggles while requests are in-flight and surfacing toast feedback.
- Extended `/api/status` with chamber metadata (names, soil readings, thresholds, light relay mapping) and wired the dashboard to use escaped chamber labels for soil tiles, control cards, and history datasets (with sane fallbacks).
- Apply timezone configuration only when a new selection is provided, applying the change once after saving.
- Standardized dashboard sparkline ranges with clamped values for temperature, humidity, and soil moisture to improve readability.
- Prevented long chamber names from breaking tile or control headers by clamping labels with ellipsis-aware overflow handling for desktop and mobile layouts.
- Documented configuration tabs (environment, lights, automation, grow profiles, system timezone, security) to match the current UI.
- Added chamber-targeted grow profile application (Ch1→Light1, Ch2→Light2) that updates only soil thresholds and linked light schedules, plus updated previews and UI actions to persist the change.
- Pump automation now tracks which chambers were dry at start, watering them until their individual wet thresholds are reached (or max-on expires) while preserving minimum-off timing.
