#include "kt_empty_compat.h"

#include "json.h"
#include "kt_empty_responses.h"

#include <ctype.h>
#include <stdlib.h>
#include <string.h>

static bool is_empty_json_object(const char* body, size_t length) {
    if (body == NULL)
        return false;
    size_t index = 0;
    while (index < length && isspace((unsigned char)body[index]))
        index++;
    if (index >= length || body[index++] != '{')
        return false;
    while (index < length && isspace((unsigned char)body[index]))
        index++;
    if (index >= length || body[index++] != '}')
        return false;
    while (index < length && isspace((unsigned char)body[index]))
        index++;
    return index == length;
}

MetalsharpResponse* kt_empty_compat_response(const HttpRequest* request) {
    if (request == NULL || request->path == NULL || request->method == NULL ||
        strncmp(request->path, "/kernel-translation/", 20) != 0)
        return NULL;
    bool empty = strcmp(request->method, "GET") == 0
                     ? request->query == NULL || request->query[0] == '\0'
                     : strcmp(request->method, "POST") == 0 && is_empty_json_object(request->body, request->body_len);
    if (!empty)
        return NULL;
    for (size_t index = 0; index < sizeof(KT_EMPTY_RESPONSES) / sizeof(KT_EMPTY_RESPONSES[0]); index++) {
        const KtEmptyResponse* entry = &KT_EMPTY_RESPONSES[index];
        if (strcmp(entry->method, request->method) != 0 || strcmp(entry->path, request->path) != 0)
            continue;
        JsonValue* value = json_parse(entry->json, strlen(entry->json), NULL);
        if (value == NULL)
            return NULL;
        MetalsharpResponse* response = calloc(1, sizeof(*response));
        if (response == NULL) {
            json_free(value);
            return NULL;
        }
        response->ok = true;
        response->data = value;
        response->data_kind = METALSHARP_RESPONSE_JSON_VALUE;
        return response;
    }
    return NULL;
}
