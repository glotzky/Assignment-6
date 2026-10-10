#include "engine.h"
#include <string.h>
#include <stdlib.h>
#include <stdbool.h>
#include <stdio.h>
#include <sys/stat.h>
#include <omp.h>
#include <ctype.h>

#define MAX_LINE_LENGTH 256
#define MAX_WORKERS 4

struct local_instance {
    char **items;
    int used;
    int capacity;
    int count;
};

static char *read_file_buffer(char *filename, long *file_size) {
    FILE *file = fopen(filename, "rb");
    char *buffer;

    if (file == NULL || fseek(file, 0, SEEK_END) != 0) {
        if (file != NULL) fclose(file);
        return NULL;
    }
    *file_size = ftell(file);
    if (*file_size < 0 || fseek(file, 0, SEEK_SET) != 0) {
        fclose(file);
        return NULL;
    }
    buffer = malloc((size_t)*file_size + 1);
    if (buffer == NULL) {
        fclose(file);
        return NULL;
    }
    if (fread(buffer, 1, (size_t)*file_size, file) != (size_t)*file_size) {
        free(buffer);
        fclose(file);
        return NULL;
    }
    buffer[*file_size] = '\0';
    fclose(file);
    return buffer;
}

static int count_chunk(const char *data, long start, long end, long file_size,
                       const char *target, size_t target_length) {
    int count = 0;

    for (long position = start;
         position < end && position + (long)target_length <= file_size;
         position++) {
        if (memcmp(data + position, target, target_length) == 0 &&
            memchr(data + position, '\n', target_length) == NULL) {
            count++;
        }
    }
    return count;
}

static void free_local_instance(struct local_instance *local) {
    for (int i = 0; i < local->used; i++) free(local->items[i]);
    free(local->items);
    local->items = NULL;
    local->used = 0;
    local->capacity = 0;
    local->count = 0;
}

static int scan_chunk(const char *data, long start, long end, long file_size,
                      const char *target, size_t target_length,
                      struct local_instance *local) {
    for (long position = start;
         position < end && position + (long)target_length <= file_size;
         position++) {
        long line_start;
        long line_end;
        long visible_end;
        long trimmed_start;
        long trimmed_end;
        size_t line_length;

        if (memcmp(data + position, target, target_length) != 0 ||
            memchr(data + position, '\n', target_length) != NULL) continue;
        line_start = position;
        while (line_start > 0 && data[line_start - 1] != '\n') line_start--;
        line_end = position;
        while (line_end < file_size && data[line_end] != '\n') line_end++;
        visible_end = line_end;
        for (long i = line_start; i < line_end; i++) {
            if (data[i] == '\0') {
                visible_end = i;
                break;
            }
        }
        if (position + (long)target_length > visible_end) continue;
        trimmed_start = line_start;
        trimmed_end = visible_end;
        while (trimmed_start < trimmed_end &&
               isspace((unsigned char)data[trimmed_start])) trimmed_start++;
        while (trimmed_end > trimmed_start &&
               isspace((unsigned char)data[trimmed_end - 1])) trimmed_end--;
        if (position < trimmed_start || position + (long)target_length > trimmed_end) continue;

        line_length = (size_t)(trimmed_end - trimmed_start);
        if (local->used == local->capacity) {
            int new_capacity = local->capacity == 0 ? 8 : local->capacity * 2;
            char **grown = realloc(local->items, sizeof(*grown) * (size_t)new_capacity);
            if (grown == NULL) return 0;
            local->items = grown;
            local->capacity = new_capacity;
        }
        local->items[local->used] = malloc(line_length + 1);
        if (local->items[local->used] == NULL) return 0;
        memcpy(local->items[local->used], data + trimmed_start, line_length);
        local->items[local->used][line_length] = '\0';
        local->used++;
        local->count++;
    }
    return 1;
}

int search_count(char *filename, char *target) {
    char *data;
    long file_size;
    size_t target_length;
    int thread_count = omp_get_max_threads();
    int total = 0;

    if (filename == NULL || target == NULL || *target == '\0') return 0;
    target_length = strlen(target);
    data = read_file_buffer(filename, &file_size);
    if (data == NULL || file_size == 0) {
        free(data);
        return 0;
    }
    if (thread_count < 1) thread_count = 1;
    if (thread_count > MAX_WORKERS) thread_count = MAX_WORKERS;
    if (thread_count > file_size) thread_count = (int)file_size;

    #pragma omp parallel for reduction(+:total) num_threads(thread_count) schedule(static)
    for (int i = 0; i < thread_count; i++) {
        long start = (file_size * i) / thread_count;
        long end = (file_size * (i + 1)) / thread_count;
        total += count_chunk(data, start, end, file_size, target, target_length);
    }
    free(data);
    return total;
}

struct count_result search_instance(char *filename, char *target) {
    struct count_result result = {0, NULL};
    char *data;
    long file_size;
    size_t target_length;
    int thread_count = omp_get_max_threads();
    int failed[MAX_WORKERS] = {0};
    int total = 0;
    struct local_instance locals[MAX_WORKERS] = {{0}};

    if (filename == NULL || target == NULL || *target == '\0') return result;
    target_length = strlen(target);
    data = read_file_buffer(filename, &file_size);
    if (data == NULL || file_size == 0) {
        free(data);
        return result;
    }
    if (thread_count < 1) thread_count = 1;
    if (thread_count > MAX_WORKERS) thread_count = MAX_WORKERS;
    if (thread_count > file_size) thread_count = (int)file_size;

    #pragma omp parallel for num_threads(thread_count) schedule(static)
    for (int i = 0; i < thread_count; i++) {
        long start = (file_size * i) / thread_count;
        long end = (file_size * (i + 1)) / thread_count;
        failed[i] = !scan_chunk(data, start, end, file_size, target,
                                target_length, &locals[i]);
    }
    for (int i = 0; i < thread_count; i++) {
        if (failed[i]) {
            for (int j = 0; j < thread_count; j++) free_local_instance(&locals[j]);
            free(data);
            return result;
        }
        total += locals[i].count;
    }
    if (total > 0) {
        int next = 0;
        result.instances = calloc((size_t)total, sizeof(*result.instances));
        if (result.instances == NULL) {
            for (int i = 0; i < thread_count; i++) free_local_instance(&locals[i]);
            free(data);
            return result;
        }
        for (int i = 0; i < thread_count; i++) {
            for (int j = 0; j < locals[i].used; j++) {
                result.instances[next++] = locals[i].items[j];
                locals[i].items[j] = NULL;
            }
        }
    }
    result.count = total;
    for (int i = 0; i < thread_count; i++) free_local_instance(&locals[i]);
    free(data);
    return result;
}