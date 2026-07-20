#ifndef METALSHARP_LAUNCH_H
#define METALSHARP_LAUNCH_H

#include <stdbool.h>
#include <stddef.h>
#include <sys/types.h>

bool metalsharp_launch_wine_executable(const char* executable_path, const char* working_directory,
                                       const char* prefix_override, const char* const* arguments, size_t argument_count,
                                       pid_t* pid_out, char* error, size_t error_size);

#endif
