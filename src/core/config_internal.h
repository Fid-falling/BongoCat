#ifndef BONGO_CAT_NEO_CONFIG_INTERNAL_H
#define BONGO_CAT_NEO_CONFIG_INTERNAL_H

#include "bongo_cat_neo/config.h"
#include <yyjson.h>

yyjson_doc *bongo_cat_neo_config_read_document(const char *path,
    const char *format, BongoCatNeoError *error);
BongoCatNeoResult bongo_cat_neo_config_write_document(const char *path,
    yyjson_mut_doc *document, const char *description, BongoCatNeoError *error);

#endif
