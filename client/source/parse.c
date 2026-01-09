#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>

// ------------------------------
// Returns the value associated with the key in the JSON string
// Caller must free() the returned string
// ------------------------------
char *get(const char *key, const char *json)
{
    if (!key || !json)
        return NULL;

    char pattern[256];
    snprintf(pattern, sizeof(pattern), "\"%s\":", key);

    char *pos = strstr(json, pattern);
    if (!pos)
        return NULL;

    pos += strlen(pattern);

    // Skip spaces
    while (*pos && isspace((unsigned char)*pos))
        pos++;

    char *value = NULL;

    if (*pos == '"')
    { // string value
        pos++;
        char *end = strchr(pos, '"');
        if (!end)
            return NULL;
        size_t len = end - pos;
        value = (char *)malloc(len + 1);
        if (!value)
            return NULL;
        strncpy(value, pos, len);
        value[len] = '\0';
    }
    else
    { // numeric or boolean value
        char *end = pos;
        while (*end && *end != ',' && *end != '}' && !isspace((unsigned char)*end))
            end++;
        size_t len = end - pos;
        value = (char *)malloc(len + 1);
        if (!value)
            return NULL;
        strncpy(value, pos, len);
        value[len] = '\0';
    }

    return value;
}
