// index.c — Staging area implementation

#include "index.h"
#include "pes.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <dirent.h>

// ─── PROVIDED ───────────────────────────────────────────────────────────────

IndexEntry* index_find(Index *index, const char *path) {
    for (int i = 0; i < index->count; i++) {
        if (strcmp(index->entries[i].path, path) == 0)
            return &index->entries[i];
    }
    return NULL;
}

int index_remove(Index *index, const char *path) {
    for (int i = 0; i < index->count; i++) {
        if (strcmp(index->entries[i].path, path) == 0) {
            int remaining = index->count - i - 1;
            if (remaining > 0)
                memmove(&index->entries[i], &index->entries[i + 1],
                        remaining * sizeof(IndexEntry));
            index->count--;
            return index_save(index);
        }
    }
    fprintf(stderr, "error: '%s' is not in the index\n", path);
    return -1;
}

int index_status(const Index *index) {
    printf("Staged changes:\n");
    int staged_count = 0;

    for (int i = 0; i < index->count; i++) {
        printf("  staged:     %s\n", index->entries[i].path);
        staged_count++;
    }

    if (staged_count == 0)
        printf("  (nothing to show)\n");

    printf("\n");
    return 0;
}

// ─── PHASE 3: STEP 1 ────────────────────────────────────────────────────────

int index_load(Index *index) {

    index->count = 0;

    FILE *f = fopen(INDEX_FILE, "r");
    if (!f) {
        // No index file yet = empty index
        return 0;
    }

    while (index->count < MAX_INDEX_ENTRIES) {

        IndexEntry *entry = &index->entries[index->count];

        char hash_hex[HASH_HEX_SIZE + 1];

        int read = fscanf(f, "%o %64s %lu %u %511[^\n]\n",
                          &entry->mode,
                          hash_hex,
                          &entry->mtime_sec,
                          &entry->size,
                          entry->path);

        if (read != 5)
            break;

        if (hex_to_hash(hash_hex, &entry->hash) != 0) {
            fclose(f);
            return -1;
        }

        index->count++;
    }

    fclose(f);
    return 0;
}

// Not implemented yet
int index_save(const Index *index) {
    return -1;
}

int index_add(Index *index, const char *path) {
    return -1;
}
