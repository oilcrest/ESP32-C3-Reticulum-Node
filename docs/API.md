# Web UI & REST API — Spec (draft)

This document describes the initial REST API and WebSocket endpoints for the planned Web UI.
All endpoints require authentication when enabled (future: token / password protected).

## Endpoints (HTTP, JSON)
- GET /api/v1/status
  - Returns device status, uptime, heap, active links, routing table summary.
- GET /api/v1/config
  - Returns current runtime config (from JSON config store if enabled).
- POST /api/v1/config
  - Accepts JSON body to update runtime config (validated); responds with saved config.
- POST /api/v1/config/save
  - Persist current runtime config to SPIFFS/LittleFS (if JSON_CONFIG_ENABLED).
- POST /api/v1/restart
  - Request soft restart of node (admin only).
- GET /api/v1/metrics
  - Prometheus-style or JSON metrics endpoint (if METRICS_ENABLED).

## WebSocket
- /ws/console — real-time log stream and packet events
  - Messages: { "type": "log", "level": "info", "msg": "..." }
  - Messages: { "type": "packet", "dir": "rx|tx", "hex": "...", "decoded": {...} }

## Security & Auth (design)
- Web UI must be behind a password or token by default.
- Use HTTPS for remote access; otherwise restrict to local networks.
- Future: integrate signed JWT tokens and role-based control.

## Implementation notes
- Minimal server will be implemented as optional module compiled when `WEBSERVER_ENABLED`.
- Use AsyncWebServer (recommended) or WebServer for smaller footprint.
- Config persistence stored in `/config.json` when `JSON_CONFIG_ENABLED`.

---

This is a draft — endpoints and payloads will be finalized while implementing the Web UI feature.