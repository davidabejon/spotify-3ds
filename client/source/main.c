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
static const int H_MARGIN = 3; // left/right horizontal margin in characters

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

// forward declaration for a function defined later
void printWithShadowCentered(int y, const char *text);

// Marquee state for long track titles
static int track_scroll_index = 0;
static u32 track_last_scroll_tick = 0;
static const int track_scroll_delay_ms = 200; // time between scroll steps

// Print a line with shadow; if text fits, center it, otherwise show a marquee within SCREEN_WIDTH
void printMarqueeLine(int y, const char *text)
{
    if (!text)
        return;
    int len = strlen(text);
    int fieldWidth = SCREEN_WIDTH - 2 * H_MARGIN;

    if (len <= fieldWidth)
    {
        printWithShadowCentered(y, text);
        return;
    }

    // Build visible window of width fieldWidth using scroll index and a small spacer
    int pad = 4;
    int loopLen = len + pad;
    char buf[64];
    if (fieldWidth >= (int)sizeof(buf))
        fieldWidth = (int)sizeof(buf) - 1;
    for (int i = 0; i < fieldWidth; i++)
    {
        int idx = (track_scroll_index + i) % loopLen;
        if (idx < len)
            buf[i] = text[idx];
        else
            buf[i] = ' ';
    }
    buf[fieldWidth] = '\0';

    // Print shadow (one row down, one column right) with left margin
    int mainCol = H_MARGIN + 1;
    int shadowCol = mainCol + 1;
    int shadowLen = (fieldWidth > 0) ? (fieldWidth - 1) : 0;
    if (shadowLen > 0)
    {
        char shadowBuf[64];
        if (shadowLen >= (int)sizeof(shadowBuf))
            shadowLen = (int)sizeof(shadowBuf) - 1;
        memcpy(shadowBuf, buf, shadowLen);
        shadowBuf[shadowLen] = '\0';
        printf("\x1b[%d;%dH\x1b[38;2;0;0;0m%s", y + 1, shadowCol, shadowBuf);
    }
    // Print main text
    printf("\x1b[%d;%dH\x1b[38;2;30;215;96m%s", y, mainCol, buf);
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
    // Set background to dark (near-black) and clear the console
    printf("\x1b[48;2;30;33;36m"); // Darker background (almost black)
    printf("\x1b[2J");

    // Draw visible frame margins: vertical lines at left/right margins and horizontal lines at top/bottom
    int leftCol = H_MARGIN;
    int rightCol = SCREEN_WIDTH - H_MARGIN + 1;
    // Keep the printable frame roughly in the previous area
    int topRow = 4;
    int bottomRow = 26;

    // Frame color (light gray)
    printf("\x1b[38;2;200;200;200m");

    int innerWidth = rightCol - leftCol - 1;

    // Decorative title to show in the top border
    const char *title = " Spotify-3DS ";
    int titleLen = (int)strlen(title);
    int titleStart = (innerWidth - titleLen) / 2;
    if (titleStart < 0)
        titleStart = 0;

    // Top border with centered title
    for (int c = leftCol; c <= rightCol; c++)
    {
        int pos = c - leftCol - 1; // 0-based inside
        if (c == leftCol)
        {
            printf("\x1b[%d;%dH+", topRow, c);
        }
        else if (c == rightCol)
        {
            printf("\x1b[%d;%dH+", topRow, c);
        }
        else
        {
            if (pos >= titleStart && pos < titleStart + titleLen)
            {
                char ch = title[pos - titleStart];
                printf("\x1b[%d;%dH%c", topRow, c, ch);
            }
            else
            {
                printf("\x1b[%d;%dH-", topRow, c);
            }
        }
    }

    // Bottom border with a small pattern
    const char *bottomStamp = "~ Enjoy the music ~";
    int stampLen = (int)strlen(bottomStamp);
    int stampStart = (innerWidth - stampLen) / 2;
    if (stampStart < 0)
        stampStart = 0;
    for (int c = leftCol; c <= rightCol; c++)
    {
        int pos = c - leftCol - 1;
        if (c == leftCol)
        {
            printf("\x1b[%d;%dH+", bottomRow, c);
        }
        else if (c == rightCol)
        {
            printf("\x1b[%d;%dH+", bottomRow, c);
        }
        else
        {
            if (pos >= stampStart && pos < stampStart + stampLen)
            {
                char ch = bottomStamp[pos - stampStart];
                printf("\x1b[%d;%dH%c", bottomRow, c, ch);
            }
            else
            {
                printf("\x1b[%d;%dH=", bottomRow, c);
            }
        }
    }

    // Vertical sides and small corner flourishes
    for (int r = topRow + 1; r < bottomRow; r++)
    {
        // left side
        printf("\x1b[%d;%dH|", r, leftCol);
        // right side
        printf("\x1b[%d;%dH|", r, rightCol);
    }

    // Small corner art inside the frame
    printf("\x1b[%d;%dH%c", topRow + 1, leftCol + 2, '/');
    printf("\x1b[%d;%dH%c", topRow + 2, leftCol + 1, '/');
    printf("\x1b[%d;%dH%c", topRow + 1, rightCol - 2, '\\');
    printf("\x1b[%d;%dH%c", topRow + 2, rightCol - 1, '\\');
    printf("\x1b[%d;%dH%c", bottomRow - 1, leftCol + 2, '\\');
    printf("\x1b[%d;%dH%c", bottomRow - 2, leftCol + 1, '\\');
    printf("\x1b[%d;%dH%c", bottomRow - 1, rightCol - 2, '/');
    printf("\x1b[%d;%dH%c", bottomRow - 2, rightCol - 1, '/');

    // Soft shadow along bottom and right borders (one row/col offset)
    printf("\x1b[38;2;100;100;100m");
    int shadowRow = bottomRow + 1;
    for (int c = leftCol + 1; c <= rightCol + 1; c++)
    {
        if (c == leftCol + 1)
        {
            // leftmost point of bottom shadow: backslash
            printf("\x1b[%d;%dH\\", shadowRow, c);
        }
        else
        {
            printf("\x1b[%d;%dH.", shadowRow, c);
        }
    }
    int shadowCol = rightCol + 1;
    for (int r = topRow + 1; r <= bottomRow + 1; r++)
    {
        if (r == topRow + 1)
        {
            // uppermost point of right shadow: backslash
            printf("\x1b[%d;%dH\\", r, shadowCol);
        }
        else
        {
            printf("\x1b[%d;%dH.", r, shadowCol);
        }
    }

    // Restore main text color (Spotify green)
    printf("\x1b[38;2;30;215;96m");
}

void printWithShadowCentered(int y, const char *text)
{
    int len = strlen(text);
    int effectiveWidth = SCREEN_WIDTH - 2 * H_MARGIN;
    int x = (effectiveWidth - len) / 2;
    if (x < 0)
        x = 0;

    int col = H_MARGIN + x + 1; // 1-based column

    // Black shadow: if the text fits within the effective width, draw full-length shadow;
    // otherwise draw the shadow one character shorter to avoid trailing overflow.
    int shadowLen = len;
    if (len > effectiveWidth)
    {
        shadowLen = (len > 0) ? (len - 1) : 0;
    }
    if (shadowLen > effectiveWidth)
        shadowLen = effectiveWidth;
    if (shadowLen > 0)
    {
        char shadowBuf[256];
        if (shadowLen >= (int)sizeof(shadowBuf))
            shadowLen = (int)sizeof(shadowBuf) - 1;
        memcpy(shadowBuf, text, shadowLen);
        shadowBuf[shadowLen] = '\0';
        printf("\x1b[%d;%dH\x1b[38;2;0;0;0m%s", y + 1, col + 1, shadowBuf);
    }

    // Main text
    printf("\x1b[%d;%dH\x1b[38;2;30;215;96m%s", y, col, text);
}

// Helper to print the volume as a 10-segment ASCII bar (each segment = 10%)
void printVolumeBar(int volume)
{
    int filled = volume / 10;
    if (filled < 0)
        filled = 0;
    if (filled > 10)
        filled = 10;

    // Print percent with shadow on the original line (row 21)
    char percent_line[32];
    snprintf(percent_line, sizeof(percent_line), "Volume: %3d%%", volume);
    printWithShadowCentered(20, percent_line);

    // Print 10 single-character segments (one per 10%) two lines below (row 22), centered.
    int effectiveWidth = SCREEN_WIDTH - 2 * H_MARGIN;
    int barLen = 10; // 10 single-char segments
    int x = (effectiveWidth - barLen) / 2;
    if (x < 0)
        x = 0;
    int col = H_MARGIN + x + 1; // 1-based column
    for (int i = 0; i < 10; i++)
    {
        int segCol = col + i;
        // Draw shadow one row down, one column right (black)
        printf("\x1b[%d;%dH\x1b[38;2;0;0;0m#", 23, segCol + 1);
        // Draw main segment on the intended row (22) with color indicating activity
        if (i < filled)
        {
            // Active segment: green '#'
            printf("\x1b[%d;%dH\x1b[38;2;30;215;96m#", 22, segCol);
        }
        else
        {
            // Inactive segment: gray '#'
            printf("\x1b[%d;%dH\x1b[38;2;120;120;120m#", 22, segCol);
        }
    }
    // Restore main text color after drawing
    printf("\x1b[38;2;30;215;96m");
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
            // If user requested play, immediately show play overlay until server confirms
            if (!is_playing)
                setTemporaryPlay(true);
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
                // update playback paused overlay
                if (is_playing_str)
                    is_playing = strcmp(is_playing_str, "true") == 0;
                setPlaybackPaused(!is_playing);
                if (is_playing)
                {
                    // Server reports playback is playing — clear temporary play overlay
                    setTemporaryPlay(false);
                }
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

                    // Status - centered with shadow
                    printWithShadowCentered(7, line1);

                    // Track - use marquee-aware printing
                    printMarqueeLine(10, track);

                    // Artist - centered with shadow
                    printWithShadowCentered(13, artist);

                    // Playing in device
                    char device_line[128];
                    snprintf(device_line, sizeof(device_line), "Playing on: %s", device_name);
                    printWithShadowCentered(17, device_line);

                    // Volume - use helper to render as 10-segment ASCII bar
                    if (volume_str && strcmp(volume_str, "N/A") == 0)
                    {
                        printWithShadowCentered(20, "Volume: N/A");
                    }
                    else
                    {
                        printVolumeBar(volume);
                    }
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

        // Update marquee scroll state for long track titles
        if (track)
        {
            int tlen = strlen(track);
            if (tlen > SCREEN_WIDTH)
            {
                if (currentTick - track_last_scroll_tick >= track_scroll_delay_ms)
                {
                    track_scroll_index = (track_scroll_index + 1) % (tlen + 4);
                    track_last_scroll_tick = currentTick;
                }
            }
            else
            {
                // reset when short
                track_scroll_index = 0;
            }

            // Always draw the track line (centered or marquee) each frame so animation updates
            printMarqueeLine(10, track);
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