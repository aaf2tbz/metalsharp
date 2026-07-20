#ifndef METALSHARP_COMPAT_LOG_H
#define METALSHARP_COMPAT_LOG_H

#include <stddef.h>

void metalsharp_app_log(const char* format, ...);
char* metalsharp_log_stream_json(size_t after);
char* metalsharp_log_list_json(void);

#endif
