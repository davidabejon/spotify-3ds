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
        return NULL;
    }

    ret = httpcBeginRequest(&context);
    if (R_FAILED(ret))
    {
        httpcCloseContext(&context);
        return NULL;
    }

    u32 statusCode = 0;
    httpcGetResponseStatusCode(&context, &statusCode);

    if (statusCode != 200)
    {
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
        httpcCloseContext(&context);
        return NULL;
    }

    // Read data
    ret = httpcReceiveData(&context, (u8 *)buffer, totalSize);
    if (R_FAILED(ret))
    {
        free(buffer);
        httpcCloseContext(&context);
        return NULL;
    }

    buffer[totalSize] = '\0'; // end of string

    httpcCloseContext(&context);
    return buffer;
}

// ------------------------------
// Returns a char* with the JSON
// Returns NULL on failure
// Caller must free()
// url: base URL, params: query string (e.g. "foo=1&bar=2")
// ------------------------------
char *fetch_with_params(const char *url, const char *params)
{
    char full_url[512];
    if (params && strlen(params) > 0)
    {
        snprintf(full_url, sizeof(full_url), "%s?%s", url, params);
    }
    else
    {
        snprintf(full_url, sizeof(full_url), "%s", url);
    }
    return fetch(full_url);
}