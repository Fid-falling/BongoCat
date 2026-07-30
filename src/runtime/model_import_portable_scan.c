#include "model_import.h"
#include "bongo_cat/path.h"

#include <SDL3/SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define PORTABLE_CHILD_CAP 32
#define PORTABLE_ENTRY_CAP 256
#define PORTABLE_NODE_CAP 256
#define PORTABLE_SCAN_DEPTH 4
#define PORTABLE_SCAN_BUDGET_NS 500000000ull

typedef struct ChildList {
    char paths[PORTABLE_CHILD_CAP][BONGO_CAT_PATH_CAP];
    size_t count, entries;
    bool limited;
} ChildList;

typedef struct PortableWorkspace {
    BongoCatImportDiscovery discovery;
    ChildList children;
} PortableWorkspace;

typedef struct PortableScan {
    BongoCatPortableVisitor visitor;
    void *userdata;
    BongoCatError *error;
    size_t nodes;
    uint64_t deadline;
    bool limited;
} PortableScan;

static BongoCatPathVisit collect_child(void *userdata,
    const char *dirname, const char *name) {
    ChildList *list = userdata;
    if (++list->entries > PORTABLE_ENTRY_CAP || list->count >= PORTABLE_CHILD_CAP) {
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

static bool within_budget(PortableScan *scan) {
    if (scan->nodes >= PORTABLE_NODE_CAP || SDL_GetTicksNS() >= scan->deadline) {
        scan->limited = true; return false;
    }
    scan->nodes++; return true;
}

static BongoCatResult scan_node(PortableScan *scan, const char *source, int depth) {
    if (!within_budget(scan)) return BONGO_CAT_OK;
    PortableWorkspace *work = calloc(1, sizeof(*work));
    if (!work) {
        bongo_cat_error_set(scan->error, BONGO_CAT_ERROR_MEMORY,
            "Cannot allocate portable model scan workspace");
        return BONGO_CAT_ERROR_MEMORY;
    }
    BongoCatError local = {0};
    int found = bongo_cat_import_mver_discover_exact(source, &work->discovery, &local);
    if (found <= 0) {
        memset(&work->discovery, 0, sizeof(work->discovery)); local = (BongoCatError){0};
        found = bongo_cat_import_mver_patch_discover(source, &work->discovery, &local);
    }
    if (found > 0) {
        BongoCatResult result = scan->visitor(scan->userdata, source,
            &work->discovery, scan->error);
        free(work); return result;
    }
    BongoCatResult result = BONGO_CAT_OK;
    if (depth < PORTABLE_SCAN_DEPTH &&
        bongo_cat_path_enumerate(source, collect_child, &work->children)) {
        if (work->children.limited) scan->limited = true;
        qsort(work->children.paths, work->children.count,
            sizeof(work->children.paths[0]), compare_path);
        for (size_t i = 0; i < work->children.count; ++i) {
            BongoCatResult item = scan_node(scan, work->children.paths[i], depth + 1);
            if (item != BONGO_CAT_OK && result == BONGO_CAT_OK) result = item;
        }
    }
    free(work); return result;
}

BongoCatResult bongo_cat_import_portable_scan(const char *root,
    BongoCatPortableVisitor visitor, void *userdata, BongoCatError *error) {
    if (!root || !visitor) return BONGO_CAT_ERROR_ARGUMENT;
    ChildList *children = calloc(1, sizeof(*children));
    if (!children) return BONGO_CAT_ERROR_MEMORY;
    PortableScan scan = {visitor, userdata, error, 0,
        SDL_GetTicksNS() + PORTABLE_SCAN_BUDGET_NS, false};
    BongoCatResult result = BONGO_CAT_OK;
    if (bongo_cat_path_enumerate(root, collect_child, children)) {
        if (children->limited) scan.limited = true;
        qsort(children->paths, children->count, sizeof(children->paths[0]), compare_path);
        for (size_t i = 0; i < children->count; ++i) {
            BongoCatResult item = scan_node(&scan, children->paths[i], 1);
            if (item != BONGO_CAT_OK && result == BONGO_CAT_OK) result = item;
        }
    }
    if (scan.limited) SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
        "Portable model discovery reached its startup scan budget");
    free(children); return result;
}
