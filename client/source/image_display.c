#include "image_display.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <malloc.h>
#include <math.h>

#define SOC_ALIGN 0x1000
#define SOC_BUFFERSIZE 0x100000

static u32 *SOC_buffer = NULL;
static bool network_initialized = false;
// Playback overlay flag
static bool playback_paused = false;
// Temporary play overlay (shown until server confirms play)
static bool temp_play_overlay = false;
// Overlay animation state
static int overlay_alpha = 0; // 0..255
// 0 = none, 1 = temp play, 2 = pause
static int current_overlay = 0;
static const int overlay_fade_step = 85; // alpha change per frame (higher = faster)

void setPlaybackPaused(bool paused)
{
    playback_paused = paused;
}

void setTemporaryPlay(bool show)
{
    temp_play_overlay = show;
}

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

    // Draw a soft drop shadow outside the outer rounded border (blended)
    const int shadowOffsetX = 8;
    const int shadowOffsetY = 8;
    const int shadowBlur = 12; // how far the shadow spreads
    const int maxShadowAlpha = 160; // max alpha (0-255)

    // Shadow is computed relative to the outer rounded rectangle shifted by the offset
    int shiftedOuterStartX = outerStartX + shadowOffsetX;
    int shiftedOuterEndX   = outerEndX   + shadowOffsetX;
    int shiftedOuterStartY = outerStartY + shadowOffsetY;
    int shiftedOuterEndY   = outerEndY   + shadowOffsetY;

    int shadowMinX = shiftedOuterStartX - shadowBlur;
    int shadowMaxX = shiftedOuterEndX + shadowBlur - 1;
    int shadowMinY = shiftedOuterStartY - shadowBlur;
    int shadowMaxY = shiftedOuterEndY + shadowBlur - 1;

    for (int y = shadowMinY; y <= shadowMaxY; y++) {
        for (int x = shadowMinX; x <= shadowMaxX; x++) {
            if (x < 0 || x >= 400 || y < 0 || y >= 240) continue;

            // Compute distance from (x,y) to the shifted outer rounded rectangle.
            // For rounded rect, distance is computed by clamping to the central rectangle
            // defined by removing the corner radius; outside that area distance to corner circle applies.
            int innerStartX = shiftedOuterStartX + outerCornerRadius;
            int innerEndX   = shiftedOuterEndX   - outerCornerRadius - 1;
            int innerStartY = shiftedOuterStartY + outerCornerRadius;
            int innerEndY   = shiftedOuterEndY   - outerCornerRadius - 1;

            int dx = 0;
            if (x < innerStartX) dx = innerStartX - x;
            else if (x > innerEndX) dx = x - innerEndX;

            int dy = 0;
            if (y < innerStartY) dy = innerStartY - y;
            else if (y > innerEndY) dy = y - innerEndY;

            float dist = sqrtf((float)(dx * dx + dy * dy));

            // If the point lies inside the shifted rounded rect (dist == 0), skip — shadow should not draw over the object
            if (dist <= 0.0f) continue;

            // If distance is beyond blur radius, skip
            if (dist >= shadowBlur) continue;

            // Alpha falls off linearly with distance
            int alpha = (int)((1.0f - (dist / (float)shadowBlur)) * maxShadowAlpha);
            if (alpha <= 0) continue;

            // Map to framebuffer (rotated coordinates)
            int fbX = 239 - y;
            int fbY = x;
            if (fbX < 0 || fbX >= 240 || fbY < 0 || fbY >= 400) continue;

            int fbIdx = (fbX + fbY * 240) * 3;
            int maxFbIdx = fbWidth * fbHeight * 3 - 3;
            if (fbIdx < 0 || fbIdx > maxFbIdx) continue;

            // Shadow color (very dark gray)
            const int sB = 18, sG = 18, sR = 18;

            // Blend shadow over existing framebuffer pixel: out = alpha*shadow + (255-alpha)*dst / 255
            u8 dstB = fb[fbIdx + 0];
            u8 dstG = fb[fbIdx + 1];
            u8 dstR = fb[fbIdx + 2];

            fb[fbIdx + 0] = (u8)((alpha * sB + (255 - alpha) * dstB) / 255);
            fb[fbIdx + 1] = (u8)((alpha * sG + (255 - alpha) * dstG) / 255);
            fb[fbIdx + 2] = (u8)((alpha * sR + (255 - alpha) * dstR) / 255);
        }
    }

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

    // Update overlay animation state (fade in/out)
    int desired_overlay = temp_play_overlay ? 1 : (playback_paused ? 2 : 0);

    // If a new overlay is requested while none was active, switch to it to start fading in.
    if (desired_overlay != 0 && current_overlay != desired_overlay)
    {
        current_overlay = desired_overlay;
    }

    int target_alpha = (desired_overlay != 0) ? 255 : 0;
    if (target_alpha > overlay_alpha)
    {
        int next = overlay_alpha + overlay_fade_step;
        overlay_alpha = (next > target_alpha) ? target_alpha : next;
    }
    else if (target_alpha < overlay_alpha)
    {
        int next = overlay_alpha - overlay_fade_step;
        overlay_alpha = (next < target_alpha) ? target_alpha : next;
    }

    // If we've fully faded out and nothing desired, clear current overlay
    if (overlay_alpha == 0 && desired_overlay == 0)
        current_overlay = 0;

    // If an overlay is currently active (possibly fading), draw it using overlay_alpha
    if (current_overlay == 1)
    {
        // Icon size: larger (roughly 2/3 of scaled image)
        int iconW = (scaledWidth * 2) / 3;
        int iconH = (scaledHeight * 2) / 3;
        if (iconW < 80) iconW = 80;
        if (iconH < 80) iconH = 80;

        int iconCX = imageStartX + scaledWidth / 2;
        int iconCY = imageStartY + scaledHeight / 2;
        int iconStartX = iconCX - iconW / 2;
        int iconStartY = iconCY - iconH / 2;
        int iconEndX = iconStartX + iconW;
        int iconEndY = iconStartY + iconH;

        const int iconCorner = 12;
        const int bgBaseAlpha = 200; // background overlay alpha (stronger)
        int bgAlpha = (bgBaseAlpha * overlay_alpha) / 255;

        // Draw rounded dark background (same as pause)
        for (int y = iconStartY; y < iconEndY; y++) {
            for (int x = iconStartX; x < iconEndX; x++) {
                if (x < 0 || x >= 400 || y < 0 || y >= 240) continue;

                bool inArea = false;
                if (x < iconStartX + iconCorner && y < iconStartY + iconCorner) {
                    int dx = (iconStartX + iconCorner) - x;
                    int dy = (iconStartY + iconCorner) - y;
                    if (dx*dx + dy*dy <= iconCorner*iconCorner) inArea = true;
                }
                else if (x >= iconEndX - iconCorner && y < iconStartY + iconCorner) {
                    int dx = x - (iconEndX - iconCorner);
                    int dy = (iconStartY + iconCorner) - y;
                    if (dx*dx + dy*dy <= iconCorner*iconCorner) inArea = true;
                }
                else if (x < iconStartX + iconCorner && y >= iconEndY - iconCorner) {
                    int dx = (iconStartX + iconCorner) - x;
                    int dy = y - (iconEndY - iconCorner);
                    if (dx*dx + dy*dy <= iconCorner*iconCorner) inArea = true;
                }
                else if (x >= iconEndX - iconCorner && y >= iconEndY - iconCorner) {
                    int dx = x - (iconEndX - iconCorner);
                    int dy = y - (iconEndY - iconCorner);
                    if (dx*dx + dy*dy <= iconCorner*iconCorner) inArea = true;
                }
                else {
                    inArea = true;
                }

                if (!inArea) continue;

                int fbX = 239 - y;
                int fbY = x;
                if (fbX < 0 || fbX >= 240 || fbY < 0 || fbY >= 400) continue;
                int fbIdx = (fbX + fbY * 240) * 3;
                int maxFbIdx = fbWidth * fbHeight * 3 - 3;
                if (fbIdx < 0 || fbIdx > maxFbIdx) continue;

                const int sB = 10, sG = 10, sR = 10;
                u8 dstB = fb[fbIdx + 0];
                u8 dstG = fb[fbIdx + 1];
                u8 dstR = fb[fbIdx + 2];
                fb[fbIdx + 0] = (u8)((bgAlpha * sB + (255 - bgAlpha) * dstB) / 255);
                fb[fbIdx + 1] = (u8)((bgAlpha * sG + (255 - bgAlpha) * dstG) / 255);
                fb[fbIdx + 2] = (u8)((bgAlpha * sR + (255 - bgAlpha) * dstR) / 255);
            }
        }

        // Draw a right-pointing play triangle with inner padding
        // padding inside the icon on all sides
        int pad = iconW / 4;
        int padH = iconH / 4;
        if (pad < 16) pad = 16;
        if (padH < 16) padH = 16;

        int Ax = iconStartX + pad, Ay = iconStartY + padH;
        int Bx = iconStartX + pad, By = iconEndY - padH;
        int Cx = iconEndX - pad, Cy = iconCY;

        int triMinX = Ax;
        int triMaxX = Cx;
        int triMinY = Ay;
        int triMaxY = By;

        for (int y = triMinY; y <= triMaxY; y++) {
            for (int x = triMinX; x <= triMaxX; x++) {
                if (x < 0 || x >= 400 || y < 0 || y >= 240) continue;
                int v0x = Cx - Ax; int v0y = Cy - Ay;
                int v1x = Bx - Ax; int v1y = By - Ay;
                int v2x = x - Ax;  int v2y = y - Ay;
                int denom = v0x * v1y - v1x * v0y;
                if (denom == 0) continue;
                float u = (float)(v2x * v1y - v1x * v2y) / (float)denom;
                float v = (float)(v0x * v2y - v2x * v0y) / (float)denom;
                if (u >= 0.0f && v >= 0.0f && (u + v) <= 1.0f) {
                    int fbX = 239 - y;
                    int fbY = x;
                    if (fbX < 0 || fbX >= 240 || fbY < 0 || fbY >= 400) continue;
                    int fbIdx = (fbX + fbY * 240) * 3;
                    int maxFbIdx = fbWidth * fbHeight * 3 - 3;
                    if (fbIdx < 0 || fbIdx > maxFbIdx) continue;
                    // Blend white triangle using overlay_alpha
                    u8 dstB = fb[fbIdx + 0];
                    u8 dstG = fb[fbIdx + 1];
                    u8 dstR = fb[fbIdx + 2];
                    int a = overlay_alpha;
                    fb[fbIdx + 0] = (u8)((a * 255 + (255 - a) * dstB) / 255);
                    fb[fbIdx + 1] = (u8)((a * 255 + (255 - a) * dstG) / 255);
                    fb[fbIdx + 2] = (u8)((a * 255 + (255 - a) * dstR) / 255);
                }
            }
        }
    }
    else if (current_overlay == 2)
    {
        // Icon size: larger (roughly 2/3 of scaled image)
        int iconW = (scaledWidth * 2) / 3;
        int iconH = (scaledHeight * 2) / 3;
        if (iconW < 80) iconW = 80;
        if (iconH < 80) iconH = 80;

        int iconCX = imageStartX + scaledWidth / 2;
        int iconCY = imageStartY + scaledHeight / 2;
        int iconStartX = iconCX - iconW / 2;
        int iconStartY = iconCY - iconH / 2;
        int iconEndX = iconStartX + iconW;
        int iconEndY = iconStartY + iconH;

        const int iconCorner = 12;
        const int bgBaseAlpha = 200; // background overlay alpha (stronger)
        int bgAlpha = (bgBaseAlpha * overlay_alpha) / 255;

        for (int y = iconStartY; y < iconEndY; y++) {
            for (int x = iconStartX; x < iconEndX; x++) {
                if (x < 0 || x >= 400 || y < 0 || y >= 240) continue;

                // Rounded rect test (same approach as above)
                bool inArea = false;
                // TL
                if (x < iconStartX + iconCorner && y < iconStartY + iconCorner) {
                    int dx = (iconStartX + iconCorner) - x;
                    int dy = (iconStartY + iconCorner) - y;
                    if (dx*dx + dy*dy <= iconCorner*iconCorner) inArea = true;
                }
                // TR
                else if (x >= iconEndX - iconCorner && y < iconStartY + iconCorner) {
                    int dx = x - (iconEndX - iconCorner);
                    int dy = (iconStartY + iconCorner) - y;
                    if (dx*dx + dy*dy <= iconCorner*iconCorner) inArea = true;
                }
                // BL
                else if (x < iconStartX + iconCorner && y >= iconEndY - iconCorner) {
                    int dx = (iconStartX + iconCorner) - x;
                    int dy = y - (iconEndY - iconCorner);
                    if (dx*dx + dy*dy <= iconCorner*iconCorner) inArea = true;
                }
                // BR
                else if (x >= iconEndX - iconCorner && y >= iconEndY - iconCorner) {
                    int dx = x - (iconEndX - iconCorner);
                    int dy = y - (iconEndY - iconCorner);
                    if (dx*dx + dy*dy <= iconCorner*iconCorner) inArea = true;
                }
                else {
                    inArea = true;
                }

                if (!inArea) continue;

                int fbX = 239 - y;
                int fbY = x;
                if (fbX < 0 || fbX >= 240 || fbY < 0 || fbY >= 400) continue;
                int fbIdx = (fbX + fbY * 240) * 3;
                int maxFbIdx = fbWidth * fbHeight * 3 - 3;
                if (fbIdx < 0 || fbIdx > maxFbIdx) continue;

                // Blend dark background (scaled by overlay alpha)
                const int sB = 10, sG = 10, sR = 10;
                u8 dstB = fb[fbIdx + 0];
                u8 dstG = fb[fbIdx + 1];
                u8 dstR = fb[fbIdx + 2];
                fb[fbIdx + 0] = (u8)((bgAlpha * sB + (255 - bgAlpha) * dstB) / 255);
                fb[fbIdx + 1] = (u8)((bgAlpha * sG + (255 - bgAlpha) * dstG) / 255);
                fb[fbIdx + 2] = (u8)((bgAlpha * sR + (255 - bgAlpha) * dstR) / 255);
            }
        }

        // Draw two white pause bars (thicker for larger icon)
        int barW = iconW / 5;
        if (barW < 10) barW = 10;
        int barH = (int)(iconH * 0.7f);
        int barTop = iconCY - barH / 2;
        int leftBarX = iconStartX + iconW/3 - barW/2;
        int rightBarX = iconStartX + (2*iconW)/3 - barW/2;

        for (int y = barTop; y < barTop + barH; y++) {
            for (int x = leftBarX; x < leftBarX + barW; x++) {
                if (x < 0 || x >= 400 || y < 0 || y >= 240) continue;
                int fbX = 239 - y;
                int fbY = x;
                int fbIdx = (fbX + fbY * 240) * 3;
                int maxFbIdx = fbWidth * fbHeight * 3 - 3;
                if (fbIdx < 0 || fbIdx > maxFbIdx) continue;
                // Blend white pause bar using overlay_alpha
                u8 dstB = fb[fbIdx + 0];
                u8 dstG = fb[fbIdx + 1];
                u8 dstR = fb[fbIdx + 2];
                int a = overlay_alpha;
                fb[fbIdx + 0] = (u8)((a * 255 + (255 - a) * dstB) / 255);
                fb[fbIdx + 1] = (u8)((a * 255 + (255 - a) * dstG) / 255);
                fb[fbIdx + 2] = (u8)((a * 255 + (255 - a) * dstR) / 255);
            }
            for (int x = rightBarX; x < rightBarX + barW; x++) {
                if (x < 0 || x >= 400 || y < 0 || y >= 240) continue;
                int fbX = 239 - y;
                int fbY = x;
                int fbIdx = (fbX + fbY * 240) * 3;
                int maxFbIdx = fbWidth * fbHeight * 3 - 3;
                if (fbIdx < 0 || fbIdx > maxFbIdx) continue;
                u8 dstB = fb[fbIdx + 0];
                u8 dstG = fb[fbIdx + 1];
                u8 dstR = fb[fbIdx + 2];
                int a = overlay_alpha;
                fb[fbIdx + 0] = (u8)((a * 255 + (255 - a) * dstB) / 255);
                fb[fbIdx + 1] = (u8)((a * 255 + (255 - a) * dstG) / 255);
                fb[fbIdx + 2] = (u8)((a * 255 + (255 - a) * dstR) / 255);
            }
        }
    }

    gfxFlushBuffers();
    gfxSwapBuffers();
}

void drawBackgroundToScreen()
{
    u16 fbWidth, fbHeight;
    u8 *fb = gfxGetFramebuffer(GFX_TOP, GFX_LEFT, &fbWidth, &fbHeight);

    if (!fb)
    {
        return; // No framebuffer
    }

    for (int i = 0; i < fbWidth * fbHeight; i++)
    {
        fb[i * 3 + 0] = 96;   // Blue
        fb[i * 3 + 1] = 215;  // Green
        fb[i * 3 + 2] = 30;   // Red
    }

    gfxFlushBuffers();
    gfxSwapBuffers();
}