#include "image_display.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <malloc.h>

#define SOC_ALIGN 0x1000
#define SOC_BUFFERSIZE 0x100000

static u32 *SOC_buffer = NULL;
static bool network_initialized = false;

Result initNetwork(void)
{
    if (network_initialized)
    {
        return 0;
    }

    // Allocate buffer for SOC service
    SOC_buffer = (u32 *)memalign(SOC_ALIGN, SOC_BUFFERSIZE);
    if (SOC_buffer == NULL)
    {
        return -1;
    }

    // Initialize SOC service
    Result ret = socInit(SOC_buffer, SOC_BUFFERSIZE);
    if (ret != 0)
    {
        free(SOC_buffer);
        SOC_buffer = NULL;
        return ret;
    }

    // Initialize HTTP service
    ret = httpcInit(0);
    if (ret != 0)
    {
        socExit();
        free(SOC_buffer);
        SOC_buffer = NULL;
        return ret;
    }

    network_initialized = true;
    return 0;
}

void cleanupNetwork(void)
{
    if (!network_initialized)
    {
        return;
    }

    httpcExit();

    if (SOC_buffer != NULL)
    {
        socExit();
        free(SOC_buffer);
        SOC_buffer = NULL;
    }

    network_initialized = false;
}

u8 *downloadImage(const char *url, u32 *size)
{
    if (!network_initialized)
    {
        printf("Network not initialized!\n");
        return NULL;
    }

    Result ret = 0;
    httpcContext context;
    u32 statuscode = 0;
    u32 contentsize = 0;
    u8 *buffer = NULL;

    ret = httpcOpenContext(&context, HTTPC_METHOD_GET, url, 0);
    if (ret != 0)
    {
        printf("httpcOpenContext failed: 0x%lx\n", ret);
        return NULL;
    }

    // Set SSL options
    ret = httpcSetSSLOpt(&context, SSLCOPT_DisableVerify);
    if (ret != 0)
    {
        printf("httpcSetSSLOpt failed: 0x%lx\n", ret);
        httpcCloseContext(&context);
        return NULL;
    }

    // Add user agent
    ret = httpcAddRequestHeaderField(&context, "User-Agent", "Mozilla/5.0 (Nintendo 3DS)");

    // Set keep alive to false
    ret = httpcSetKeepAlive(&context, HTTPC_KEEPALIVE_DISABLED);

    ret = httpcBeginRequest(&context);
    if (ret != 0)
    {
        printf("httpcBeginRequest failed: 0x%lx\n", ret);
        httpcCloseContext(&context);
        return NULL;
    }

    ret = httpcGetResponseStatusCode(&context, &statuscode);
    if (ret != 0 || statuscode != 200)
    {
        printf("Status code: %lu\n", statuscode);
        httpcCloseContext(&context);
        return NULL;
    }

    ret = httpcGetDownloadSizeState(&context, NULL, &contentsize);
    if (ret != 0)
    {
        printf("httpcGetDownloadSizeState failed: 0x%lx\n", ret);
        httpcCloseContext(&context);
        return NULL;
    }

    // Add size validation check
    if (contentsize == 0 || contentsize > 10 * 1024 * 1024)
    { // Limit to 10MB
        printf("Invalid content size: %lu\n", contentsize);
        httpcCloseContext(&context);
        return NULL;
    }

    buffer = (u8 *)malloc(contentsize);
    if (buffer == NULL)
    {
        printf("Memory allocation failed\n");
        httpcCloseContext(&context);
        return NULL;
    }

    ret = httpcDownloadData(&context, buffer, contentsize, NULL);
    if (ret != 0)
    {
        printf("httpcDownloadData failed: 0x%lx\n", ret);
        free(buffer);
        httpcCloseContext(&context);
        return NULL;
    }

    *size = contentsize;
    httpcCloseContext(&context);
    return buffer;
}

void drawImageToScreen(u8 *pixels, int width, int height)
{
    if (!pixels || width <= 0 || height <= 0)
    {
        return; // Invalid parameters
    }

    // Get framebuffer dimensions
    u16 fbWidth, fbHeight;
    u8 *fb = gfxGetFramebuffer(GFX_TOP, GFX_LEFT, &fbWidth, &fbHeight);

    if (!fb)
    {
        return; // No framebuffer
    }

    // Fill the framebuffer with a solid color (e.g., dark gray) before drawing
    for (int i = 0; i < fbWidth * fbHeight; i++) {
        fb[i * 3 + 0] = 96;   // Blue
        fb[i * 3 + 1] = 215;  // Green
        fb[i * 3 + 2] = 30;   // Red
    }

    // The top screen is 400x240 but framebuffer is 240x400 (rotated)
    // Calculate scale to fit the entire image (accounting for 4px border on each side)
    // AND making the image 10px smaller in each direction
    float scaleX = (400.0f - 8.0f - 20.0f) / width;   // Subtract border (8px) and 10px padding on each side (20px total)
    float scaleY = (240.0f - 8.0f - 20.0f) / height;  // Subtract border (8px) and 10px padding on each side (20px total)
    float scale = (scaleX < scaleY) ? scaleX : scaleY;

    int scaledWidth = (int)(width * scale);
    int scaledHeight = (int)(height * scale);

    // Ensure scaled dimensions don't exceed available space (accounting for border and padding)
    int maxWidth = 400 - 8 - 20;  // Screen width - border - padding
    int maxHeight = 240 - 8 - 20; // Screen height - border - padding
    
    if (scaledWidth > maxWidth)
        scaledWidth = maxWidth;
    if (scaledHeight > maxHeight)
        scaledHeight = maxHeight;
    if (scaledWidth <= 0)
        scaledWidth = 1;
    if (scaledHeight <= 0)
        scaledHeight = 1;

    // Center the scaled image (accounting for border AND 10px padding)
    int imageStartX = (400 - scaledWidth) / 2;
    int imageStartY = (240 - scaledHeight) / 2;
    int imageEndX = imageStartX + scaledWidth;
    int imageEndY = imageStartY + scaledHeight;

    // Corner radius for slightly rounded corners (8 pixels radius)
    const int cornerRadius = 8;
    
    // Border width
    const int borderWidth = 4;
    
    // Outer border coordinates (including the border)
    int outerStartX = imageStartX - borderWidth;
    int outerStartY = imageStartY - borderWidth;
    int outerEndX = imageEndX + borderWidth;
    int outerEndY = imageEndY + borderWidth;
    
    // Outer corner radius (larger for the border)
    int outerCornerRadius = cornerRadius + borderWidth;

    // SIMPLIFIED: Draw the entire white border area first (including corners)
    // This will create a white rectangle with rounded corners
    for (int y = outerStartY; y < outerEndY; y++) {
        for (int x = outerStartX; x < outerEndX; x++) {
            // Skip if outside screen bounds
            if (x < 0 || x >= 400 || y < 0 || y >= 240) {
                continue;
            }
            
            // Check if this pixel is within the rounded border area
            bool inBorderArea = false;
            
            // Check all four corners
            // Top-left corner
            if (x < outerStartX + outerCornerRadius && y < outerStartY + outerCornerRadius) {
                int dx = (outerStartX + outerCornerRadius) - x;
                int dy = (outerStartY + outerCornerRadius) - y;
                if (dx * dx + dy * dy <= outerCornerRadius * outerCornerRadius) {
                    inBorderArea = true;
                }
            }
            // Top-right corner
            else if (x >= outerEndX - outerCornerRadius && y < outerStartY + outerCornerRadius) {
                int dx = x - (outerEndX - outerCornerRadius);
                int dy = (outerStartY + outerCornerRadius) - y;
                if (dx * dx + dy * dy <= outerCornerRadius * outerCornerRadius) {
                    inBorderArea = true;
                }
            }
            // Bottom-left corner
            else if (x < outerStartX + outerCornerRadius && y >= outerEndY - outerCornerRadius) {
                int dx = (outerStartX + outerCornerRadius) - x;
                int dy = y - (outerEndY - outerCornerRadius);
                if (dx * dx + dy * dy <= outerCornerRadius * outerCornerRadius) {
                    inBorderArea = true;
                }
            }
            // Bottom-right corner
            else if (x >= outerEndX - outerCornerRadius && y >= outerEndY - outerCornerRadius) {
                int dx = x - (outerEndX - outerCornerRadius);
                int dy = y - (outerEndY - outerCornerRadius);
                if (dx * dx + dy * dy <= outerCornerRadius * outerCornerRadius) {
                    inBorderArea = true;
                }
            }
            // Not in a corner - check if it's within the rectangle area
            else {
                inBorderArea = true;
            }
            
            // Draw the white border pixel
            if (inBorderArea) {
                // Convert to framebuffer coordinates (rotated)
                int fbX = 239 - y;
                int fbY = x;
                
                // Ensure within framebuffer bounds
                if (fbX >= 0 && fbX < 240 && fbY >= 0 && fbY < 400) {
                    int fbIdx = (fbX + fbY * 240) * 3;
                    // Draw white border
                    fb[fbIdx + 0] = 255;     // Blue
                    fb[fbIdx + 1] = 255;     // Green  
                    fb[fbIdx + 2] = 255;     // Red
                }
            }
        }
    }

    // Draw the actual image content with rounded corners
    // The image will be drawn on top of the white border
    for (int screenY = 0; screenY < scaledHeight; screenY++)
    {
        int posY = imageStartY + screenY;
        
        for (int screenX = 0; screenX < scaledWidth; screenX++)
        {
            int posX = imageStartX + screenX;
            
            // Check if pixel is within the image's rounded rectangle area
            bool inImageArea = false;
            
            // First check if it's within the rectangular bounds
            if (posX >= imageStartX && posX < imageEndX && posY >= imageStartY && posY < imageEndY) {
                // Now check corners
                // Top-left corner
                if (posX < imageStartX + cornerRadius && posY < imageStartY + cornerRadius) {
                    int dx = (imageStartX + cornerRadius) - posX;
                    int dy = (imageStartY + cornerRadius) - posY;
                    if (dx * dx + dy * dy <= cornerRadius * cornerRadius) {
                        inImageArea = true;
                    }
                }
                // Top-right corner
                else if (posX >= imageEndX - cornerRadius && posY < imageStartY + cornerRadius) {
                    int dx = posX - (imageEndX - cornerRadius);
                    int dy = (imageStartY + cornerRadius) - posY;
                    if (dx * dx + dy * dy <= cornerRadius * cornerRadius) {
                        inImageArea = true;
                    }
                }
                // Bottom-left corner
                else if (posX < imageStartX + cornerRadius && posY >= imageEndY - cornerRadius) {
                    int dx = (imageStartX + cornerRadius) - posX;
                    int dy = posY - (imageEndY - cornerRadius);
                    if (dx * dx + dy * dy <= cornerRadius * cornerRadius) {
                        inImageArea = true;
                    }
                }
                // Bottom-right corner
                else if (posX >= imageEndX - cornerRadius && posY >= imageEndY - cornerRadius) {
                    int dx = posX - (imageEndX - cornerRadius);
                    int dy = posY - (imageEndY - cornerRadius);
                    if (dx * dx + dy * dy <= cornerRadius * cornerRadius) {
                        inImageArea = true;
                    }
                }
                // Not in a corner area - it's in the main rectangle
                else {
                    inImageArea = true;
                }
            }
            
            // Skip drawing if pixel is NOT in the image's rounded area
            if (!inImageArea) {
                continue;
            }

            // Map back to source image
            int srcX = (int)(screenX / scale);
            int srcY = (int)(screenY / scale);

            if (srcX >= width)
                srcX = width - 1;
            if (srcY >= height)
                srcY = height - 1;

            int srcIdx = (srcY * width + srcX) * 4;
            u8 r = pixels[srcIdx + 0];
            u8 g = pixels[srcIdx + 1];
            u8 b = pixels[srcIdx + 2];

            // Convert to framebuffer coordinates (rotated)
            int fbX = 239 - posY;
            int fbY = posX;

            // Ensure within framebuffer bounds
            if (fbX < 0 || fbX >= 240 || fbY < 0 || fbY >= 400)
            {
                continue;
            }

            int fbIdx = (fbX + fbY * 240) * 3;
            int maxFbIdx = fbWidth * fbHeight * 3 - 3;

            // Write BGR (with bounds check) - this overwrites the white border
            if (fbIdx >= 0 && fbIdx <= maxFbIdx)
            {
                fb[fbIdx + 0] = b;
                fb[fbIdx + 1] = g;
                fb[fbIdx + 2] = r;
            }
        }
    }

    gfxFlushBuffers();
    gfxSwapBuffers();
}