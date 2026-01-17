#include <3ds/thread.h>
#include <3ds/synchronization.h>
#include <stdio.h>
#include <string.h>
#include <3ds.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <citro2d.h>

#include "fetch.h"
#include "parse.h"
#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"
#include "image_display.h"

// Struct for async fetch result
typedef struct
{
    char url[128];
    char *json_result;
    volatile bool done;
    LightEvent event;
} FetchJob;

// Worker thread function
void fetch_worker(void *arg)
{
    FetchJob *job = (FetchJob *)arg;
    job->json_result = fetch(job->url);
    job->done = true;
    LightEvent_Signal(&job->event);
}

#define CONFIG_DIR "sdmc:/3ds/spotify-3ds"
#define CONFIG_PATH "sdmc:/3ds/spotify-3ds/ip.cfg"

// Ensures directory exists
void ensureDirectory(const char *path)
{
    mkdir(path, 0777); // Safe on 3DS, does nothing if exists
}

// Persistent IP load
bool loadIP(char *buffer, size_t size)
{
    FILE *f = fopen(CONFIG_PATH, "r");
    if (!f)
        return false;

    if (!fgets(buffer, size, f))
    {
        fclose(f);
        return false;
    }

    buffer[strcspn(buffer, "\n")] = 0; // strip newline
    fclose(f);
    return true;
}

// Persistent IP save
void saveIP(const char *ip)
{
    FILE *f = fopen(CONFIG_PATH, "w");
    if (!f)
        return;
    fprintf(f, "%s\n", ip);
    fclose(f);
}

// Keyboard input helper
char *askUser(const char *prompt)
{
    static SwkbdState swkbd;
    static char inputbuf[60];

    swkbdInit(&swkbd, SWKBD_TYPE_WESTERN, 1, -1);
    swkbdSetValidation(&swkbd, SWKBD_NOTEMPTY_NOTBLANK,
                       SWKBD_FILTER_DIGITS | SWKBD_FILTER_AT |
                           SWKBD_FILTER_PERCENT | SWKBD_FILTER_BACKSLASH |
                           SWKBD_FILTER_PROFANITY,
                       -1);
    swkbdSetFeatures(&swkbd, SWKBD_MULTILINE);
    swkbdSetHintText(&swkbd, prompt);
    swkbdInputText(&swkbd, inputbuf, sizeof(inputbuf));

    return inputbuf;
}

// Helper to build URLs
void build_url(char *buf, size_t buflen, const char *server_ip, const char *endpoint)
{
    snprintf(buf, buflen, "http://%s:8000/%s", server_ip, endpoint);
}

int main(int argc, char **argv)
{
    gfxInitDefault();
    C3D_Init(C3D_DEFAULT_CMDBUF_SIZE);
    C2D_Init(C2D_DEFAULT_MAX_OBJECTS);
    C2D_Prepare();
    cfguInit();
    httpcInit(0);
    initNetwork();

    // Create screen targets for BOTH top and bottom
    C3D_RenderTarget *top = C2D_CreateScreenTarget(GFX_TOP, GFX_LEFT);
    C3D_RenderTarget *bottom = C2D_CreateScreenTarget(GFX_BOTTOM, GFX_LEFT);

    // Text buffers
    C2D_TextBuf staticBuf = C2D_TextBufNew(4096);
    C2D_TextBuf dynamicBuf = C2D_TextBufNew(4096);
    C2D_Text text_status, text_track, text_artist, text_device, text_volume;

    // Image variables
    u8 *imagePixels = NULL;
    int imageWidth = 0;
    int imageHeight = 0;
    bool imageLoaded = false;
    bool needImageReload = false;
    char *currentImageURL = NULL;

    bool is_playing = false;
    char url[128];
    ensureDirectory(CONFIG_DIR);
    char server_ip[60];
    if (!loadIP(server_ip, sizeof(server_ip)))
    {
        char *input = askUser("Enter server IP address:");
        strncpy(server_ip, input, sizeof(server_ip) - 1);
        server_ip[sizeof(server_ip) - 1] = '\0';
        saveIP(server_ip);
    }

    // Variables for now-playing info - with mutex for thread safety
    static u32 lastTick = 0;
    char *track = strdup("Unknown");
    char *artist = strdup("Unknown");
    char *device_name = strdup("Unknown Device");
    char *volume_str = strdup("N/A");
    int volume = 0;
    char *is_playing_str = strdup("false");
    bool need_refresh = true;
    FetchJob fetchJob;
    Thread fetchThread = NULL;
    bool fetchInProgress = false;

    // Mutex for thread-safe updates
    LightLock dataLock;
    LightLock_Init(&dataLock);

    while (aptMainLoop())
    {
        hidScanInput();
        u32 kDown = hidKeysDown();
        u32 currentTick = osGetTime();
        if (kDown & KEY_START)
            break;

        if (kDown & KEY_Y)
        {
            char *input = askUser("Enter new server IP address:");
            strncpy(server_ip, input, sizeof(server_ip) - 1);
            server_ip[sizeof(server_ip) - 1] = '\0';
            saveIP(server_ip);
            need_refresh = true;
        }
        if (kDown & KEY_A)
        {
            if (is_playing)
                build_url(url, sizeof(url), server_ip, "pause");
            else
                build_url(url, sizeof(url), server_ip, "play");
            fetch(url);
            need_refresh = true;
        }
        if (kDown & KEY_DRIGHT)
        {
            build_url(url, sizeof(url), server_ip, "next");
            fetch(url);
            need_refresh = true;
        }
        if (kDown & KEY_DLEFT)
        {
            build_url(url, sizeof(url), server_ip, "previous");
            fetch(url);
            need_refresh = true;
        }
        if (kDown & KEY_DUP || kDown & KEY_DDOWN)
        {
            build_url(url, sizeof(url), server_ip, "volume");
            char params[32];
            if (kDown & KEY_DUP)
            {
                if (volume <= 90)
                    volume += 10;
                else
                    volume = 100;
            }
            if (kDown & KEY_DDOWN)
            {
                if (volume >= 10)
                    volume -= 10;
                else
                    volume = 0;
            }
            if (volume > 0 || volume < 100)
            {
                snprintf(params, sizeof(params), "volume_percent=%d", volume);
                fetch_with_params(url, params);
                need_refresh = true;
            }
        }

        // Start async fetch if needed and not already in progress
        if ((need_refresh || (currentTick - lastTick >= 5000)) && !fetchInProgress)
        {
            lastTick = currentTick;
            need_refresh = false;
            build_url(fetchJob.url, sizeof(fetchJob.url), server_ip, "now-playing");
            fetchJob.json_result = NULL;
            fetchJob.done = false;
            LightEvent_Init(&fetchJob.event, RESET_ONESHOT);
            fetchThread = threadCreate(fetch_worker, &fetchJob, 8 * 1024, 0x18, -2, false);
            fetchInProgress = true;
        }

        if (fetchInProgress && fetchJob.done)
        {
            threadJoin(fetchThread, U64_MAX);
            threadFree(fetchThread);
            fetchThread = NULL;
            fetchInProgress = false;
            char *json = fetchJob.json_result;
            if (json)
            {
                // Parse JSON first
                char *new_track = get("name", json);
                char *new_artist = get("artist", json);
                char *new_is_playing_str = get("is_playing", json);
                char *new_device_name = get("device", json);
                char *new_volume_str = get("volume_percent", json);

                // Lock before updating shared data
                LightLock_Lock(&dataLock);

                // Free old strings
                if (track)
                    free(track);
                if (artist)
                    free(artist);
                if (is_playing_str)
                    free(is_playing_str);
                if (device_name)
                    free(device_name);
                if (volume_str)
                    free(volume_str);

                // Assign new values
                track = new_track ? strdup(new_track) : strdup("Unknown");
                artist = new_artist ? strdup(new_artist) : strdup("Unknown");
                is_playing_str = new_is_playing_str ? strdup(new_is_playing_str) : strdup("false");
                device_name = new_device_name ? strdup(new_device_name) : strdup("Unknown Device");
                volume_str = new_volume_str ? strdup(new_volume_str) : strdup("N/A");

                is_playing = strcmp(is_playing_str, "true") == 0;
                if (volume_str && strcmp(volume_str, "N/A") != 0)
                    volume = atoi(volume_str);

                // Free temporary strings from get()
                if (new_track)
                    free(new_track);
                if (new_artist)
                    free(new_artist);
                if (new_is_playing_str)
                    free(new_is_playing_str);
                if (new_device_name)
                    free(new_device_name);
                if (new_volume_str)
                    free(new_volume_str);

                // Handle image URL
                char *imageURL = get("image_url", json);
                if (imageURL)
                {
                    // Check if image URL changed
                    if (!currentImageURL || strcmp(imageURL, currentImageURL) != 0)
                    {
                        // Free old image
                        if (imagePixels)
                        {
                            stbi_image_free(imagePixels);
                            imagePixels = NULL;
                            imageLoaded = false;
                        }
                        if (currentImageURL)
                        {
                            free(currentImageURL);
                        }

                        currentImageURL = strdup(imageURL);
                        needImageReload = true;
                    }
                    free(imageURL);
                }

                LightLock_Unlock(&dataLock);
                free(json);
            }
        }

        // Load new image if needed
        if (needImageReload && currentImageURL)
        {
            u32 imageSize = 0;
            u8 *imageData = downloadImage(currentImageURL, &imageSize);

            if (imageData && imageSize > 0)
            {
                int width, height, channels;
                u8 *pixels = stbi_load_from_memory(imageData, imageSize, &width, &height, &channels, 4);
                free(imageData);

                if (pixels)
                {
                    if (imagePixels)
                    {
                        stbi_image_free(imagePixels);
                    }

                    imagePixels = pixels;
                    imageWidth = width;
                    imageHeight = height;
                    imageLoaded = true;
                }
            }
            needImageReload = false;
        }

        // Render with citro2d
        C3D_FrameBegin(C3D_FRAME_SYNCDRAW);

        // Render BOTTOM screen (text interface)
        C2D_TargetClear(bottom, C2D_Color32(0x20, 0x20, 0x20, 0xFF));
        C2D_SceneBegin(bottom);

        // Lock while reading data for rendering
        LightLock_Lock(&dataLock);

        // Prepare text with current data
        char status[64];
        snprintf(status, sizeof(status), "%s", is_playing ? "Now playing:" : "Playback paused:");
        char device_line[128];
        snprintf(device_line, sizeof(device_line), "Playing on: %s", device_name);
        char volume_line[64];
        snprintf(volume_line, sizeof(volume_line), "Volume: %s%%", volume_str);

        LightLock_Unlock(&dataLock);

        // Parse and optimize text
        C2D_TextBufClear(staticBuf);
        C2D_TextParse(&text_status, staticBuf, status);
        C2D_TextParse(&text_track, staticBuf, track);
        C2D_TextParse(&text_artist, staticBuf, artist);
        C2D_TextParse(&text_device, staticBuf, device_line);
        C2D_TextParse(&text_volume, staticBuf, volume_line);
        C2D_TextOptimize(&text_status);
        C2D_TextOptimize(&text_track);
        C2D_TextOptimize(&text_artist);
        C2D_TextOptimize(&text_device);
        C2D_TextOptimize(&text_volume);

        // Draw text on bottom screen
        float y = 30.0f;
        C2D_DrawText(&text_status, C2D_AtBaseline | C2D_WithColor, 16.0f, y, 0.5f, 0.7f, 0.7f, C2D_Color32(0xFF, 0xFF, 0xFF, 0xFF));
        y += 28.0f;
        C2D_DrawText(&text_track, C2D_AtBaseline | C2D_WithColor, 16.0f, y, 0.5f, 0.9f, 0.9f, C2D_Color32(0xFF, 0xFF, 0xFF, 0xFF));
        y += 28.0f;
        C2D_DrawText(&text_artist, C2D_AtBaseline | C2D_WithColor, 16.0f, y, 0.5f, 0.7f, 0.7f, C2D_Color32(0xFF, 0xFF, 0xFF, 0xFF));
        y += 28.0f;
        C2D_DrawText(&text_device, C2D_AtBaseline | C2D_WithColor, 16.0f, y, 0.5f, 0.6f, 0.6f, C2D_Color32(0xFF, 0xFF, 0xFF, 0xFF));
        y += 28.0f;
        C2D_DrawText(&text_volume, C2D_AtBaseline | C2D_WithColor, 16.0f, y, 0.5f, 0.6f, 0.6f, C2D_Color32(0xFF, 0xFF, 0xFF, 0xFF));

        C3D_FrameEnd(0);

        // Draw image on TOP screen using your existing function
        // We do this AFTER the Citro2D frame because drawImageToScreen uses direct framebuffer access
        if (imageLoaded && imagePixels)
        {
            drawImageToScreen(imagePixels, imageWidth, imageHeight);
        }
        else
        {
            // Just clear the top screen with background color if no image
            C3D_FrameBegin(C3D_FRAME_SYNCDRAW);
            C2D_TargetClear(top, C2D_Color32(30, 215, 96, 0xFF));
            C2D_SceneBegin(top);
            C3D_FrameEnd(0);
        }

        gspWaitForVBlank();
    }

    // Cleanup
    LightLock_Lock(&dataLock);
    if (track)
        free(track);
    if (artist)
        free(artist);
    if (is_playing_str)
        free(is_playing_str);
    if (device_name)
        free(device_name);
    if (volume_str)
        free(volume_str);
    LightLock_Unlock(&dataLock);

    if (currentImageURL)
        free(currentImageURL);
    if (imagePixels)
        stbi_image_free(imagePixels);

    C2D_TextBufDelete(staticBuf);
    C2D_TextBufDelete(dynamicBuf);
    C2D_Fini();
    C3D_Fini();
    cleanupNetwork();
    httpcExit();
    cfguExit();
    gfxExit();
    return 0;
}