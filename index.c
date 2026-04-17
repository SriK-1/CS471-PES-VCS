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

int object_write(ObjectType type, const void *data, size_t len, ObjectID *id_out);

// ─────────────────────────────────────────────
// PROVIDED
// ─────────────────────────────────────────────

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
                memmove(&index->entries[i],
                        &index->entries[i + 1],
                        remaining * sizeof(IndexEntry));
            index->count--;
            return index_save(index);
        }
    }
    return -1;
}

int index_status(const Index *index) {
    printf("Staged changes:\n");

    if (index->count == 0)
        printf("  (nothing to show)\n");
    else {
        for (int i = 0; i < index->count; i++)
            printf("  staged:     %s\n", index->entries[i].path);
    }

    printf("\n");
    return 0;
}

// ─────────────────────────────────────────────
// index_load
// ─────────────────────────────────────────────

int index_load(Index *index) {

    index->count = 0;

    FILE *f = fopen(INDEX_FILE, "r");
    if (!f)
        return 0;

    while (index->count < MAX_INDEX_ENTRIES) {

        IndexEntry *e = &index->entries[index->count];
        char hex[HASH_HEX_SIZE + 1];

        int read = fscanf(f, "%o %64s %lu %u %511[^\n]\n",
                          &e->mode,
                          hex,
                          &e->mtime_sec,
                          &e->size,
                          e->path);

        if (read != 5)
            break;

        if (hex_to_hash(hex, &e->hash) != 0) {
            fclose(f);
            return -1;
        }

        index->count++;
    }

    fclose(f);
    return 0;
}

// ─────────────────────────────────────────────
// index_save (NO STACK COPY)
// ─────────────────────────────────────────────

int index_save(const Index *index) {

    char tmp_path[512];
    snprintf(tmp_path, sizeof(tmp_path), "%s.tmp", INDEX_FILE);

    FILE *f = fopen(tmp_path, "w");
    if (!f)
        return -1;

    for (int i = 0; i < index->count; i++) {

        char hex[HASH_HEX_SIZE + 1];
        hash_to_hex(&index->entries[i].hash, hex);

        fprintf(f, "%o %s %lu %u %s\n",
                index->entries[i].mode,
                hex,
                index->entries[i].mtime_sec,
                index->entries[i].size,
                index->entries[i].path);
    }

    fflush(f);
    fsync(fileno(f));
    fclose(f);

    if (rename(tmp_path, INDEX_FILE) != 0)
        return -1;

    return 0;
}

// ─────────────────────────────────────────────
// index_add
// ─────────────────────────────────────────────

int index_add(Index *index, const char *path) {

    struct stat st;
    if (stat(path, &st) != 0)
        return -1;

    FILE *f = fopen(path, "rb");
    if (!f)
        return -1;

    size_t size = st.st_size;
    char *buf = NULL;

    if (size > 0) {
        buf = malloc(size);
        fread(buf, 1, size, f);
    }

    fclose(f);

    ObjectID id;
    if (object_write(OBJ_BLOB, buf, size, &id) != 0) {
        if (buf) free(buf);
        return -1;
    }

    if (buf) free(buf);

    IndexEntry *e = index_find(index, path);
    if (!e) {
        if (index->count >= MAX_INDEX_ENTRIES)
            return -1;
        e = &index->entries[index->count++];
    }

    e->mode = (st.st_mode & S_IXUSR) ? 0100755 : 0100644;
    e->hash = id;
    e->mtime_sec = st.st_mtime;
    e->size = st.st_size;
    strncpy(e->path, path, sizeof(e->path));
    e->path[sizeof(e->path) - 1] = '\0';

    return index_save(index);
}
