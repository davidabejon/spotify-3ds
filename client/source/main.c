#include <stdio.h>
#include <string.h>
#include <3ds.h>
#include <stdlib.h>

#include "fetch.h"
#include "parse.h"

const int SCREEN_WIDTH = 50;

// Centers a string within a given width, returns the starting column
int center(const char *text, int width)
{
    int len = strlen(text);
    if (len >= width)
        return 0;
    return (width - len) / 2;
}

char *askUser(const char *prompt)
{
    static SwkbdState swkbd;
    static char inputbuf[60];

    swkbdInit(&swkbd, SWKBD_TYPE_WESTERN, 1, -1);
    swkbdSetValidation(&swkbd, SWKBD_NOTEMPTY_NOTBLANK, SWKBD_FILTER_DIGITS | SWKBD_FILTER_AT | SWKBD_FILTER_PERCENT | SWKBD_FILTER_BACKSLASH | SWKBD_FILTER_PROFANITY, -1);
    swkbdSetFeatures(&swkbd, SWKBD_MULTILINE);
    swkbdSetHintText(&swkbd, prompt);
    swkbdInputText(&swkbd, inputbuf, sizeof(inputbuf));

    return inputbuf;
}

void clearScreen()
{
    printf("\x1b[2J"); // ANSI escape code to clear screen
}

int main(int argc, char **argv)
{
    // Initialize services
    gfxInitDefault();
    cfguInit();
    httpcInit(0);

    // Init console for text output
    consoleInit(GFX_TOP, NULL);

    char *server_ip = askUser("Enter server IP address:");
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
            break; // break in order to return to hbmenu
        if (kDown & KEY_A)
        {
            clearScreen();
            // ask for server IP again
            server_ip = askUser("Enter server IP address:");
            snprintf(connect_msg, sizeof(connect_msg), "Connecting to %s...", server_ip);
            col_connect = center(connect_msg, SCREEN_WIDTH);
            printf("\x1b[1;%dH%s\n", col_connect + 1, connect_msg);
        }

        // Fetch from server every 3 seconds
        static u32 lastTick = 0;
        u32 currentTick = osGetTime(); // milliseconds since boot
        if (currentTick - lastTick >= 3000)
        {
            lastTick = currentTick;

            char url[128];
            snprintf(url, sizeof(url), "http://%s:8000/now-playing", server_ip);
            char *json = fetch(url);
            if (json)
            {
                char *track = get("name", json);
                char *artist = get("artist", json);
                char *is_playing_str = get("is_playing", json);

                bool is_playing = false;
                if (is_playing_str)
                {
                    is_playing = strcmp(is_playing_str, "true") == 0;
                }

                if (!track)
                    track = strdup("Unknown");
                if (!artist)
                    artist = strdup("Unknown");

                clearScreen();

                // Status
                char line1[64];
                if (is_playing)
                    snprintf(line1, sizeof(line1), "Now playing:");
                else
                    snprintf(line1, sizeof(line1), "Playback paused:");

                int col1 = center(line1, SCREEN_WIDTH);
                printf("\x1b[1;%dH%s", col1 + 1, line1);

                // Track
                int col_track = center(track, SCREEN_WIDTH);
                printf("\x1b[2;%dH%s", col_track + 1, track);

                // Artist
                int col_artist = center(artist, SCREEN_WIDTH);
                printf("\x1b[3;%dH%s", col_artist + 1, artist);

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

        // Flush and swap framebuffers
        gfxFlushBuffers();
        gfxSwapBuffers();
        gspWaitForVBlank();
    }

    // Exit services
    httpcExit();
    cfguExit();
    gfxExit();
    return 0;
}