#ifndef METALSHARP_KT_EMPTY_COMPAT_H
#define METALSHARP_KT_EMPTY_COMPAT_H

#include "http_server.h"
#include "server.h"

/* Return the legacy empty-input response for a kernel-translation route.
 * Returns NULL when the request is not an empty-input compatibility case. */
MetalsharpResponse* kt_empty_compat_response(const HttpRequest* request);

#endif
