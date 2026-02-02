#include <3ds/thread.h>
#include <3ds/synchronization.h>
#include <stdio.h>
#include <string.h>
#include <3ds.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <sys/types.h>

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

const int SCREEN_WIDTH = 40;

// Ensures directory exists
void ensureDirectory(const char *path)
{
    mkdir(path, 0777); // Safe on 3DS, does nothing if exists
}

// Center helper
int center(const char *text, int width)
{
    int len = strlen(text);
    if (len >= width)
        return 0;
    return (width - len) / 2;
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

// Make bottomConsole file-scope so clearScreen can use it
static PrintConsole bottomConsole;

// Clear screen
void clearScreen()
{
    // Set background to light gray (252), foreground to green (Spotify green: 46)
    printf("\x1b[48;2;30;33;36m");  // Darker background (almost black)
    printf("\x1b[38;2;30;215;96m"); // Text color
    printf("\x1b[2J");
}

void printWithShadowCentered(int y, const char *text)
{
    int len = strlen(text);
    int x = (SCREEN_WIDTH - len) / 2;

    if (x < 1)
        x = 1;

    // Black shadow, offset by 1
    printf("\x1b[%d;%dH\x1b[38;2;0;0;0m%s", y + 1, x + 1, text);

    // Main text
    printf("\x1b[%d;%dH\x1b[38;2;30;215;96m%s", y, x, text);
}

// Helper to build URLs
void build_url(char *buf, size_t buflen, const char *server_ip, const char *endpoint)
{
    snprintf(buf, buflen, "http://%s:8000/%s", server_ip, endpoint);
}

int main(int argc, char **argv)
{
    gfxInitDefault();
    cfguInit();
    httpcInit(0);
    Result ret = initNetwork();
    consoleInit(GFX_BOTTOM, &bottomConsole);
    consoleSelect(&bottomConsole);
    bool is_playing = false;
    char url[128];

    // Ensure directory exists
    ensureDirectory(CONFIG_DIR);

    char server_ip[60];

    // Load or ask for IP
    if (!loadIP(server_ip, sizeof(server_ip)))
    {
        char *input = askUser("Enter server IP address:");
        strncpy(server_ip, input, sizeof(server_ip) - 1);
        server_ip[sizeof(server_ip) - 1] = '\0';
        saveIP(server_ip);
    }

    // Initial connection message
    char connect_msg[80];
    snprintf(connect_msg, sizeof(connect_msg), "Connecting to %s...", server_ip);
    int col_connect = center(connect_msg, SCREEN_WIDTH);
    printf("\x1b[1;%dH%s\n", col_connect + 1, connect_msg);

    // Variables for now-playing info
    static u32 lastTick = 0;
    char *track = NULL;
    char *artist = NULL;
    char *device_name = NULL;
    char *volume_str = NULL;
    int volume = 0;
    char *is_playing_str = NULL;
    bool need_refresh = true;

    // Image data variables
    char *imageURL = NULL;
    u8 *imagePixels = NULL;
    int imageWidth = 0, imageHeight = 0;

    // Async fetch state
    FetchJob fetchJob;
    Thread fetchThread = NULL;
    bool fetchInProgress = false;

    while (aptMainLoop())
    {
        hidScanInput();
        u32 kDown = hidKeysDown();
        u32 currentTick = osGetTime();

        // Handle input every frame
        if (kDown & KEY_START)
            break;

        // Re-enter IP on pressing Y
        if (kDown & KEY_Y)
        {
            clearScreen();
            char *input = askUser("Enter new server IP address:");
            strncpy(server_ip, input, sizeof(server_ip) - 1);
            server_ip[sizeof(server_ip) - 1] = '\0';
            saveIP(server_ip);

            snprintf(connect_msg, sizeof(connect_msg), "Connecting to %s...", server_ip);
            col_connect = center(connect_msg, SCREEN_WIDTH);
            printf("\x1b[1;%dH%s\n", col_connect + 1, connect_msg);
            need_refresh = true; // force refresh after IP change
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

        // declare previous state variables for comparison
        static char prev_track[128] = "";
        static char prev_artist[128] = "";
        static char prev_device_name[128] = "";
        static int prev_volume = -1;
        static bool prev_is_playing = false;
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

        // If fetch is done, process result
        if (fetchInProgress && fetchJob.done)
        {
            threadJoin(fetchThread, U64_MAX);
            threadFree(fetchThread);
            fetchThread = NULL;
            fetchInProgress = false;

            char *json = fetchJob.json_result;
            if (json)
            {
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

                track = get("name", json);
                artist = get("artist", json);
                is_playing_str = get("is_playing", json);
                device_name = get("device", json);
                volume_str = get("volume_percent", json);
                imageURL = get("image_url", json);

                if (is_playing_str)
                    is_playing = strcmp(is_playing_str, "true") == 0;

                if (!track)
                    track = strdup("Unknown");
                if (!artist)
                    artist = strdup("Unknown");
                if (!device_name)
                    device_name = strdup("Unknown Device");
                if (!volume_str)
                    volume_str = strdup("N/A");
                else
                    volume = atoi(volume_str);

                // only clear the screen if the data is different from before
                if (strcmp(track, prev_track) != 0 || strcmp(artist, prev_artist) != 0 || strcmp(device_name, prev_device_name) != 0 || volume != prev_volume || is_playing != prev_is_playing)
                {
                    clearScreen();
                    // Save current state as previous
                    strncpy(prev_track, track, sizeof(prev_track) - 1);
                    prev_track[sizeof(prev_track) - 1] = '\0';
                    strncpy(prev_artist, artist, sizeof(prev_artist) - 1);
                    prev_artist[sizeof(prev_artist) - 1] = '\0';
                    strncpy(prev_device_name, device_name, sizeof(prev_device_name) - 1);
                    prev_device_name[sizeof(prev_device_name) - 1] = '\0';
                    prev_volume = volume;
                    prev_is_playing = is_playing;

                    // Status with spacing
                    char line1[64];
                    snprintf(line1, sizeof(line1),
                             is_playing ? "Now playing:" : "Playback paused:");

                    // First line on row 11
                    printWithShadowCentered(9, line1);

                    // Track - centered with shadow (2 rows below)
                    printWithShadowCentered(12, track);

                    // Artist - centered with shadow (2 rows below)
                    printWithShadowCentered(15, artist);

                    // Playing in device (2 rows below)
                    char device_line[128];
                    snprintf(device_line, sizeof(device_line), "Playing on: %s", device_name);
                    printWithShadowCentered(18, device_line);

                    // Volume (2 rows below)
                    char volume_line[64];
                    snprintf(volume_line, sizeof(volume_line), "Volume: %s%%", volume_str);
                    printWithShadowCentered(21, volume_line);
                }
                // Handle image download/display
                if (ret == 0 && imageURL && strlen(imageURL) > 0)
                {
                    // Free old image data
                    if (imagePixels)
                    {
                        stbi_image_free(imagePixels);
                        imagePixels = NULL;
                        imageWidth = 0;
                        imageHeight = 0;
                    }

                    // Download and decode image
                    u32 imageSize = 0;
                    u8 *imageData = downloadImage(imageURL, &imageSize);
                    if (imageData && imageSize > 0)
                    {
                        imagePixels = stbi_load_from_memory(imageData, imageSize,
                                                            &imageWidth, &imageHeight, NULL, STBI_rgb_alpha);
                        free(imageData);

                        if (!imagePixels)
                        {
                            printf("Failed to decode image\n");
                        }
                    }
                    else if (imageData)
                    {
                        free(imageData);
                    }
                }

                free(json);
            }
            else
            {
                const char *err_msg = "Error fetching data from server.";
                int col_err = center(err_msg, SCREEN_WIDTH);
                printf("\x1b[1;%dH%s\n", col_err + 1, err_msg);
            }
        }

        // Draw image if we have one
        if (imagePixels)
        {
            drawImageToScreen(imagePixels, imageWidth, imageHeight);
        }

        gspWaitForVBlank();
    }

    // Cleanup
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
    if (imagePixels)
        stbi_image_free(imagePixels);

    cleanupNetwork();
    httpcExit();
    cfguExit();
    gfxExit();
    return 0;
}