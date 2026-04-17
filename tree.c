// tree.c — Tree object serialization and construction

#include "tree.h"
#include "index.h"
#include "pes.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <sys/stat.h>

int object_write(ObjectType type, const void *data, size_t len, ObjectID *id_out);

#define MODE_FILE  0100644
#define MODE_EXEC  0100755
#define MODE_DIR   0040000

// ------------------------------------------------------------
// tree_parse
// ------------------------------------------------------------

int tree_parse(const void *data, size_t len, Tree *tree_out) {
    tree_out->count = 0;
    const uint8_t *ptr = (const uint8_t *)data;
    const uint8_t *end = ptr + len;

    while (ptr < end && tree_out->count < MAX_TREE_ENTRIES) {
        TreeEntry *entry = &tree_out->entries[tree_out->count];

        const uint8_t *space = memchr(ptr, ' ', end - ptr);
        if (!space) return -1;

        char mode_str[16] = {0};
        size_t mode_len = space - ptr;
        if (mode_len >= sizeof(mode_str)) return -1;
        memcpy(mode_str, ptr, mode_len);
        entry->mode = strtol(mode_str, NULL, 8);

        ptr = space + 1;

        const uint8_t *null_byte = memchr(ptr, '\0', end - ptr);
        if (!null_byte) return -1;

        size_t name_len = null_byte - ptr;
        if (name_len >= sizeof(entry->name)) return -1;
        memcpy(entry->name, ptr, name_len);
        entry->name[name_len] = '\0';

        ptr = null_byte + 1;

        if (ptr + HASH_SIZE > end) return -1;
        memcpy(entry->hash.hash, ptr, HASH_SIZE);
        ptr += HASH_SIZE;

        tree_out->count++;
    }
    return 0;
}

// ------------------------------------------------------------
// tree_serialize
// ------------------------------------------------------------

static int compare_tree_entries(const void *a, const void *b) {
    return strcmp(((const TreeEntry *)a)->name,
                  ((const TreeEntry *)b)->name);
}

int tree_serialize(const Tree *tree, void **data_out, size_t *len_out) {

    size_t max_size = tree->count * 296;
    uint8_t *buffer = malloc(max_size);
    if (!buffer) return -1;

    Tree sorted_tree = *tree;
    qsort(sorted_tree.entries,
          sorted_tree.count,
          sizeof(TreeEntry),
          compare_tree_entries);

    size_t offset = 0;

    for (int i = 0; i < sorted_tree.count; i++) {

        const TreeEntry *entry = &sorted_tree.entries[i];

        int written = sprintf((char *)buffer + offset,
                              "%o %s",
                              entry->mode,
                              entry->name);

        offset += written + 1;

        memcpy(buffer + offset,
               entry->hash.hash,
               HASH_SIZE);

        offset += HASH_SIZE;
    }

    *data_out = buffer;
    *len_out = offset;
    return 0;
}

// ------------------------------------------------------------
// recursive build_tree
// ------------------------------------------------------------

static int build_tree(IndexEntry *entries,
                      int count,
                      const char *prefix,
                      ObjectID *id_out) {

    Tree tree;
    tree.count = 0;

    size_t prefix_len = strlen(prefix);

    for (int i = 0; i < count; i++) {

        if (strncmp(entries[i].path, prefix, prefix_len) != 0)
            continue;

        const char *rest = entries[i].path + prefix_len;
        if (*rest == '/')
            rest++;

        const char *slash = strchr(rest, '/');

        if (!slash) {

            if (tree.count >= MAX_TREE_ENTRIES)
                return -1;

            TreeEntry *entry = &tree.entries[tree.count++];
            entry->mode = entries[i].mode;
            entry->hash = entries[i].hash;
            strncpy(entry->name, rest, sizeof(entry->name));
            entry->name[sizeof(entry->name) - 1] = '\0';
        }
        else {

            char dirname[256];
            size_t len = slash - rest;
            strncpy(dirname, rest, len);
            dirname[len] = '\0';

            int exists = 0;
            for (int j = 0; j < tree.count; j++) {
                if (strcmp(tree.entries[j].name, dirname) == 0) {
                    exists = 1;
                    break;
                }
            }

            if (!exists) {

                if (tree.count >= MAX_TREE_ENTRIES)
                    return -1;

                char new_prefix[512];
                snprintf(new_prefix, sizeof(new_prefix),
                         "%s%s/", prefix, dirname);

                ObjectID sub_id;
                if (build_tree(entries,
                               count,
                               new_prefix,
                               &sub_id) != 0)
                    return -1;

                TreeEntry *entry = &tree.entries[tree.count++];
                entry->mode = MODE_DIR;
                entry->hash = sub_id;
                strncpy(entry->name, dirname,
                        sizeof(entry->name));
                entry->name[sizeof(entry->name) - 1] = '\0';
            }
        }
    }

    void *data;
    size_t len;

    if (tree_serialize(&tree, &data, &len) != 0)
        return -1;

    int rc = object_write(OBJ_TREE, data, len, id_out);
    free(data);
    return rc;
}

// ------------------------------------------------------------
// tree_from_index
// ------------------------------------------------------------

int tree_from_index(ObjectID *id_out) {

    Index index;

    if (index_load(&index) != 0) {
        fprintf(stderr, "error: failed to load index\n");
        return -1;
    }

    return build_tree(index.entries,
                      index.count,
                      "",
                      id_out);
}
