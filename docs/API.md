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
- POST /api/v1/ota
  - Upload a signed firmware image for OTA. Requires `OTA_ENABLED` and an Ed25519 `public_key` in `/config.json` under `api.public_key`.
  - Headers: `X-Signature-Ed25519: <hex-signature>` (64-byte signature, hex-encoded)
  - Body: raw firmware binary (application/octet-stream)

Example (host):

curl -X POST \
  -H "Authorization: Bearer <API_TOKEN>" \
  -H "X-Signature-Ed25519: <hex-signature>" \
  --data-binary @firmware.bin \
  http://<node-ip>/api/v1/ota

Security notes:
- When `api.token` is set in config, all sensitive endpoints require `Authorization: Bearer <token>`.
- Keep the private signing key offline and only sign release artifacts.

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