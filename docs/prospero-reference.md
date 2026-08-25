# Prospero reference analysis for Astro Remote

This document records architecture ideas observed in `ProsperoMgr.elf` and how Astro can reuse the concepts without copying Prospero's frontend source.

## What was confirmed from the ELF

- FreeBSD x86-64 PIE ELF, with debug info and symbols intact.
- Embedded frontend assets: `index.html`, bundled CSS, bundled JavaScript, locale bundles and icons.
- HTTP server abstraction with route registration and static fallback.
- API-oriented design instead of a monolithic HTML handler.
- Frontend is a SPA with views for home, files, payloads, repository, PKG install, saves, processes, profile, settings and logs.
- The bundled frontend uses a compact dashboard shell: sidebar, top bar, view container, status indicator and reusable panels.
- The ELF exposes a broad set of APIs such as `/api/status`, `/api/system/info`, `/api/payloads`, `/api/payloads/launch`, `/api/ftp/*`, `/api/titles`, `/api/processes`, `/api/thermal`, `/api/saves`, `/api/elfldr/status`, `/api/autoloader/status` and `/api/shutdown`.

## Astro direction

Astro should adopt the architecture, not the exact implementation or visual assets.

```text
Astro Remote
├── HTTP server
├── Router
├── Auth/session
├── API
│   ├── /api/status
│   ├── /api/system/info
│   ├── /api/elfs
│   ├── /api/elfs/launch
│   ├── /api/browser/open
│   ├── /api/pegasus/status
│   ├── /api/remote/status
│   ├── /api/remote/session/start
│   ├── /api/remote/session/stop
│   ├── /api/remote/controllers
│   └── /api/shutdown
├── Services
│   ├── ELF registry/launcher
│   ├── Browser launcher
│   ├── Pegasus bridge
│   ├── Remote input bridge
│   └── Remote video/session bridge
└── Embedded frontend
    ├── index.html
    ├── app.css
    └── app.js
```

## Astro-only Remote module

The Remote module is the main differentiator from Prospero.

Target browser-side pieces:

- Browser authentication to Astro.
- Gamepad API for one or more controllers.
- Low-latency controller state transport, initially WebSocket and later possibly WebRTC DataChannel.
- Controller slots mapped to virtual DualSense slots on the PS5 side.
- Remote video as a separate transport layer so input development does not depend on video streaming being solved first.
- Session page showing controller connectivity, latency, bitrate/stream state and PS5 connection state.

Target PS5-side pieces:

- Network input receiver.
- Controller slot manager.
- Adapter layer toward the virtual DualSense injection logic already demonstrated by Ghostcontrol-style projects.
- Session ownership and timeout logic so stale remote clients cannot keep control indefinitely.

## UI direction

Use a compact product-style shell inspired by the layout density of Prospero:

- fixed sidebar
- thin top bar
- compact status cards
- dense tables/lists instead of oversized cards
- blue Astro accent on graphite surfaces
- green only for healthy/online states
- red only for destructive actions
- responsive mobile navigation later

The file `ui_preview.html` on `feat/ui-v03` is the first Astro-specific implementation of that direction.
