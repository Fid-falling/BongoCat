#ifndef BONGO_CAT_NEO_PATH_H
#define BONGO_CAT_NEO_PATH_H

#include "bongo_cat_neo/common.h"

#ifdef __cplusplus
extern "C" {
#endif

bool bongo_cat_neo_path_join(char *output, size_t capacity, const char *left, const char *right);
const char *bongo_cat_neo_path_name(const char *path);
bool bongo_cat_neo_path_is_file(const char *path);
bool bongo_cat_neo_path_is_dir(const char *path);
bool bongo_cat_neo_path_find_suffix(const char *directory, const char *suffix,
    char *name, size_t capacity);
int bongo_cat_neo_path_find_unique_suffix(const char *directory, const char *suffix,
    char *name, size_t capacity);

#ifdef __cplusplus
}
#endif

#endif
