#include <3ds.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "fetch.h"

// ------------------------------
// Returns a char* with the JSON
// Returns NULL on failure
// Caller must free()
// ------------------------------
char *fetch(const char *url)
{
    httpcContext context;
    Result ret;

    ret = httpcOpenContext(&context, HTTPC_METHOD_GET, url, 1);
    if (R_FAILED(ret))
    {
        printf("httpcOpenContext error: %08lX\n", ret);
        return NULL;
    }

    ret = httpcBeginRequest(&context);
    if (R_FAILED(ret))
    {
        printf("httpcBeginRequest error: %08lX\n", ret);
        httpcCloseContext(&context);
        return NULL;
    }

    u32 statusCode = 0;
    httpcGetResponseStatusCode(&context, &statusCode);

    if (statusCode != 200)
    {
        printf("HTTP status: %lu\n", statusCode);
        httpcCloseContext(&context);
        return NULL;
    }

    u32 downloadedSize = 0;
    u32 totalSize = 0;
    httpcGetDownloadSizeState(&context, &downloadedSize, &totalSize);

    // If the server doesn't provide it, use an arbitrary size (e.g., 8KB)
    if (totalSize == 0)
        totalSize = 8192;

    // Allocate memory for the JSON
    char *buffer = (char *)malloc(totalSize + 1);
    if (!buffer)
    {
        printf("Error allocating memory.\n");
        httpcCloseContext(&context);
        return NULL;
    }

    // Read data
    ret = httpcReceiveData(&context, (u8 *)buffer, totalSize);
    if (R_FAILED(ret))
    {
        printf("httpcReceiveData error: %08lX\n", ret);
        free(buffer);
        httpcCloseContext(&context);
        return NULL;
    }

    buffer[totalSize] = '\0'; // end of string

    httpcCloseContext(&context);
    return buffer;
}