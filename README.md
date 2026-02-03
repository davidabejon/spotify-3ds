Nota: Esta documentación también está disponible en español: `README_es.md`

# Spotify 3DS

Unofficial Spotify client for the Nintendo 3DS (proxy + 3DS application).

<table>
  <tr>
    <td><img src="https://i.imgur.com/BiwIS82.png" alt="Top playing" width="320"></td>
    <td><img src="https://i.imgur.com/ppvuOb7.png" alt="Bottom playing" width="320"></td>
  </tr>
</table>

## Overview
This repository contains:
- A proxy server (FastAPI) that handles Spotify authentication and exposes endpoints for the console: [server/server.py](server/server.py). See also [server/Dockerfile](server/Dockerfile) and [server/spotify_config.json](server/spotify_config.json).
- The 3DS client application (source and assets) in the `client` folder: [client/Makefile](client/Makefile), [client/source/main.c](client/source/main.c) and the display interface [client/include/image_display.h](client/include/image_display.h) (e.g. `initNetwork`).

## Requirements
- Docker or Python (depending on whether you choose step 1.a or 1.b)
- To build the client from source: devkitARM/3DS homebrew toolchain (see `client/Makefile`)
- A Spotify Developer application (Client ID / Client Secret)

## Quick start

### 1) Deploy the proxy server
You can deploy the server with Docker (recommended) or run it directly with Python.

### 1.a) Proxy server with Docker
1. Download the release `spotify3ds-proxy.tar` from the project's Releases.
2. Load the Docker image:
   ```sh
   docker load -i spotify3ds-proxy.tar
   ```
3. Run the container:
   ```sh
   docker run -d --name spotify3ds-proxy -p 8000:8000 spotify3ds-proxy
   ```
4. Check container status:
   ```sh
   docker ps
   ```
5. Enter the container to edit configuration:
   ```sh
   docker exec -it spotify3ds-proxy bash
   apt update && apt install nano -y
   nano spotify_config.json
   ```
   Fill `client_id` and `client_secret` (from the Spotify dashboard) and save.
6. On the first run the server will try to open the authorization URL from the container host; if it doesn't open you will see the URL in the container or host console. Open it in a browser and authorize. After authorization the server will store the `code`/`refresh_token` in `spotify_config.json`.

### 1.b) Proxy server directly with Python

1. Clone the repo and enter the server folder:
   ```sh
   git clone https://github.com/davidabejon/spotify-3ds.git
   cd spotify-3ds/server
   ```

2. Create and activate a virtual environment (Windows):
   ```ps
   python -m venv .venv
   .venv\Scripts\activate
   ```

3. Install dependencies:
   ```ps
   pip install -r requirements.txt
   ```
   (If there is no requirements.txt, install FastAPI and Uvicorn: `pip install fastapi uvicorn requests`.)

4. Edit `spotify_config.json` and add `client_id` and `client_secret` from the Spotify Dashboard. Optionally adjust `redirect_uri`.

5. Run the server with Uvicorn (from the `server` folder):
   ```ps
   uvicorn server:app --host 0.0.0.0 --port 8000 --reload --reload-include spotify_config.json
   ```
   - If the main module name differs adjust `module:app` accordingly.
   - On first run the server will show or open the authorization URL; complete the flow in the browser so the `refresh_token` is saved to `spotify_config.json`.

### 2) 3DS Application
- Easiest: download `Spotify3DS.cia` from [Releases](https://github.com/davidabejon/spotify-3ds/releases), copy to the SD card and install with FBI or another CIA installer
- Alternatively download `spotify-3ds.zip` package from [Releases](https://github.com/davidabejon/spotify-3ds/releases): extract into `/3ds` on the SD card and run via the Homebrew Launcher.

On first run the app will prompt you to enter the proxy server's IP address; after submitting it will attempt to connect automatically.

To change the IP later, press Y to reopen the on-screen keyboard and enter a new address.

#### Controls
 - A — Play / Pause
 - Y — Open IP entry (on-screen keyboard)
 - D-Pad Right — Next track
 - D-Pad Left — Previous track
 - D-Pad Up — Volume up
 - D-Pad Down — Volume down
 - START — Exit the app

## License and credits
- License: Apache License 2.0 — see the `LICENSE` file at the repository root.
- Original author: David Abejón.
- Included libraries: stbi (stb_image) at `client/include/stb_image.h` and other dependencies listed in Docker/requirements.