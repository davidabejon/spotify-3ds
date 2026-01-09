#include <stdio.h>
#include <string.h>
#include <3ds.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <sys/types.h>

#include "fetch.h"
#include "parse.h"

#define CONFIG_DIR "sdmc:/3ds/spotify-3ds"
#define CONFIG_PATH "sdmc:/3ds/spotify-3ds/ip.cfg"

const int SCREEN_WIDTH = 50;

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

void clearScreen()
{
    printf("\x1b[2J");
}

int main(int argc, char **argv)
{
    gfxInitDefault();
    cfguInit();
    httpcInit(0);
    consoleInit(GFX_TOP, NULL);
    bool is_playing = false;
    char url[128];

    // Ensure directory exists
    ensureDirectory(CONFIG_DIR);

    char server_ip[60];

    // Load or ask for IP
    if (!loadIP(server_ip, sizeof(server_ip)))
    {
        char *input = askUser("Enter server IP address:");
        strncpy(server_ip, input, sizeof(server_ip));
        saveIP(server_ip);
    }

    // Initial connection message
    char connect_msg[80];
    snprintf(connect_msg, sizeof(connect_msg), "Connecting to %s...", server_ip);
    int col_connect = center(connect_msg, SCREEN_WIDTH);
    printf("\x1b[1;%dH%s\n", col_connect + 1, connect_msg);

    // Main loop
    while (aptMainLoop())
    {
        hidScanInput();

        u32 kDown = hidKeysDown();
        if (kDown & KEY_START)
            break;

        // Re-enter IP on pressing Y
        if (kDown & KEY_Y)
        {
            clearScreen();
            char *input = askUser("Enter new server IP address:");
            strncpy(server_ip, input, sizeof(server_ip));
            saveIP(server_ip);

            snprintf(connect_msg, sizeof(connect_msg), "Connecting to %s...", server_ip);
            col_connect = center(connect_msg, SCREEN_WIDTH);
            printf("\x1b[1;%dH%s\n", col_connect + 1, connect_msg);
        }

        if (kDown & KEY_A)
        {
            if (is_playing)
                snprintf(url, sizeof(url), "http://%s:8000/pause", server_ip);
            else
                snprintf(url, sizeof(url), "http://%s:8000/play", server_ip);
            fetch(url);
        }
        if (kDown & KEY_DRIGHT)
        {
            snprintf(url, sizeof(url), "http://%s:8000/next", server_ip);
            fetch(url);
        }
        if (kDown & KEY_DLEFT)
        {
            snprintf(url, sizeof(url), "http://%s:8000/previous", server_ip);
            fetch(url);
        }

        // Fetch every 3 seconds
        static u32 lastTick = 0;
        u32 currentTick = osGetTime();

        if (currentTick - lastTick >= 3000)
        {
            lastTick = currentTick;

            snprintf(url, sizeof(url), "http://%s:8000/now-playing", server_ip);

            char *json = fetch(url);
            if (json)
            {
                char *track = get("name", json);
                char *artist = get("artist", json);
                char *is_playing_str = get("is_playing", json);

                if (is_playing_str)
                    is_playing = strcmp(is_playing_str, "true") == 0;

                if (!track)
                    track = strdup("Unknown");
                if (!artist)
                    artist = strdup("Unknown");

                clearScreen();

                // Status with spacing
                char line1[64];
                snprintf(line1, sizeof(line1),
                         is_playing ? "Now playing:" : "Playback paused:");
                int col1 = center(line1, SCREEN_WIDTH);
                printf("\x1b[1;1H\n"); // Space

                // Status
                printf("\x1b[2;%dH%s\n", col1 + 1, line1);

                printf("\x1b[3;1H\n"); // Space

                // Track
                int col_track = center(track, SCREEN_WIDTH);
                printf("\x1b[4;%dH%s\n", col_track + 1, track);

                printf("\x1b[5;1H\n"); // Space

                // Artist
                int col_artist = center(artist, SCREEN_WIDTH);
                printf("\x1b[6;%dH%s", col_artist + 1, artist);

                free(track);
                free(artist);
                free(json);
                free(is_playing_str);
            }
            else
            {
                const char *err_msg = "Error fetching data from server.";
                int col_err = center(err_msg, SCREEN_WIDTH);
                printf("\x1b[1;%dH%s\n", col_err + 1, err_msg);
            }
        }

        gfxFlushBuffers();
        gfxSwapBuffers();
        gspWaitForVBlank();
    }

    httpcExit();
    cfguExit();
    gfxExit();
    return 0;
}
