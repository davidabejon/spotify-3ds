#include <stdio.h>
#include <string.h>
#include <3ds.h>
#include <stdlib.h>

#include "fetch.h"
#include "parse.h"

// Centers a string within a given width, returns the starting column
int center(const char* text, int width) {
    int len = strlen(text);
    if (len >= width) return 0;
    return (width - len) / 2;
}

int main(int argc, char** argv)
{
    // Initialize services
    gfxInitDefault();
    cfguInit();
    httpcInit(0);
    
    // Init console for text output
    consoleInit(GFX_TOP, NULL);
    
    // Main loop
    while (aptMainLoop())
    {
        hidScanInput();

        u32 kDown = hidKeysDown();
        if (kDown & KEY_START)
            break; // break in order to return to hbmenu

        // Fetch from server every 3 seconds
        static u32 lastTick = 0;
        u32 currentTick = osGetTime(); // milliseconds since boot
        if (currentTick - lastTick >= 3000) {
            lastTick = currentTick;

            char* json = fetch("http://192.168.1.200:8000/now-playing");
			if (json) {
				char* track = get("name", json);
				char* artist = get("artist", json);
				char* is_playing_str = get("is_playing", json);

				bool is_playing = false;
				if (is_playing_str) {
					is_playing = strcmp(is_playing_str, "true") == 0;
				}

				if (!track) track = strdup("Unknown");
				if (!artist) artist = strdup("Unknown");

				const int screen_width = 50;

				// Clear screen
				printf("\x1b[2J");

				// Status
				char line1[64];
				if (is_playing)
					snprintf(line1, sizeof(line1), "Now playing:");
				else
					snprintf(line1, sizeof(line1), "Playback paused:");

				int col1 = center(line1, screen_width);
				printf("\x1b[1;%dH%s", col1 + 1, line1);

				// Track
				int col_track = center(track, screen_width);
				printf("\x1b[2;%dH%s", col_track + 1, track);

				// Artist
				int col_artist = center(artist, screen_width);
				printf("\x1b[3;%dH%s", col_artist + 1, artist);


				free(track);
				free(artist);
				free(json);
				free(is_playing_str);
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