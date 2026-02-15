# Project Roadmap — ESP32 Reticulum Gateway

Status (2026-02-14): initial roadmap and tracked feature checklist. Work will be implemented incrementally in small PRs; CI and unit tests will guard commits.

## Goals
- Make the node easier to operate remotely (Web UI, runtime config, metrics)
- Improve reliability and update security (signed OTA, CI, tests)
- Add user-facing features (APRS, IPFS improvements, BLE provisioning)

## Milestones
- v2.1 — Observability & DX
  - [ ] CI + unit tests
  - [ ] Web UI + REST API (status, logs)
  - [ ] Runtime JSON config (SPIFFS/LittleFS)
  - [ ] /metrics endpoint + log levels
- v2.2 — Field features
  - [ ] Secure OTA with signature verification
  - [ ] BLE provisioning (GATT) for WiFi/callsign
  - [ ] IPFS: pinning & IPNS support
- v2.3 — Advanced routing & security
  - [ ] Adaptive routing metrics (ETX/RSSI)
  - [ ] Encrypted group messaging & key management
  - [ ] Crash reporting & remote core dumps (opt-in)

## Immediate next tasks (this repo change set)
- Create issues for each roadmap item (one-per-feature)
- Add CI workflow + unit-test stubs
- Add documentation (API, roadmap, contributing, security)
- Add lightweight WebServer skeleton + runtime-config example

## How we'll work
- Features implemented via separate PRs (recommended)
- Each PR must include tests or docs where applicable
- CI must pass on every PR before merge

---

If you want, I can now:
- create GitHub issues & milestones for every unchecked item, or
- start implementing items from the top of the checklist (pick 1–3 to begin)
