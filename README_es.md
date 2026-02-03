# Spotify 3DS

Cliente no oficial de Spotify para Nintendo 3DS (proxy + aplicación 3DS).

<table>
  <tr>
    <td><img src="https://i.imgur.com/BiwIS82.png" alt="Top playing" width="320"></td>
    <td><img src="https://i.imgur.com/ppvuOb7.png" alt="Bottom playing" width="320"></td>
  </tr>
</table>

## Resumen
Este repositorio contiene:
- Un servidor proxy (FastAPI) que gestiona la autenticación con Spotify y expone endpoints para la consola: [server/server.py](server/server.py) — ver también [server/Dockerfile](server/Dockerfile) y [server/spotify_config.json](server/spotify_config.json).
- La aplicación cliente para 3DS (código fuente y arte) en la carpeta `client`: [client/Makefile](client/Makefile), [client/source/main.c](client/source/main.c) y la interfaz de pantalla [client/include/image_display.h](client/include/image_display.h) (función `initNetwork`).

## Requisitos
- Docker o Python (dependiendo de si eliges el paso 1.a o 1.b)
- Para compilar el cliente desde código: toolchain devkitARM/3DS homebrew environment (ver `client/Makefile`).
- Crear una aplicación en el Developer Portal de Spotify

## Uso rápido

### 1) Desplegar servidor proxy
 - Puedes elegir desplegar el servidor con Docker (recomendado) o ejecutarlo directamente desde Python clonando el repositorio.
### 1.a) Servidor proxy con Docker
1. Descarga la release `spotify3ds-proxy.tar` desde las Releases del proyecto.
2. Carga la imagen Docker:
   ```sh
   docker load -i spotify3ds-proxy.tar
   ```
3. Ejecuta el contenedor:
   ```sh
   docker run -d --name spotify3ds-proxy -p 8000:8000 spotify3ds-proxy
   ```
4. Obtén el ID/estado del contenedor:
   ```sh
   docker ps
   ```
5. Entra al contenedor para editar la configuración:
   ```sh
   docker exec -it spotify3ds-proxy bash
   apt update && apt install nano -y
   nano spotify_config.json
   ```
   Rellena `client_id` y `client_secret` (consigue estos valores desde el dashboard de Spotify) y guarda.
6. Desde el host/terminal del contenedor, la primera vez el servidor intentará abrir la URL de autorización. Si no se abre, la verás en la consola. Ábrela en un navegador. Tras autorizar, el servidor almacenará el `code`/`refresh_token` en `spotify_config.json`.

### 1.b) Servidor proxy directamente en Python

1. Clona el repositorio y entra en la carpeta del servidor:
   ```sh
   git clone https://github.com/davidabejon/spotify-3ds.git
   cd spotify-3ds/server
   ```

2. Crea y activa un entorno virtual (Windows):
   ```ps
   python -m venv .venv
   .venv\Scripts\activate
   ```

3. Instala dependencias:
   ```ps
   pip install -r requirements.txt
   ```

4. Edita `spotify_config.json` y añade `client_id` y `client_secret` obtenidos desde el Dashboard de Spotify.

5. Ejecuta el servidor con Uvicorn (desde la carpeta `server`):
   ```ps
   uvicorn server:app --host 0.0.0.0 --port 8000 --reload --reload-include spotify_config.json
   ```
   - Si el servidor imprime un mensaje advirtiendo de que client_id y client_secret no han sido rellenados abre el archivo `spotify_config.json`

6. Desde el terminal, el servidor intentará abrir la URL de autorización. Si no se abre, la verás en la consola. Ábrela en un navegador. Tras autorizar, el servidor almacenará el `code`/`refresh_token` en `spotify_config.json`.

### 2) Aplicación 3DS
- Lo más sencillo: descarga `Spotify3DS.cia` desde las [Releases](https://github.com/davidabejon/spotify-3ds/releases), cópialo a la tarjeta SD e instálalo con FBI u otro instalador de CIA
- Alternativamente, descarga `spotify-3ds.zip` desde las [Releases](https://github.com/davidabejon/spotify-3ds/releases): extráelo en la carpeta `/3ds` de la tarjeta SD y ejecútalo desde el Homebrew Launcher.

Al iniciar la aplicación por primera vez, te pedirá la dirección IP del servidor proxy; tras introducirla intentará conectarse automáticamente.

Para cambiar la IP más tarde, pulsa Y para volver a abrir el teclado en pantalla y escribe la nueva dirección IP.

#### Controles
 - A — Reproducir / Pausar
 - Y — Abrir entrada de IP (teclado en pantalla)
 - Cruceta derecha — Siguiente canción
 - Cruceta izquierda — Anterior canción
 - Cruceta arriba — Subir volumen
 - Cruceta abajo — Bajar volumen
 - Botón START — Salir de la aplicación

## Licencia y créditos
- Licencia: Apache License 2.0 — ver el archivo `LICENSE` en la raíz del repositorio.
- Proyecto original / autor: David Abejón.
- Librerías incluidas: stbi (stb_image) en `client/include/stb_image.h` y demás dependencias listadas en el Docker image / requirements.