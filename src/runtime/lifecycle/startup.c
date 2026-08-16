#include "runtime.h"
#include "runtime_state.h"
#include "bongo_cat/file.h"
#include "bongo_cat/path.h"
#include "storage_paths.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#ifdef _WIN32
#include <windows.h>
#endif

static char startup_log_path[BONGO_CAT_PATH_CAP];
static bool startup_is_ready;
static bool verbose_logging;
static SDL_Mutex *log_mutex;

static void append_log(const char *path, const char *timestamp,
    int category, SDL_LogPriority priority, const char *message) {
    FILE *file = path && path[0] ? bongo_cat_file_open(path, "ab") : NULL;
    if (!file) return;
    fprintf(file, "%s [%d:%d] %s\n", timestamp ? timestamp : "unknown-time", category, (int)priority, message ? message : "");
    fclose(file);
}

static bool essential_info(const char *message) {
    return message && strncmp(message, "[runtime] ", 10) == 0;
}

static void SDLCALL log_output(void *userdata, int category,
    SDL_LogPriority priority, const char *message) {
    (void)userdata;
    if (!verbose_logging && priority < SDL_LOG_PRIORITY_WARN &&
        !essential_info(message)) return;
    if (log_mutex) SDL_LockMutex(log_mutex);
    char timestamp[64];
    bongo_cat_runtime_timestamp(timestamp, sizeof(timestamp));
    append_log(startup_log_path, timestamp, category, priority, message);
    fprintf(stderr, "%s [%d:%d] %s\n", timestamp, category, (int)priority, message ? message : "");
    if (log_mutex) SDL_UnlockMutex(log_mutex);
}

static void format_shutdown_timestamp(char *target, size_t capacity) {
    time_t now = time(NULL);
    struct tm *utc = gmtime(&now);
    if (!utc || !strftime(target, capacity, "%Y-%m-%d %H:%M:%S UTC", utc))
        snprintf(target, capacity, "%lld", (long long)now);
    target[capacity - 1] = '\0';
}

void bongo_cat_runtime_clean_shutdown(BongoCatApp *app, int exit_code) {
    char timestamp[64], message[80];
    format_shutdown_timestamp(timestamp, sizeof(timestamp));
    snprintf(message, sizeof(message),
        "[runtime] Shutdown complete: exit_code=%d", exit_code);
    append_log(startup_log_path, timestamp, SDL_LOG_CATEGORY_APPLICATION,
        SDL_LOG_PRIORITY_INFO, message);
    fprintf(stderr, "%s [%d:%d] %s\n", timestamp,
        SDL_LOG_CATEGORY_APPLICATION, SDL_LOG_PRIORITY_INFO, message);

    bongo_cat_runtime_state_clean(app, timestamp);
}

void bongo_cat_runtime_log_stop(void) {
    if (!log_mutex) return;
    SDL_Mutex *mutex = log_mutex;
    log_mutex = NULL;
    SDL_DestroyMutex(mutex);
}

static bool reset_log(const char *path) {
    FILE *file = path && path[0] ? bongo_cat_file_open(path, "wb") : NULL;
    if (!file) return false;
    char timestamp[64];
    bongo_cat_runtime_timestamp(timestamp, sizeof(timestamp));
    fprintf(file, "%s BongoCat %s runtime log\n", timestamp, BONGO_CAT_VERSION);
    return fclose(file) == 0;
}

static bool store_argument(char *target, size_t capacity, const char *value,
    const char *name, BongoCatError *error) {
    int length = snprintf(target, capacity, "%s", value ? value : "");
    if (length >= 0 && (size_t)length < capacity) return true;
    bongo_cat_error_set(error, BONGO_CAT_ERROR_ARGUMENT,
        "%s path is too long", name);
    return false;
}

static bool parse_arguments(BongoCatApp *app, int argc, char **argv,
    BongoCatError *error) {
    for (int i = 1; i < argc; ++i) {
        const char *arg = argv[i];
        if (strcmp(arg, "--autostart") == 0) app->autostart_launch = true;
        else if (strcmp(arg, "--ci-smoke") == 0) app->smoke = true;
        else if (strcmp(arg, "--ci-preferences") == 0) app->smoke_preferences = true;
        else if (strcmp(arg, "--ci-preference-shortcut") == 0)
            app->smoke_preference_shortcut = true;
        else if (strcmp(arg, "--ci-preference-model-select") == 0)
            app->smoke_preference_model_select = true;
        else if (strcmp(arg, "--ci-remove-imported") == 0) app->smoke_remove_imported = true;
        else if (strcmp(arg, "--ci-shortcuts") == 0) app->smoke_shortcuts = true;
        else if (strcmp(arg, "--ci-menu") == 0) app->smoke_menu = true;
        else if (strcmp(arg, "--ci-input-audit") == 0) app->smoke_input_audit = true;
        else if (strcmp(arg, "--ci-ignore-global-input") == 0) app->smoke_ignore_global_input = true;
        else if (strcmp(arg, "--ci-pass-through") == 0) app->smoke_pass_through = true;
        else if (strcmp(arg, "--ci-context-menu") == 0) app->smoke_context_menu = true;
        else if (strcmp(arg, "--ci-frame-series") == 0) app->smoke_frame_series = true;
        else if (strcmp(arg, "--ci-runtime-flow") == 0) app->smoke_runtime_flow = true;
        else if (strcmp(arg, "--ci-freeze-model") == 0) app->smoke_freeze_model = true;
        else if (strncmp(arg, "--ci-preference-page=", 21) == 0) {
            int page = atoi(arg + 21);
            if (page >= 0 && page < 5) app->smoke_preference_page = page;
        } else if (strncmp(arg, "--ci-language=", 14) == 0) {
            const char *name = arg + 14;
            for (int value = 0; value < BONGO_CAT_LANG_COUNT; ++value)
                if (strcmp(name, bongo_cat_language_name((BongoCatLanguage)value)) == 0)
                    app->smoke_language = value;
        } else if (strncmp(arg, "--ci-theme=", 11) == 0) {
            const char *name = arg + 11;
            for (int value = 0; value <= BONGO_CAT_THEME_DARK; ++value)
                if (strcmp(name, bongo_cat_theme_name((BongoCatTheme)value)) == 0)
                    app->smoke_theme = value;
        } else if (strncmp(arg, "--ci-exit-ms=", 13) == 0) {
            uint64_t delay = strtoull(arg + 13, NULL, 10);
            app->smoke_deadline_ns = delay > UINT64_MAX / 1000000ull
                ? UINT64_MAX : delay * 1000000ull;
        } else if (strncmp(arg, "--storage-root=", 15) == 0) {
            if (!store_argument(app->storage_root, sizeof(app->storage_root),
                arg + 15, "Storage", error)) return false;
        } else if (strncmp(arg, "--ci-import=", 12) == 0) {
            if (!store_argument(app->smoke_import_path, sizeof(app->smoke_import_path),
                arg + 12, "Import", error)) return false;
        } else if (strncmp(arg, "--ci-model=", 11) == 0) {
            if (!store_argument(app->smoke_model, sizeof(app->smoke_model),
                arg + 11, "Model", error)) return false;
        } else if (strncmp(arg, "--ci-viewer-trace=", 18) == 0) {
            if (!store_argument(app->smoke_viewer_trace,
                sizeof(app->smoke_viewer_trace), arg + 18,
                "Viewer trace", error)) return false;
        } else if (strncmp(arg, "--ci-live2d-scenario=", 21) == 0 &&
            !store_argument(app->smoke_live2d_scenario,
                sizeof(app->smoke_live2d_scenario), arg + 21, "Scenario", error)) return false;
    }
    if (app->smoke && !app->smoke_deadline_ns) app->smoke_deadline_ns = 1500000000ull;
    return true;
}

static void begin_log(BongoCatApp *app) {
    char previous[BONGO_CAT_PATH_CAP], stage_path[BONGO_CAT_PATH_CAP];
    char stage_temporary[BONGO_CAT_PATH_CAP];
    char interrupted[128] = {0};
    char unexpected[256] = {0};
    bongo_cat_runtime_state_previous(app, unexpected, sizeof(unexpected));
    bongo_cat_path_join(startup_log_path, sizeof(startup_log_path),
        app->log_root, "startup.log");
    bongo_cat_path_join(previous, sizeof(previous), app->log_root,
        "startup-previous.log");
    bool stage_paths_ready = bongo_cat_path_join(stage_path,
        sizeof(stage_path), app->state_root, "startup-stage.txt") &&
        bongo_cat_path_join(stage_temporary, sizeof(stage_temporary),
            app->state_root, "startup-stage.tmp");
    if (stage_paths_ready && !bongo_cat_path_is_file(stage_path) &&
        bongo_cat_path_is_file(stage_temporary))
        bongo_cat_file_replace(stage_temporary, stage_path, true);
    else if (stage_paths_ready) bongo_cat_file_remove(stage_temporary);
    FILE *stage = stage_paths_ready ? bongo_cat_file_open(stage_path, "rb")
        : NULL;
    if (stage) {
        size_t length = fread(interrupted, 1, sizeof(interrupted) - 1, stage);
        interrupted[ferror(stage) ? 0 : length] = '\0';
        fclose(stage);
    }
    if (bongo_cat_path_is_file(startup_log_path))
        bongo_cat_file_replace(startup_log_path, previous, true);
    reset_log(startup_log_path);
    verbose_logging = app->smoke;
    if (!log_mutex) log_mutex = SDL_CreateMutex();
    SDL_SetLogOutputFunction(log_output, NULL);
    SDL_Log("[runtime] Startup on %s; storage=%s", SDL_GetPlatform(), app->storage_root[0]
        ? app->storage_root : app->config_root);
    if (interrupted[0]) SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
        "Previous startup ended before readiness at stage: %s", interrupted);
    if (unexpected[0] && !strstr(unexpected, "stage=clean-shutdown"))
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
            "Previous run ended unexpectedly (crash, forced close, or hang): %s",
            unexpected);
}

bool bongo_cat_startup_prepare(BongoCatApp *app, int argc, char **argv,
    BongoCatError *error) {
    if (!parse_arguments(app, argc, argv, error) ||
        !bongo_cat_storage_paths_prepare(app, error)) return false;
    begin_log(app); bongo_cat_startup_stage(app, "paths-ready"); return true;
}

void bongo_cat_startup_stage(BongoCatApp *app, const char *stage) {
    if (!app || !app->state_root[0] || !stage) return;
    char path[BONGO_CAT_PATH_CAP], temporary[BONGO_CAT_PATH_CAP];
    if (bongo_cat_path_join(path, sizeof(path), app->state_root,
        "startup-stage.txt") && bongo_cat_path_join(temporary,
        sizeof(temporary), app->state_root, "startup-stage.tmp")) {
        FILE *file = bongo_cat_file_open(temporary, "wb");
        bool written = file && fputs(stage, file) >= 0;
        if (file && fclose(file) != 0) written = false;
        if (!written || !bongo_cat_file_replace(temporary, path, true))
            bongo_cat_file_remove(temporary);
    }
    char runtime_stage[160];
    snprintf(runtime_stage, sizeof(runtime_stage), "startup:%s", stage);
    bongo_cat_runtime_stage(app, runtime_stage);
    SDL_Log("[runtime] Startup stage: %s", stage);
}

void bongo_cat_startup_ready(BongoCatApp *app) {
    if (!app || startup_is_ready) return;
    startup_is_ready = true;
    char path[BONGO_CAT_PATH_CAP];
    if (bongo_cat_path_join(path, sizeof(path), app->state_root,
        "startup-stage.txt")) bongo_cat_file_remove(path);
    if (bongo_cat_path_join(path, sizeof(path), app->state_root,
        "startup-stage.tmp")) bongo_cat_file_remove(path);
    if (bongo_cat_path_join(path, sizeof(path), app->log_root,
        "startup-error.log")) bongo_cat_file_remove(path);
    bongo_cat_runtime_stage(app, "running");
    SDL_Log("[runtime] Startup ready");
}

static void write_error(BongoCatApp *app, const char *name, const char *message) {
    if (!app || !app->log_root[0]) return;
    char path[BONGO_CAT_PATH_CAP];
    if (!bongo_cat_path_join(path, sizeof(path), app->log_root, name)) return;
    FILE *file = bongo_cat_file_open(path, "wb");
    if (file) { fputs(message, file); fputc('\n', file); fclose(file); }
}

#ifdef _WIN32
static void native_error_box(const char *message) {
    int count = MultiByteToWideChar(CP_UTF8, 0, message, -1, NULL, 0);
    wchar_t *wide = count > 0 ? calloc((size_t)count, sizeof(*wide)) : NULL;
    if (wide) {
        MultiByteToWideChar(CP_UTF8, 0, message, -1, wide, count);
        MessageBoxW(NULL, wide, BONGO_CAT_NAME_W, MB_OK | MB_ICONERROR);
        free(wide);
    }
}
#endif

void bongo_cat_startup_failure(BongoCatApp *app, const BongoCatError *error) {
    const char *message = error && error->message[0] ? error->message : "Initialization failed";
    if (app) { bongo_cat_startup_stage(app, "failed"); write_error(app, "startup-error.log", message); }
    SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Startup failed: %s", message);
    char body[BONGO_CAT_PATH_CAP + 384];
    snprintf(body, sizeof(body), "BongoCat could not start.\n\n%s%s%s", message,
        app && app->log_root[0] ? "\n\nDiagnostic log:\n" : "",
        app && app->log_root[0] ? startup_log_path : "");
    if (app && app->smoke) return;
    if (!SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, BONGO_CAT_NAME, body,
        app ? app->window : NULL)) {
#ifdef _WIN32
        native_error_box(body);
#endif
    }
}

void bongo_cat_startup_ci_failure(BongoCatApp *app,
    const BongoCatError *error) {
    if (!app) return;
    app->exit_code = 1;
    write_error(app, "ci-error.log", error && error->message[0]
        ? error->message : "CI operation failed");
}
