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

// ─── PHASE 3: index_load ─────────────────────────────────────────────────────

int index_load(Index *index) {
    index->count = 0;
    FILE *f = fopen(INDEX_FILE, "r");
    if (!f) return 0;

    while (index->count < MAX_INDEX_ENTRIES) {
        IndexEntry *entry = &index->entries[index->count];
        char hash_hex[HASH_HEX_SIZE + 1];
        int read = fscanf(f, "%o %64s %lu %u %511[^\n]\n",
                          &entry->mode, hash_hex, &entry->mtime_sec, &entry->size, entry->path);
        if (read != 5) break;
        if (hex_to_hash(hash_hex, &entry->hash) != 0) { fclose(f); return -1; }
        index->count++;
    }
    fclose(f);
    return 0;
}

// ─── PHASE 3: index_save ─────────────────────────────────────────────────────

static int compare_index_entries(const void *a, const void *b) {
    const IndexEntry *ea = a;
    const IndexEntry *eb = b;
    return strcmp(ea->path, eb->path);
}

int index_save(const Index *index) {
    Index sorted = *index;
    qsort(sorted.entries, sorted.count, sizeof(IndexEntry), compare_index_entries);
    char tmp_path[512];
    snprintf(tmp_path, sizeof(tmp_path), "%s.tmp", INDEX_FILE);
    FILE *f = fopen(tmp_path, "w");
    if (!f) return -1;
    for (int i = 0; i < sorted.count; i++) {
        char hash_hex[HASH_HEX_SIZE + 1];
        hash_to_hex(&sorted.entries[i].hash, hash_hex);
        fprintf(f, "%o %s %lu %u %s\n", sorted.entries[i].mode, hash_hex, sorted.entries[i].mtime_sec, sorted.entries[i].size, sorted.entries[i].path);
    }
    fflush(f); fsync(fileno(f)); fclose(f);
    return rename(tmp_path, INDEX_FILE);
}

// ─── PHASE 3: index_add ──────────────────────────────────────────────────────

int index_add(Index *index, const char *path) {
    struct stat st;
    if (stat(path, &st) != 0) return -1;

    FILE *f = fopen(path, "rb");
    if (!f) return -1;

    void *buf = malloc(st.st_size);
    fread(buf, 1, st.st_size, f);
    fclose(f);

    ObjectID id;
    if (object_write(OBJ_BLOB, buf, st.st_size, &id) != 0) {
        free(buf);
        return -1;
    }
    free(buf);

    IndexEntry *entry = index_find(index, path);
    if (!entry) {
        if (index->count >= MAX_INDEX_ENTRIES) return -1;
        entry = &index->entries[index->count++];
        strncpy(entry->path, path, sizeof(entry->path) - 1);
    }

    entry->mode = 0100644; // Assuming regular file
    entry->hash = id;
    entry->mtime_sec = (uint64_t)st.st_mtime;
    entry->size = (uint32_t)st.st_size;

    return index_save(index);
}
