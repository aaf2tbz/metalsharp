#ifndef MSCOREE_UNIX_H
#define MSCOREE_UNIX_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

enum mscoree_func_id {
    MSCOREE_FUNC_INIT = 0,
    MSCOREE_FUNC_COR_EXE_MAIN,
    MSCOREE_FUNC_GET_COR_VERSION,
};

struct mscoree_cor_exe_main_params {
    char exe_path[1024];
    char exe_dir[1024];
    int argc;
    char *argv[64];
    int exit_code;
};

struct mscoree_cor_version_params {
    char version[64];
    int32_t version_len;
};

#ifdef __cplusplus
}
#endif

#endif
