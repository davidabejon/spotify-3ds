#include "image_display.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <malloc.h>

#define SOC_ALIGN 0x1000
#define SOC_BUFFERSIZE 0x100000

static u32 *SOC_buffer = NULL;
static bool network_initialized = false;

Result initNetwork(void) {
    if (network_initialized) {
        return 0;
    }
    
    // Allocate buffer for SOC service
    SOC_buffer = (u32*)memalign(SOC_ALIGN, SOC_BUFFERSIZE);
    if (SOC_buffer == NULL) {
        return -1;
    }
    
    // Initialize SOC service
    Result ret = socInit(SOC_buffer, SOC_BUFFERSIZE);
    if (ret != 0) {
        free(SOC_buffer);
        SOC_buffer = NULL;
        return ret;
    }
    
    // Initialize HTTP service
    ret = httpcInit(0);
    if (ret != 0) {
        socExit();
        free(SOC_buffer);
        SOC_buffer = NULL;
        return ret;
    }
    
    network_initialized = true;
    return 0;
}

void cleanupNetwork(void) {
    if (!network_initialized) {
        return;
    }
    
    httpcExit();
    
    if (SOC_buffer != NULL) {
        socExit();
        free(SOC_buffer);
        SOC_buffer = NULL;
    }
    
    network_initialized = false;
}

u8* downloadImage(const char* url, u32* size) {
    if (!network_initialized) {
        printf("Network not initialized!\n");
        return NULL;
    }
    
    Result ret = 0;
    httpcContext context;
    u32 statuscode = 0;
    u32 contentsize = 0;
    u8* buffer = NULL;
    
    ret = httpcOpenContext(&context, HTTPC_METHOD_GET, url, 0);
    if (ret != 0) {
        printf("httpcOpenContext failed: 0x%lx\n", ret);
        return NULL;
    }
    
    // Set SSL options
    ret = httpcSetSSLOpt(&context, SSLCOPT_DisableVerify);
    if (ret != 0) {
        printf("httpcSetSSLOpt failed: 0x%lx\n", ret);
        httpcCloseContext(&context);
        return NULL;
    }
    
    // Add user agent
    ret = httpcAddRequestHeaderField(&context, "User-Agent", "Mozilla/5.0 (Nintendo 3DS)");
    
    // Set keep alive to false
    ret = httpcSetKeepAlive(&context, HTTPC_KEEPALIVE_DISABLED);
    
    ret = httpcBeginRequest(&context);
    if (ret != 0) {
        printf("httpcBeginRequest failed: 0x%lx\n", ret);
        httpcCloseContext(&context);
        return NULL;
    }
    
    ret = httpcGetResponseStatusCode(&context, &statuscode);
    if (ret != 0 || statuscode != 200) {
        printf("Status code: %lu\n", statuscode);
        httpcCloseContext(&context);
        return NULL;
    }
    
    ret = httpcGetDownloadSizeState(&context, NULL, &contentsize);
    if (ret != 0) {
        printf("httpcGetDownloadSizeState failed: 0x%lx\n", ret);
        httpcCloseContext(&context);
        return NULL;
    }
    
    buffer = (u8*)malloc(contentsize);
    if (buffer == NULL) {
        printf("Memory allocation failed\n");
        httpcCloseContext(&context);
        return NULL;
    }
    
    ret = httpcDownloadData(&context, buffer, contentsize, NULL);
    if (ret != 0) {
        printf("httpcDownloadData failed: 0x%lx\n", ret);
        free(buffer);
        httpcCloseContext(&context);
        return NULL;
    }
    
    *size = contentsize;
    httpcCloseContext(&context);
    return buffer;
}

void drawImageToScreen(u8* pixels, int width, int height) {
    // Get framebuffer dimensions
    u16 fbWidth, fbHeight;
    u8* fb = gfxGetFramebuffer(GFX_TOP, GFX_LEFT, &fbWidth, &fbHeight);
    
    // Clear the framebuffer
    memset(fb, 0, fbWidth * fbHeight * 3);
    
    // The top screen is 400x240 but framebuffer is 240x400 (rotated)
    // Calculate scale to fit the entire image
    float scaleX = 400.0f / width;
    float scaleY = 240.0f / height;
    float scale = (scaleX < scaleY) ? scaleX : scaleY;
    
    int scaledWidth = (int)(width * scale);
    int scaledHeight = (int)(height * scale);
    
    // Center the scaled image
    int startX = (400 - scaledWidth) / 2;
    int startY = (240 - scaledHeight) / 2;
    
    for (int screenY = 0; screenY < scaledHeight; screenY++) {
        for (int screenX = 0; screenX < scaledWidth; screenX++) {
            // Map back to source image
            int srcX = (int)(screenX / scale);
            int srcY = (int)(screenY / scale);
            
            if (srcX >= width) srcX = width - 1;
            if (srcY >= height) srcY = height - 1;
            
            int srcIdx = (srcY * width + srcX) * 4;
            u8 r = pixels[srcIdx + 0];
            u8 g = pixels[srcIdx + 1];
            u8 b = pixels[srcIdx + 2];
            
            // Actual screen position
            int posX = startX + screenX;
            int posY = startY + screenY;
            
            // Convert to framebuffer coordinates
            int fbX = 239 - posY;
            int fbY = posX;
            
            int fbIdx = (fbX + fbY * 240) * 3;
            
            // Write BGR
            if (fbIdx >= 0 && fbIdx < fbWidth * fbHeight * 3 - 2) {
                fb[fbIdx + 0] = b;
                fb[fbIdx + 1] = g;
                fb[fbIdx + 2] = r;
            }
        }
    }
    
    gfxFlushBuffers();
    gfxSwapBuffers();
}