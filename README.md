# LANSCR (Windows)

LANSCR is a lightweight Windows app for **live screen sharing over LAN / Wi-Fi**.

It can stream:
- Screen video as **MJPEG over HTTP**
- System audio as **WAV (PCM16) over HTTP** (optional)

The full software is implemented in one C++ file: `lanscr.cpp`.

---

## Quick start (end users)

1. Download the latest build from GitHub **Releases**.
2. Extract the ZIP.
3. Run:
   - Double-click `LANSCR.exe` (opens the GUI launcher), or
   - Start a server from a terminal: `LANSCR.exe server 8000`

You do **not** need Visual Studio to run the prebuilt EXE.

---

## Demo video

Add a short demo video showing how to:
- start `server` from the GUI/CLI
- open the browser viewer (`http://<ip>:<port>/`)
- optionally run the native `client`

Recommended: put your demo file at `docs/demo.mp4`.

<!-- If GitHub renders this tag for your viewer, it will play inline. -->
<p align="center">
  <video src="docs/demo.mp4" controls muted playsinline style="max-width: 100%;"></video>
</p>

If the embedded player doesn’t show up in your GitHub view, use this direct link instead: [docs/demo.mp4](docs/demo.mp4)

---

## What does it do?

LANSCR can run in multiple modes:

- **Server mode**: captures your screen and streams it over HTTP as **MJPEG**, and streams system audio as **WAV (PCM16)**.
- **Client mode**: connects to a server URL, displays the MJPEG stream in a native window, and plays the WAV audio.
- **Browser viewer**: when you open the server URL in a browser, the server serves a simple landing page that shows the video (`/mjpeg`) and audio (`/audio`).
- **GUI launcher**: when you double‑click `LANSCR.exe` with no arguments, it shows a small Win32 UI to start/stop server and open viewer links.
- **Extra commands**: mute server audio (`/control?mute=1`), stop a local server by port, and “detect” running servers on any port.

---

## Features (high level)

- Live screen streaming over HTTP MJPEG (`/mjpeg`) with a browser landing page (`/`)
- Live system-audio streaming over HTTP WAV (`/audio`) using WASAPI loopback
- Native Windows client viewer (WinHTTP + WIC + Win32) with local mute
- Server-side mute control endpoint (`/control?mute=0|1`) returning JSON status
- GUI launcher (double-click) to start/stop server, open browser, open viewer, and manage mute
- Detect running servers and stop selected/all (CLI and GUI)
- Optional UDP video-only mode for low-latency experiments (`udp-server` / `udp-client`)

## Use cases

- Share your screen to a phone/tablet on the same Wi-Fi
- Local presentations/classrooms where all devices are on the same LAN
- Quick “second screen” preview on another Windows PC
- Troubleshooting: start server on a PC, open the stream from another device on the LAN

---

## Why this app?

- **No dependencies**: built with Windows APIs (WinSock, WinHTTP, WIC, WASAPI, WinMM).
- **Low setup**: one EXE for running.
- **LAN-first**: intended for trusted local networks.

---

## How it works (based on `lanscr.cpp`)

### Screen capture → JPEG
- Captures the **virtual screen** (multi-monitor) using GDI (`BitBlt`).
- Draws the mouse cursor on top (hardware cursor isn’t included in BitBlt).
- Encodes frames to JPEG using Windows Imaging Component (WIC).
- A single capture thread produces frames shared to all clients.

### Video streaming (MJPEG over HTTP)
- The server is a small HTTP server built on WinSock.
- Video endpoint:
  - `GET /mjpeg` (also default for unknown paths)
  - Response is `multipart/x-mixed-replace` with boundary `frame`.
- The server uses non-blocking sockets and bounded writes to reduce latency; slow clients get dropped rather than accumulating many seconds of delay.

### Audio streaming (WAV over HTTP)
- Audio endpoint: `GET /audio`
- Captures **system output** using WASAPI loopback (default render device).
- Streams a WAV header followed by continuous PCM16 samples.

### Control endpoint (mute)
- `GET /control?mute=0|1` toggles server-side audio mute.
- Returns JSON status: `{ "audioMuted": true/false }`.

### Client viewer (native)
- Fetches MJPEG with WinHTTP, parses JPEG parts, decodes via WIC, and draws frames in a Win32 window.
- Fetches `/audio`, parses the WAV header, then plays PCM via WinMM (`waveOut*`).
- `--mute` (client flag) mutes playback locally without stopping the connection.


---

## Command line usage

Run `LANSCR.exe --help` to see the full list. Common ones:

```text
LANSCR.exe server <port> [fps] [jpegQuality0to100]
LANSCR.exe client <url>
LANSCR.exe udp-server <port> [fps] [jpegQuality0to100]
LANSCR.exe udp-client <serverIp> <port>
LANSCR.exe audio-mute <urlOrPort> <0|1>
LANSCR.exe stop <port>
LANSCR.exe detect
```

Examples:

```text
LANSCR.exe server 8000 10 80
LANSCR.exe client http://192.168.1.50:8000/
LANSCR.exe udp-server 9000 60 70
LANSCR.exe udp-client 192.168.1.50 9000
LANSCR.exe audio-mute 8000 1
LANSCR.exe stop 8000
```

---

## Build from source (developers)

You need MSVC + Windows SDK.

1. Install :
   - Visual Studio 2022 Community → workload: **Desktop development with C++**
   - Visual Studio 2022 Build Tools → **MSVC v143** + **Windows 10/11 SDK**
2. In this folder run:
   - Double-click `Setup.bat' and choose No, that first downloads the Visual Studio then Builds the exe file from that CPP Source file
Notes:
- `run.bat` is a helper and may offer to install Build Tools using winget.
- `build.bat` compiles `lanscr.rc` into `LANSCR.res` and links it into the final EXE.

---

## EXE icon + version metadata + manifest

These files are embedded into `LANSCR.exe` at build time:

- `lanscr.rc`
  - icon resource
  - version info (Explorer → Properties → Details)
  - embeds the manifest (`RT_MANIFEST "lanscr.manifest"`)
- `lanscr.manifest`
- `lanscr.ico`

---

## Security notes (important)

LANSCR is intended for **trusted LAN use**.

- The HTTP stream is not encrypted and has no authentication.
- Anyone on the same network who can reach the port can view the stream.
- Use Windows Firewall/router rules if you need to restrict access.

---

## Troubleshooting

### “Unknown Publisher” / Security warning when running the EXE
This is expected for any **unsigned** EXE downloaded from the internet.

- The **Publisher** shown in the Windows security dialog comes from a **digital signature** (code-signing certificate).
- The metadata in `lanscr.rc` shows up under **Explorer → Properties → Details**, but it does **not** make Windows trust the file.

To remove “Unknown Publisher”, you must **code-sign** the EXE with a trusted certificate.

Tip (downloaded ZIPs): after extracting, right-click `LANSCR.exe` → **Properties** → check **Unblock** (if shown).
Or PowerShell:

```powershell
Unblock-File .\LANSCR.exe
```

### Audio plays only after clicking
Browsers often block autoplay audio. On the web page, click **Enable Audio**.

### Build: “cl.exe not found”
Install MSVC Build Tools/Visual Studio (see “Build from source”).

---

## Repo contents

- `lanscr.cpp` — the full application (server + client + GUI)
- `lanscr.rc` / `lanscr.manifest` / `lanscr.ico` — icon + version metadata + embedded manifest
- `build.bat` / `run.bat` — build helpers
