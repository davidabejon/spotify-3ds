#ifndef IMAGE_DISPLAY_H
#define IMAGE_DISPLAY_H

#include <3ds.h>

/**
 * @brief Download an image from a URL
 * @param url The URL to download from (HTTP or HTTPS)
 * @param size Output parameter for the size of downloaded data
 * @return Pointer to downloaded image data (must be freed by caller), or NULL on failure
 */
u8* downloadImage(const char* url, u32* size);

/**
 * @brief Display an image on the top screen
 * @param pixels RGBA pixel data (from stb_image or similar)
 * @param width Width of the image in pixels
 * @param height Height of the image in pixels
 * @note The image will be automatically scaled to fit and centered on screen
 */
void drawImageToScreen(u8* pixels, int width, int height);

/**
 * @brief Initialize network services required for downloading images
 * @return 0 on success, error code on failure
 */
Result initNetwork(void);

/**
 * @brief Cleanup network services
 */
void cleanupNetwork(void);

#endif // IMAGE_DISPLAY_H