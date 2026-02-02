from fastapi import FastAPI, Request
from fastapi.responses import HTMLResponse, RedirectResponse, JSONResponse
import json, os, requests, base64
from pykakasi import kakasi
from unidecode import unidecode

# Initialize pykakasi converter (Kanji/Hiragana/Katakana -> Latin)
_kakasi_conv = None
try:
    _k = kakasi()
    _k.setMode("J", "a")
    _k.setMode("H", "a")
    _k.setMode("K", "a")
    _k.setMode("r", "Hepburn")
    _k.setMode("s", True)
    _kakasi_conv = _k.getConverter()
except Exception:
    _kakasi_conv = None

def _contains_non_latin(s: str) -> bool:
    if not s:
        return False
    for ch in s:
        if ord(ch) > 127:
            return True
    return False


def _contains_hangul(s: str) -> bool:
    if not s:
        return False
    for ch in s:
        cp = ord(ch)
        # Hangul Syllables range
        if 0xAC00 <= cp <= 0xD7AF:
            return True
    return False

app = FastAPI()

spotify_config_path = "spotify_config.json"
redirect_uri = "http://localhost:8000/callback"
required_scopes = "user-read-playback-state user-modify-playback-state"

# ----------------------------
# Helper for obtaining access_token
# ----------------------------
def get_access_token():
    """Returns a valid access token using refresh_token or code."""
    if not os.path.exists(spotify_config_path):
        return None, "No spotify_config.json"

    with open(spotify_config_path, "r") as f:
        config = json.load(f)

    client_id = config.get("client_id")
    client_secret = config.get("client_secret")

    if not client_id or not client_secret:
        return None, "Missing client_id or client_secret"

    headers = {"Authorization": f"Basic {base64.b64encode(f'{client_id}:{client_secret}'.encode()).decode()}"}

    # Use refresh_token if it exists
    if "refresh_token" in config:
        data = {"grant_type": "refresh_token", "refresh_token": config["refresh_token"]}
        resp = requests.post("https://accounts.spotify.com/api/token", data=data, headers=headers)
        if resp.status_code != 200:
            return None, resp.json()
        return resp.json(), None

    # If no refresh_token, exchange code (can only be done once)
    if "code" not in config:
        return None, "No code available"

    data = {"grant_type": "authorization_code", "code": config["code"], "redirect_uri": redirect_uri}
    resp = requests.post("https://accounts.spotify.com/api/token", data=data, headers=headers)
    if resp.status_code != 200:
        return None, resp.json()

    token_data = resp.json()
    # Save refresh_token and remove code
    config["refresh_token"] = token_data.get("refresh_token")
    config.pop("code", None)
    with open(spotify_config_path, "w") as f:
        json.dump(config, f, indent=2)

    return token_data, None

# ----------------------------
# Serve HTML
# ----------------------------
@app.get("/", response_class=HTMLResponse)
async def serve_html():
    with open("templates/index.html", "r", encoding="utf-8") as f:
        html_content = f.read()
    return HTMLResponse(content=html_content)

# ----------------------------
# Spotify callback
# ----------------------------
@app.get("/callback")
async def callback(request: Request):
    code = request.query_params.get("code")
    if not code:
        return JSONResponse({"error": "No code received"}, status_code=400)

    spotify_config = {"code": code}
    if os.path.exists(spotify_config_path):
        with open(spotify_config_path, "r") as f:
            old_data = f.read()
            if old_data:
                old_data = json.loads(old_data)
                spotify_config.update(old_data)

    with open(spotify_config_path, "w") as f:
        json.dump(spotify_config, f, indent=2)

    return HTMLResponse("<h2>Code received and stored successfully!</h2>")

# ----------------------------
# Save client_id and client_secret and redirect to Spotify
# ----------------------------
@app.get("/authorize")
async def authorize(client_id: str, client_secret: str):
    spotify_config = {}
    if os.path.exists(spotify_config_path):
        with open(spotify_config_path, "r") as f:
            old_data = f.read()
            if old_data:
                spotify_config = json.loads(old_data)

    spotify_config["client_id"] = client_id
    spotify_config["client_secret"] = client_secret

    with open(spotify_config_path, "w") as f:
        json.dump(spotify_config, f, indent=2)

    # Redirect to Spotify
    auth_url = (
        "https://accounts.spotify.com/authorize"
        f"?client_id={client_id}"
        "&response_type=code"
        f"&redirect_uri={redirect_uri}"
        f"&scope={required_scopes}"
    )
    return RedirectResponse(auth_url)

# ----------------------------
# Unified endpoint for now-playing and player state
# ----------------------------
@app.get("/now-playing")
def now_playing_and_state():
    token_data, error = get_access_token()
    if error:
        return JSONResponse({"error": error}, status_code=400)

    access_token = token_data["access_token"]
    headers = {"Authorization": f"Bearer {access_token}"}

    # Get currently playing track
    resp_track = requests.get("https://api.spotify.com/v1/me/player/currently-playing", headers=headers)
    # Get player state
    resp_state = requests.get("https://api.spotify.com/v1/me/player", headers=headers)

    result = {}

    # Track info
    if resp_track.status_code == 204:
        result["track"] = {"status": "no track playing"}
    elif resp_track.status_code != 200:
        result["track"] = {"error": resp_track.json()}
    else:
        data = resp_track.json()
        name = data["item"]["name"]
        artist_name = data["item"]["artists"][0]["name"]

        # Romanize if needed (replace with romanized text for client simplicity)
        if _contains_non_latin(name):
            try:
                if _contains_hangul(name):
                    # Use Unidecode for Hangul/Korean
                    name = unidecode(name)
                elif _kakasi_conv:
                    name = _kakasi_conv.do(name)
                else:
                    name = unidecode(name)
            except Exception:
                pass

        if _contains_non_latin(artist_name):
            try:
                if _contains_hangul(artist_name):
                    artist_name = unidecode(artist_name)
                elif _kakasi_conv:
                    artist_name = _kakasi_conv.do(artist_name)
                else:
                    artist_name = unidecode(artist_name)
            except Exception:
                pass

        result["track"] = {
            "name": name,
            "artist": artist_name,
            "album": data["item"]["album"]["name"],
            "is_playing": data["is_playing"]
        }

    # Player state
    if resp_state.status_code == 204:
        result["player_state"] = {"status": "no active device"}
    elif resp_state.status_code != 200:
        result["player_state"] = {"error": resp_state.json()}
    else:
        data = resp_state.json()
        result["player_state"] = {
            "device": data["device"]["name"],
            "volume_percent": data["device"]["volume_percent"],
        }
        result["image_url"] = data["item"]["album"]["images"][0]["url"]

    return JSONResponse(result)

# ----------------------------
# Playback control
# ----------------------------
def spotify_put(endpoint, access_token, device_id=None, params=None):
    url = f"https://api.spotify.com/v1/me/player/{endpoint}"
    if device_id:
        url += f"?device_id={device_id}"
    headers = {"Authorization": f"Bearer {access_token}"}
    return requests.put(url, headers=headers, params=params)

def spotify_post(endpoint, access_token, device_id=None):
    url = f"https://api.spotify.com/v1/me/player/{endpoint}"
    if device_id:
        url += f"?device_id={device_id}"
    headers = {"Authorization": f"Bearer {access_token}"}
    return requests.post(url, headers=headers)

@app.get("/pause")
def pause(device_id: str = None):
    token_data, error = get_access_token()
    if error:
        return JSONResponse({"error": error}, status_code=400)
    resp = spotify_put("pause", token_data["access_token"], device_id)
    return JSONResponse({"status": resp.status_code})

@app.get("/play")
def play(device_id: str = None):
    token_data, error = get_access_token()
    if error:
        return JSONResponse({"error": error}, status_code=400)
    resp = spotify_put("play", token_data["access_token"], device_id)
    return JSONResponse({"status": resp.status_code})

@app.get("/next")
def next_track(device_id: str = None):
    token_data, error = get_access_token()
    if error:
        return JSONResponse({"error": error}, status_code=400)
    resp = spotify_post("next", token_data["access_token"], device_id)
    return JSONResponse({"status": resp.status_code})

@app.get("/previous")
def previous_track(device_id: str = None):
    token_data, error = get_access_token()
    if error:
        return JSONResponse({"error": error}, status_code=400)
    resp = spotify_post("previous", token_data["access_token"], device_id)
    return JSONResponse({"status": resp.status_code})

@app.get("/volume")
def volume_up(device_id: str = None, volume_percent: int = 10):
    token_data, error = get_access_token()
    if error:
        return JSONResponse({"error": error}, status_code=400)
    resp = spotify_put("volume", token_data["access_token"], device_id, params={"volume_percent": volume_percent})
    return JSONResponse({"status": resp.status_code})