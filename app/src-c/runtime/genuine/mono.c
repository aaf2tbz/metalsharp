/* Wine Mono management routes. Behavioral port of mono.rs detection,
 * prefix resolution, status, no-op install, download handoff, and reset paths. */

#include "database.h"
#include "http_server.h"
#include "json.h"
#include "logger.h"
#include "server.h"

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <pthread.h>
#include <signal.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#define MONO_VERSION "11.2.0"
#define MONO_MSI     "wine-mono-11.2.0-x86.msi"
#define MONO_URL "https://github.com/wine-mono/wine-mono/releases/download/wine-mono-11.2.0/wine-mono-11.2.0-x86.msi"

typedef struct {
    bool running;
    bool downloading;
    pid_t pid;
    unsigned long long started_at;
    unsigned long long download_bytes;
    unsigned long long download_total;
    char prefix[PATH_MAX];
    char log_path[PATH_MAX];
    char last_error[512];
    char download_error[512];
} MonoState;

static pthread_mutex_t g_mono_lock = PTHREAD_MUTEX_INITIALIZER;
static MonoState g_mono_state;

static void mono_stop_wineserver(const char* prefix);
static unsigned int mono_sweep_orphans(const char* prefix);

static MetalsharpResponse* make_data_response(const char* body) {
    MetalsharpResponse* response = calloc(1, sizeof(*response));
    if (response == NULL)
        return NULL;
    response->data = json_parse(body, strlen(body), NULL);
    if (response->data == NULL) {
        response->error_msg = strdup("internal error");
        return response;
    }
    response->ok = true;
    response->data_kind = METALSHARP_RESPONSE_JSON_VALUE;
    return response;
}

static const char* mono_home(void) {
    const char* home = getenv("METALSHARP_HOME");
    if (home == NULL || home[0] == '\0')
        home = getenv("HOME");
    return home != NULL ? home : "";
}

static bool mono_join(char* output, size_t output_size, const char* left, const char* right) {
    int written = snprintf(output, output_size, "%s%s%s", left,
                           left[0] != '\0' && left[strlen(left) - 1u] == '/' ? "" : "/", right);
    return written >= 0 && (size_t)written < output_size;
}

static bool mono_directory(const char* path) {
    struct stat st;
    return stat(path, &st) == 0 && S_ISDIR(st.st_mode);
}

static bool mono_file(const char* path) {
    struct stat st;
    return stat(path, &st) == 0 && S_ISREG(st.st_mode);
}

static bool mono_mkdir_p(const char* path) {
    char copy[PATH_MAX];
    int written = snprintf(copy, sizeof(copy), "%s", path);
    if (written < 0 || (size_t)written >= sizeof(copy))
        return false;
    for (char* p = copy + 1; *p != '\0'; p++) {
        if (*p != '/')
            continue;
        *p = '\0';
        if (mkdir(copy, 0755) != 0 && errno != EEXIST)
            return false;
        *p = '/';
    }
    return mkdir(copy, 0755) == 0 || errno == EEXIST;
}

static bool mono_prefix(const char* kind, char* output, size_t output_size) {
    if (strcmp(kind, "gog") == 0)
        return mono_join(output, output_size, mono_home(), "bottles/gog-prefix/prefix");
    if (strcmp(kind, "steam") == 0)
        return mono_join(output, output_size, mono_home(), "prefix-steam");
    return false;
}

static const char* mono_query_prefix(const HttpRequest* req, char* storage, size_t storage_size) {
    if (req == NULL || req->query == NULL)
        return "gog";
    const char* found = strstr(req->query, "prefix=");
    if (found == NULL)
        return "gog";
    found += 7;
    size_t length = strcspn(found, "&");
    if (length == 0u || length >= storage_size)
        return "gog";
    memcpy(storage, found, length);
    storage[length] = '\0';
    return storage;
}

static char* mono_body_prefix(const HttpRequest* req) {
    JsonValue* root = req != NULL ? json_parse(req->body, req->body_len, NULL) : NULL;
    const char* value =
        root != NULL && json_type(root) == JSON_OBJECT ? json_get_string(json_object_get(root, "prefix")) : NULL;
    char* result = strdup(value != NULL ? value : "gog");
    json_free(root);
    return result;
}

static bool mono_json_escape(const char* input, char* output, size_t output_size) {
    size_t used = 0u;
    for (const unsigned char* p = (const unsigned char*)input; *p != '\0'; p++) {
        const char* replacement = NULL;
        switch (*p) {
        case '"':
            replacement = "\\\"";
            break;
        case '\\':
            replacement = "\\\\";
            break;
        case '\n':
            replacement = "\\n";
            break;
        case '\r':
            replacement = "\\r";
            break;
        case '\t':
            replacement = "\\t";
            break;
        default:
            break;
        }
        if (replacement != NULL) {
            size_t length = strlen(replacement);
            if (used + length >= output_size)
                return false;
            memcpy(output + used, replacement, length);
            used += length;
        } else {
            if (used + 1u >= output_size)
                return false;
            output[used++] = (char)*p;
        }
    }
    output[used] = '\0';
    return true;
}

static bool mono_status_json(const char* kind, char* output, size_t output_size) {
    char prefix[PATH_MAX], latest_marker[PATH_MAX], any_marker[PATH_MAX], cache[PATH_MAX];
    if (!mono_prefix(kind, prefix, sizeof(prefix)))
        return false;
    mono_join(latest_marker, sizeof(latest_marker), prefix, "drive_c/windows/mono/wine-mono-11.2.0");
    mono_join(any_marker, sizeof(any_marker), prefix, "drive_c/windows/mono/mono-2.0");
    mono_join(cache, sizeof(cache), mono_home(), "cache/wine-mono/" MONO_MSI);
    bool latest = mono_directory(latest_marker);
    bool installed = latest || mono_directory(any_marker);
    MonoState state;
    pthread_mutex_lock(&g_mono_lock);
    state = g_mono_state;
    pthread_mutex_unlock(&g_mono_lock);
    unsigned long long elapsed = state.started_at > 0u && (unsigned long long)time(NULL) >= state.started_at
                                     ? (unsigned long long)time(NULL) - state.started_at
                                     : 0u;
    bool stalled = state.running && state.started_at > 0u && elapsed >= 450u;
    char escaped_log[PATH_MAX * 2u], escaped_error[1024], escaped_download_error[1024];
    char pid_json[64], started_json[64], elapsed_json[64], log_json[PATH_MAX * 2u + 4u];
    char error_json[1030], download_error_json[1030];
    mono_json_escape(state.log_path, escaped_log, sizeof(escaped_log));
    mono_json_escape(state.last_error, escaped_error, sizeof(escaped_error));
    mono_json_escape(state.download_error, escaped_download_error, sizeof(escaped_download_error));
    snprintf(pid_json, sizeof(pid_json), state.pid > 0 ? "%ld" : "null", (long)state.pid);
    if (state.started_at > 0u) {
        snprintf(started_json, sizeof(started_json), "%llu", state.started_at);
        snprintf(elapsed_json, sizeof(elapsed_json), "%llu", elapsed);
    } else {
        snprintf(started_json, sizeof(started_json), "null");
        snprintf(elapsed_json, sizeof(elapsed_json), "null");
    }
    snprintf(log_json, sizeof(log_json), state.log_path[0] != '\0' ? "\"%s\"" : "null", escaped_log);
    snprintf(error_json, sizeof(error_json), state.last_error[0] != '\0' ? "\"%s\"" : "null", escaped_error);
    snprintf(download_error_json, sizeof(download_error_json), state.download_error[0] != '\0' ? "\"%s\"" : "null",
             escaped_download_error);
    int written =
        snprintf(output, output_size,
                 "{\"ok\":true,\"prefixKind\":\"%s\",\"latestVersion\":\"" MONO_VERSION
                 "\",\"installedVersion\":%s,\"installed\":%s,\"upToDate\":%s,\"running\":%s,"
                 "\"stalled\":%s,\"pid\":%s,\"startedAt\":%s,\"elapsedSeconds\":%s,\"logPath\":%s,"
                 "\"targetVersion\":\"" MONO_VERSION
                 "\",\"lastError\":%s,\"msiCached\":%s,\"downloading\":%s,\"downloadBytes\":%llu,"
                 "\"downloadTotal\":%llu,\"downloadError\":%s}",
                 kind, latest ? "\"11.2.0\"" : "null", installed ? "true" : "false", latest ? "true" : "false",
                 state.running ? "true" : "false", stalled ? "true" : "false", pid_json, started_json, elapsed_json,
                 log_json, error_json, mono_file(cache) ? "true" : "false", state.downloading ? "true" : "false",
                 state.download_bytes, state.download_total, download_error_json);
    return written >= 0 && (size_t)written < output_size;
}

static MetalsharpResponse* mono_unknown_prefix(const char* kind) {
    char escaped[PATH_MAX * 2u], body[PATH_MAX * 2u + 64u];
    mono_json_escape(kind, escaped, sizeof(escaped));
    snprintf(body, sizeof(body), "{\"ok\":false,\"error\":\"unknown prefix kind: %s\"}", escaped);
    return make_data_response(body);
}

static MetalsharpResponse* handle_mono_status(const HttpRequest* req) {
    char storage[64], body[2048];
    const char* kind = mono_query_prefix(req, storage, sizeof(storage));
    if (!mono_status_json(kind, body, sizeof(body)))
        return mono_unknown_prefix(kind);
    return make_data_response(body);
}

static void* mono_download_worker(void* unused) {
    (void)unused;
    char directory[PATH_MAX], destination[PATH_MAX], temporary[PATH_MAX];
    mono_join(directory, sizeof(directory), mono_home(), "cache/wine-mono");
    mono_join(destination, sizeof(destination), directory, MONO_MSI);
    snprintf(temporary, sizeof(temporary), "%s.tmp", destination);
    (void)mono_mkdir_p(directory);
    pid_t pid = fork();
    int status = -1;
    if (pid == 0) {
        execl("/usr/bin/curl", "curl", "--fail", "--location", "--silent", "--show-error", "--output", temporary,
              MONO_URL, (char*)NULL);
        _exit(127);
    }
    if (pid > 0) {
        while (waitpid(pid, &status, 0) < 0 && errno == EINTR) {
        }
    }
    pthread_mutex_lock(&g_mono_lock);
    g_mono_state.downloading = false;
    if (pid > 0 && WIFEXITED(status) && WEXITSTATUS(status) == 0 && rename(temporary, destination) == 0) {
        g_mono_state.download_error[0] = '\0';
        struct stat st;
        if (stat(destination, &st) == 0)
            g_mono_state.download_bytes = g_mono_state.download_total = (unsigned long long)st.st_size;
    } else {
        snprintf(g_mono_state.download_error, sizeof(g_mono_state.download_error), "Wine Mono MSI download failed");
        (void)unlink(temporary);
    }
    pthread_mutex_unlock(&g_mono_lock);
    return NULL;
}

static bool mono_remove_tree(const char* path) {
    struct stat st;
    if (lstat(path, &st) != 0)
        return errno == ENOENT;
    if (!S_ISDIR(st.st_mode) || S_ISLNK(st.st_mode))
        return unlink(path) == 0;
    DIR* dir = opendir(path);
    if (dir == NULL)
        return false;
    bool ok = true;
    struct dirent* entry;
    while ((entry = readdir(dir)) != NULL) {
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0)
            continue;
        char child[PATH_MAX];
        if (!mono_join(child, sizeof(child), path, entry->d_name) || !mono_remove_tree(child)) {
            ok = false;
            break;
        }
    }
    closedir(dir);
    return ok && rmdir(path) == 0;
}

static bool mono_copy_file(const char* source, const char* destination) {
    FILE* input = fopen(source, "rb");
    if (input == NULL)
        return false;
    FILE* output = fopen(destination, "wb");
    if (output == NULL) {
        fclose(input);
        return false;
    }
    char buffer[65536];
    bool ok = true;
    size_t count;
    while ((count = fread(buffer, 1u, sizeof(buffer), input)) > 0u) {
        if (fwrite(buffer, 1u, count, output) != count) {
            ok = false;
            break;
        }
    }
    if (ferror(input))
        ok = false;
    if (fclose(input) != 0)
        ok = false;
    if (fclose(output) != 0)
        ok = false;
    return ok;
}

typedef struct {
    pid_t pid;
    char prefix[PATH_MAX];
    char log_path[PATH_MAX];
} MonoInstallWait;

static void* mono_install_reaper(void* opaque) {
    MonoInstallWait* wait = opaque;
    int status = 0;
    bool reaped = false;
    bool timed_out = false;
    for (unsigned int poll = 0u; poll < 1200u; poll++) {
        pid_t result = waitpid(wait->pid, &status, WNOHANG);
        if (result == wait->pid) {
            reaped = true;
            break;
        }
        if (result < 0 && errno != EINTR) {
            reaped = true;
            break;
        }
        usleep(500000u);
    }
    if (!reaped) {
        timed_out = true;
        (void)kill(wait->pid, SIGKILL);
        while (waitpid(wait->pid, &status, 0) < 0 && errno == EINTR) {
        }
        FILE* log = fopen(wait->log_path, "ab");
        if (log != NULL) {
            fprintf(log, "[metal-sharp] timeout: Wine Mono installer exceeded 600s budget\n");
            fclose(log);
        }
    }
    mono_stop_wineserver(wait->prefix);
    (void)mono_sweep_orphans(wait->prefix);
    if (!timed_out && WIFEXITED(status) && WEXITSTATUS(status) == 0) {
        char marker[PATH_MAX];
        if (mono_join(marker, sizeof(marker), wait->prefix, "drive_c/windows/mono/wine-mono-11.2.0"))
            (void)mono_mkdir_p(marker);
    }
    pthread_mutex_lock(&g_mono_lock);
    g_mono_state.running = false;
    g_mono_state.pid = 0;
    g_mono_state.started_at = 0u;
    if (timed_out) {
        snprintf(g_mono_state.last_error, sizeof(g_mono_state.last_error), "Wine Mono installer exceeded 600s budget");
    } else if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
        if (WIFEXITED(status))
            snprintf(g_mono_state.last_error, sizeof(g_mono_state.last_error),
                     "Wine Mono installer exited with Some(%d) but " MONO_VERSION " was not detected in %s",
                     WEXITSTATUS(status), wait->prefix);
        else
            snprintf(g_mono_state.last_error, sizeof(g_mono_state.last_error),
                     "Wine Mono installer exited with None but " MONO_VERSION " was not detected in %s", wait->prefix);
    }
    pthread_mutex_unlock(&g_mono_lock);
    free(wait);
    return NULL;
}

static bool mono_start_cached_installer(const char* kind, const char* prefix, pid_t* pid_out, char* error,
                                        size_t error_size) {
    char wine[PATH_MAX], drive_c[PATH_MAX], cache[PATH_MAX], staging[PATH_MAX], staged[PATH_MAX], logs[PATH_MAX];
    char log_path[PATH_MAX], mono2[PATH_MAX], installer_dir[PATH_MAX];
    mono_join(wine, sizeof(wine), mono_home(), "runtime/wine/bin/wine");
    if (!mono_file(wine)) {
        snprintf(error, error_size, "MetalSharp Wine not found: %s", wine);
        return false;
    }
    if (!mono_mkdir_p(prefix)) {
        snprintf(error, error_size, "create prefix dir: %s", strerror(errno));
        return false;
    }
    mono_join(drive_c, sizeof(drive_c), prefix, "drive_c");
    if (!mono_directory(drive_c)) {
        snprintf(error, error_size, "prefix not initialized (no drive_c): %s", prefix);
        return false;
    }
    mono_join(cache, sizeof(cache), mono_home(), "cache/wine-mono/" MONO_MSI);
    struct stat cache_stat;
    if (stat(cache, &cache_stat) != 0 || !S_ISREG(cache_stat.st_mode) || cache_stat.st_size < 1000000) {
        snprintf(error, error_size, "Wine Mono MSI not downloaded yet");
        return false;
    }
    mono_join(staging, sizeof(staging), prefix, ".ms-mono-install");
    if (!mono_mkdir_p(staging) || !mono_join(staged, sizeof(staged), staging, MONO_MSI) ||
        !mono_copy_file(cache, staged)) {
        snprintf(error, error_size, "stage mono msi: %s", strerror(errno));
        return false;
    }
    mono_join(logs, sizeof(logs), mono_home(), "logs");
    if (!mono_mkdir_p(logs)) {
        snprintf(error, error_size, "create log dir: %s", strerror(errno));
        return false;
    }
    int written = snprintf(log_path, sizeof(log_path), "%s/wine-mono-install-%s.log", logs, kind);
    if (written < 0 || (size_t)written >= sizeof(log_path)) {
        snprintf(error, error_size, "open wine-mono log: File name too long");
        return false;
    }
    FILE* log = fopen(log_path, "wb");
    if (log == NULL) {
        snprintf(error, error_size, "open wine-mono log: %s", strerror(errno));
        return false;
    }
    mono_join(mono2, sizeof(mono2), prefix, "drive_c/windows/mono/mono-2.0");
    bool had_existing = mono_directory(mono2);
    fprintf(log,
            "installer_kind=msi\nprefix=%s\nmsi=%s\nmono_version=" MONO_VERSION
            "\ntimeout=600s; reinstall_mode=vomus; reinstall=ALL\n--- wine output ---\n",
            prefix, staged);
    fflush(log);
    mono_stop_wineserver(prefix);
    (void)mono_sweep_orphans(prefix);
    (void)mono_remove_tree(mono2);
    mono_join(installer_dir, sizeof(installer_dir), prefix, "drive_c/windows/Installer");
    DIR* installers = opendir(installer_dir);
    if (installers != NULL) {
        struct dirent* entry;
        while ((entry = readdir(installers)) != NULL) {
            size_t length = strlen(entry->d_name);
            if (length >= 4u && strcmp(entry->d_name + length - 4u, ".msi") == 0) {
                char path[PATH_MAX];
                if (mono_join(path, sizeof(path), installer_dir, entry->d_name))
                    (void)unlink(path);
            }
        }
        closedir(installers);
    }
    int exec_pipe[2];
    if (pipe(exec_pipe) != 0) {
        fclose(log);
        snprintf(error, error_size, "spawn wine msiexec: %s", strerror(errno));
        return false;
    }
    (void)fcntl(exec_pipe[1], F_SETFD, FD_CLOEXEC);
    pid_t pid = fork();
    if (pid < 0) {
        fclose(log);
        close(exec_pipe[0]);
        close(exec_pipe[1]);
        snprintf(error, error_size, "spawn wine msiexec: %s", strerror(errno));
        return false;
    }
    if (pid == 0) {
        close(exec_pipe[0]);
        (void)dup2(fileno(log), STDOUT_FILENO);
        (void)dup2(fileno(log), STDERR_FILENO);
        fclose(log);
        (void)chdir(staging);
        (void)setenv("WINEPREFIX", prefix, 1);
        (void)setenv("WINEDEBUG", "-all", 1);
        char library_path[PATH_MAX * 2u];
        snprintf(library_path, sizeof(library_path), "%s/runtime/wine/lib:%s/runtime/wine/lib/wine/x86_64-unix",
                 mono_home(), mono_home());
#if defined(__APPLE__)
        (void)setenv("DYLD_FALLBACK_LIBRARY_PATH", library_path, 1);
#elif defined(__linux__)
        (void)setenv("LD_LIBRARY_PATH", library_path, 1);
#endif
        if (had_existing)
            execl(wine, wine, "msiexec", "/i", staged, "REINSTALLMODE=vomus", "REINSTALL=ALL", "/qn", (char*)NULL);
        else
            execl(wine, wine, "msiexec", "/i", staged, "/qn", (char*)NULL);
        int child_errno = errno;
        (void)write(exec_pipe[1], &child_errno, sizeof(child_errno));
        _exit(127);
    }
    fclose(log);
    close(exec_pipe[1]);
    int child_errno = 0;
    ssize_t exec_result;
    do {
        exec_result = read(exec_pipe[0], &child_errno, sizeof(child_errno));
    } while (exec_result < 0 && errno == EINTR);
    close(exec_pipe[0]);
    if (exec_result > 0) {
        while (waitpid(pid, NULL, 0) < 0 && errno == EINTR) {
        }
        snprintf(error, error_size, "spawn wine msiexec: %s", strerror(child_errno));
        return false;
    }
    pthread_mutex_lock(&g_mono_lock);
    g_mono_state.running = true;
    g_mono_state.pid = pid;
    g_mono_state.started_at = (unsigned long long)time(NULL);
    snprintf(g_mono_state.prefix, sizeof(g_mono_state.prefix), "%s", prefix);
    snprintf(g_mono_state.log_path, sizeof(g_mono_state.log_path), "%s", log_path);
    g_mono_state.last_error[0] = '\0';
    pthread_mutex_unlock(&g_mono_lock);
    MonoInstallWait* wait = calloc(1, sizeof(*wait));
    if (wait != NULL) {
        wait->pid = pid;
        snprintf(wait->prefix, sizeof(wait->prefix), "%s", prefix);
        snprintf(wait->log_path, sizeof(wait->log_path), "%s", log_path);
        pthread_t thread;
        if (pthread_create(&thread, NULL, mono_install_reaper, wait) == 0)
            pthread_detach(thread);
        else
            free(wait);
    }
    *pid_out = pid;
    return true;
}

static MetalsharpResponse* handle_mono_install(const HttpRequest* req) {
    char* kind = mono_body_prefix(req);
    if (kind == NULL)
        return NULL;
    char prefix[PATH_MAX], marker[PATH_MAX], cache[PATH_MAX], status_json[2048];
    if (!mono_prefix(kind, prefix, sizeof(prefix))) {
        MetalsharpResponse* response = mono_unknown_prefix(kind);
        free(kind);
        return response;
    }
    mono_join(marker, sizeof(marker), prefix, "drive_c/windows/mono/wine-mono-11.2.0");
    mono_join(cache, sizeof(cache), mono_home(), "cache/wine-mono/" MONO_MSI);
    if (mono_directory(marker)) {
        mono_status_json(kind, status_json, sizeof(status_json));
        char body[2300];
        snprintf(body, sizeof(body), "{\"ok\":true,\"alreadyInstalled\":true,\"status\":%s}", status_json);
        free(kind);
        return make_data_response(body);
    }
    pthread_mutex_lock(&g_mono_lock);
    bool downloading = g_mono_state.downloading;
    if (!downloading && !mono_file(cache)) {
        g_mono_state.downloading = true;
        g_mono_state.download_bytes = 0u;
        g_mono_state.download_total = 0u;
        g_mono_state.download_error[0] = '\0';
        pthread_t worker;
        if (pthread_create(&worker, NULL, mono_download_worker, NULL) == 0)
            pthread_detach(worker);
        else {
            g_mono_state.downloading = false;
            snprintf(g_mono_state.download_error, sizeof(g_mono_state.download_error), "Wine Mono MSI download failed");
        }
        downloading = g_mono_state.downloading;
    }
    pthread_mutex_unlock(&g_mono_lock);
    mono_status_json(kind, status_json, sizeof(status_json));
    char body[4096];
    if (downloading || !mono_file(cache)) {
        snprintf(body, sizeof(body), "{\"ok\":true,\"downloading\":true,\"status\":%s}", status_json);
    } else {
        pid_t pid = 0;
        char install_error[1024];
        if (mono_start_cached_installer(kind, prefix, &pid, install_error, sizeof(install_error))) {
            mono_status_json(kind, status_json, sizeof(status_json));
            snprintf(body, sizeof(body), "{\"ok\":true,\"pid\":%ld,\"status\":%s}", (long)pid, status_json);
        } else {
            pthread_mutex_lock(&g_mono_lock);
            g_mono_state.running = false;
            snprintf(g_mono_state.last_error, sizeof(g_mono_state.last_error), "%s", install_error);
            pthread_mutex_unlock(&g_mono_lock);
            mono_status_json(kind, status_json, sizeof(status_json));
            char escaped_error[2048];
            mono_json_escape(install_error, escaped_error, sizeof(escaped_error));
            snprintf(body, sizeof(body), "{\"ok\":false,\"error\":\"%s\",\"status\":%s}", escaped_error, status_json);
        }
    }
    free(kind);
    return make_data_response(body);
}

static void mono_stop_wineserver(const char* prefix) {
    char wineserver[PATH_MAX];
    mono_join(wineserver, sizeof(wineserver), mono_home(), "runtime/wine/bin/wineserver");
    if (!mono_file(wineserver))
        return;
    pid_t pid = fork();
    if (pid < 0)
        return;
    if (pid == 0) {
        (void)setenv("WINEPREFIX", prefix, 1);
        int null_fd = open("/dev/null", O_RDWR);
        if (null_fd >= 0) {
            (void)dup2(null_fd, STDOUT_FILENO);
            (void)dup2(null_fd, STDERR_FILENO);
        }
        execl(wineserver, wineserver, "-k", (char*)NULL);
        _exit(127);
    }
    while (waitpid(pid, NULL, 0) < 0 && errno == EINTR) {
    }
}

static unsigned int mono_sweep_orphans(const char* prefix) {
    FILE* process_list = popen("/bin/ps -axo pid=,command=", "r");
    if (process_list == NULL)
        return 0u;
    unsigned int killed = 0u;
    char* line = NULL;
    size_t capacity = 0u;
    while (getline(&line, &capacity, process_list) >= 0) {
        char* cursor = line;
        while (*cursor == ' ' || *cursor == '\t')
            cursor++;
        char* end = NULL;
        long pid = strtol(cursor, &end, 10);
        if (end == cursor || pid <= 0 || pid == (long)getpid())
            continue;
        while (*end == ' ' || *end == '\t')
            end++;
        bool msiexec_orphan =
            strstr(end, "msiexec") != NULL && (strstr(end, "REMOVE=ALL") != NULL || strstr(end, prefix) != NULL);
        bool start_wrapper = strstr(end, "/exec msiexec") != NULL && strstr(end, ".ms-mono-install") != NULL;
        bool winedevice = strstr(end, "winedevice") != NULL;
        bool in_prefix =
            strstr(end, prefix) != NULL &&
            (strstr(end, "wine") != NULL || strstr(end, "msiexec") != NULL || strstr(end, "winedevice") != NULL);
        if (!msiexec_orphan && !start_wrapper && !winedevice && !in_prefix)
            continue;
        (void)kill((pid_t)pid, SIGTERM);
        usleep(50000u);
        if (kill((pid_t)pid, 0) == 0)
            (void)kill((pid_t)pid, SIGKILL);
        killed++;
    }
    free(line);
    (void)pclose(process_list);
    return killed;
}

static MetalsharpResponse* handle_mono_reset(const HttpRequest* req) {
    char* kind = mono_body_prefix(req);
    if (kind == NULL)
        return NULL;
    char prefix[PATH_MAX], escaped_prefix[PATH_MAX * 2u], status_json[2048], body[PATH_MAX * 2u + 2300u];
    if (!mono_prefix(kind, prefix, sizeof(prefix))) {
        char message[512];
        snprintf(message, sizeof(message), "unknown prefix kind: %s", kind);
        pthread_mutex_lock(&g_mono_lock);
        g_mono_state.running = false;
        g_mono_state.pid = 0;
        g_mono_state.started_at = 0u;
        snprintf(g_mono_state.last_error, sizeof(g_mono_state.last_error), "%s", message);
        pthread_mutex_unlock(&g_mono_lock);
        MetalsharpResponse* response = mono_unknown_prefix(kind);
        free(kind);
        return response;
    }
    mono_stop_wineserver(prefix);
    unsigned int killed = mono_sweep_orphans(prefix);
    pthread_mutex_lock(&g_mono_lock);
    g_mono_state.running = false;
    g_mono_state.pid = 0;
    g_mono_state.started_at = 0u;
    g_mono_state.last_error[0] = '\0';
    pthread_mutex_unlock(&g_mono_lock);
    mono_status_json(kind, status_json, sizeof(status_json));
    mono_json_escape(prefix, escaped_prefix, sizeof(escaped_prefix));
    snprintf(body, sizeof(body), "{\"ok\":true,\"killedProcesses\":%u,\"prefix\":\"%s\",\"status\":%s}", killed,
             escaped_prefix, status_json);
    free(kind);
    return make_data_response(body);
}

void mono_register_routes(HttpServer* server, Database* db) {
    (void)db;
    if (server == NULL) {
        LOG_WARN("mono_register_routes called with NULL server");
        return;
    }
    http_server_register(server, "GET", "/wine-mono/status", handle_mono_status);
    http_server_register(server, "POST", "/wine-mono/install", handle_mono_install);
    http_server_register(server, "POST", "/wine-mono/reset", handle_mono_reset);
    LOG_INFO("wine-mono routes registered (3)");
}
