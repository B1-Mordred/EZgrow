#pragma once

// Initialize HTTP server and register routes
void initWebServer();

// Process incoming HTTP requests
void handleWebServer();

// Keep captive portal DNS/auth state aligned with current Wi-Fi mode.
void refreshCaptivePortalState();
