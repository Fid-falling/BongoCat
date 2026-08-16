#include "model_import.h"
#include "bongo_cat/path.h"

#include <SDL3/SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define IMPORT_CHILD_CAP 64
#define IMPORT_ENTRY_CAP 256
#define IMPORT_NODE_CAP 256
#define IMPORT_SCAN_DEPTH 3
#define IMPORT_SCAN_BUDGET_NS 2000000000ull

typedef struct ChildList {
    char paths[IMPORT_CHILD_CAP][BONGO_CAT_PATH_CAP];
    size_t count, entries;
    bool limited;
} ChildList;

typedef struct ImportWorkspace {
    BongoCatImportDiscovery discovery;
    ChildList children;
} ImportWorkspace;

typedef struct ImportScan {
    BongoCatImportVisitor visitor;
    void *userdata;
    BongoCatError *error;
    size_t nodes;
    uint64_t deadline;
    bool limited;
} ImportScan;

typedef struct ImportQueue {
    char paths[IMPORT_NODE_CAP][BONGO_CAT_PATH_CAP];
    unsigned char depths[IMPORT_NODE_CAP];
    size_t head, count;
} ImportQueue;

static BongoCatPathVisit collect_child(void *userdata,
    const char *dirname, const char *name) {
    ChildList *list = userdata;
    if (++list->entries > IMPORT_ENTRY_CAP || list->count >= IMPORT_CHILD_CAP) {
        list->limited = true; return BONGO_CAT_PATH_SUCCESS;
    }
    if (name[0] == '.') return BONGO_CAT_PATH_CONTINUE;
    char path[BONGO_CAT_PATH_CAP];
    if (bongo_cat_path_join(path, sizeof(path), dirname, name) &&
        bongo_cat_path_is_dir(path))
        snprintf(list->paths[list->count++], BONGO_CAT_PATH_CAP, "%s", path);
    return BONGO_CAT_PATH_CONTINUE;
}

static int compare_path(const void *left, const void *right) {
#ifdef _WIN32
    return SDL_strcasecmp(left, right);
#else
    return strcmp(left, right);
#endif
}

static bool within_budget(ImportScan *scan) {
    if (scan->nodes >= IMPORT_NODE_CAP || SDL_GetTicksNS() >= scan->deadline) {
        scan->limited = true; return false;
    }
    scan->nodes++; return true;
}

static void enqueue_children(ImportScan *scan, ImportQueue *queue,
    const ChildList *children, int depth) {
    for (size_t i = 0; i < children->count; ++i) {
        if (queue->count >= IMPORT_NODE_CAP) {
            scan->limited = true; return;
        }
        snprintf(queue->paths[queue->count], BONGO_CAT_PATH_CAP, "%s",
            children->paths[i]);
        queue->depths[queue->count++] = (unsigned char)depth;
    }
}

static BongoCatResult scan_node(ImportScan *scan, ImportQueue *queue,
    const char *source, int depth) {
    if (!within_budget(scan)) return BONGO_CAT_OK;
    ImportWorkspace *work = calloc(1, sizeof(*work));
    if (!work) {
        bongo_cat_error_set(scan->error, BONGO_CAT_ERROR_MEMORY,
            "Cannot allocate model import scan workspace");
        return BONGO_CAT_ERROR_MEMORY;
    }
    BongoCatError local = {0};
    int found = bongo_cat_import_mver_discover_exact(source, &work->discovery, &local);
    if (found <= 0) {
        memset(&work->discovery, 0, sizeof(work->discovery));
        local = (BongoCatError){0};
        found = bongo_cat_import_mver_patch_discover_exact(source,
            &work->discovery, &local);
    }
    if (found <= 0) {
        memset(&work->discovery, 0, sizeof(work->discovery));
        local = (BongoCatError){0};
        found = bongo_cat_import_tauri_discover_exact(source,
            &work->discovery, &local);
    }
    if (found > 0) {
        uint64_t started = SDL_GetTicksNS();
        BongoCatResult result = scan->visitor(scan->userdata, source,
            &work->discovery, scan->error);
        uint64_t finished = SDL_GetTicksNS(), elapsed = finished - started;
        if (scan->deadline <= UINT64_MAX - elapsed) scan->deadline += elapsed;
        else scan->deadline = UINT64_MAX;
        free(work); return result;
    }
    if (depth < IMPORT_SCAN_DEPTH &&
        bongo_cat_path_enumerate(source, collect_child, &work->children)) {
        if (work->children.limited) scan->limited = true;
        qsort(work->children.paths, work->children.count,
            sizeof(work->children.paths[0]), compare_path);
        enqueue_children(scan, queue, &work->children, depth + 1);
    }
    free(work); return BONGO_CAT_OK;
}

static BongoCatResult scan_with_budget(const char *root,
    BongoCatImportVisitor visitor, void *userdata, uint64_t budget_ns,
    BongoCatError *error) {
    if (!root || !visitor) return BONGO_CAT_ERROR_ARGUMENT;
    ImportWorkspace *work = calloc(1, sizeof(*work));
    ImportQueue *queue = calloc(1, sizeof(*queue));
    if (!work || !queue) { free(work); free(queue); return BONGO_CAT_ERROR_MEMORY; }
    uint64_t now = SDL_GetTicksNS();
    uint64_t deadline = budget_ns <= UINT64_MAX - now
        ? now + budget_ns : UINT64_MAX;
    ImportScan scan = {visitor, userdata, error, 0, deadline, false};
    BongoCatResult result = BONGO_CAT_OK;
    if (bongo_cat_path_enumerate(root, collect_child, &work->children)) {
        if (work->children.limited) scan.limited = true;
        qsort(work->children.paths, work->children.count,
            sizeof(work->children.paths[0]), compare_path);
        enqueue_children(&scan, queue, &work->children, 1);
        while (queue->head < queue->count) {
            size_t current = queue->head++;
            BongoCatResult item = scan_node(&scan, queue,
                queue->paths[current], queue->depths[current]);
            if (item != BONGO_CAT_OK && result == BONGO_CAT_OK) result = item;
        }
    }
    if (scan.limited) SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
        "Model folder scan reached its directory budget");
    free(queue); free(work); return result;
}

BongoCatResult bongo_cat_import_scan_budget(const char *root,
    BongoCatImportVisitor visitor, void *userdata, uint64_t budget_ns,
    BongoCatError *error) {
    return scan_with_budget(root, visitor, userdata, budget_ns, error);
}

BongoCatResult bongo_cat_import_scan(const char *root,
    BongoCatImportVisitor visitor, void *userdata, BongoCatError *error) {
    return scan_with_budget(root, visitor, userdata,
        IMPORT_SCAN_BUDGET_NS, error);
}
