#include "engine.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <pthread.h>
#include <unistd.h>
#include <ctype.h>

#define MAX_LINE_LENGTH 256
#define MAX_WORKERS 4
#define SMALL_FILE_LIMIT (8L * 1024L * 1024L)

struct worker_args {
    char *data;
    long start;
    long end;
    long data_size;
    char *target;
    size_t target_length;
};

struct worker_result {
    int count;
    char **instances;
    int used;
    int capacity;
};

static void free_worker_result(struct worker_result *result) {
    if (result == NULL) {
        return;
    }
    for (int i = 0; i < result->used; i++) {
        free(result->instances[i]);
    }
    free(result->instances);
    free(result);
}

void *count_worker(void *arg) {
    struct worker_args *args = (struct worker_args *)arg;
    int *result = malloc(sizeof(int));

    if (result == NULL) {
        return NULL;
    }

    *result = 0;

        for (long position = args->start;
            position < args->end && position + (long)args->target_length <= args->data_size;
         position++) {
        if (memcmp(args->data + position, args->target, args->target_length) == 0 &&
            memchr(args->data + position, '\n', args->target_length) == NULL) {
            (*result)++;
        }
    }

    return result;
}

void *instance_worker(void *arg) {
    struct worker_args *args = arg;
    struct worker_result *result = calloc(1, sizeof(*result));

    if (result == NULL) {
        return NULL;
    }

        for (long position = args->start;
            position < args->end && position + (long)args->target_length <= args->data_size;
         position++) {
        long line_start;
        long line_end;
        long visible_end;
        long trimmed_start;
        long trimmed_end;

        if (memcmp(args->data + position, args->target, args->target_length) != 0 ||
            memchr(args->data + position, '\n', args->target_length) != NULL) {
            continue;
        }
        line_start = position;
        while (line_start > 0 && args->data[line_start - 1] != '\n') line_start--;
        line_end = position;
        while (line_end < args->data_size && args->data[line_end] != '\n') line_end++;
        visible_end = line_end;
        for (long i = line_start; i < line_end; i++) {
            if (args->data[i] == '\0') {
                visible_end = i;
                break;
            }
        }
        if (position + (long)args->target_length > visible_end) continue;
        trimmed_start = line_start;
        trimmed_end = visible_end;
        while (trimmed_start < trimmed_end &&
               isspace((unsigned char)args->data[trimmed_start])) trimmed_start++;
        while (trimmed_end > trimmed_start &&
               isspace((unsigned char)args->data[trimmed_end - 1])) trimmed_end--;
        if (position < trimmed_start || position + (long)args->target_length > trimmed_end) continue;

        {
            size_t line_length = (size_t)(trimmed_end - trimmed_start);
            if (result->used == result->capacity) {
                int new_capacity = result->capacity == 0 ? 8 : result->capacity * 2;
                char **grown = realloc(result->instances, sizeof(*grown) * (size_t)new_capacity);
                if (grown == NULL) {
                    free_worker_result(result);
                    return NULL;
                }
                result->instances = grown;
                result->capacity = new_capacity;
            }
            result->instances[result->used] = malloc(line_length + 1);
            if (result->instances[result->used] == NULL) {
                free_worker_result(result);
                return NULL;
            }
            memcpy(result->instances[result->used], args->data + trimmed_start, line_length);
            result->instances[result->used][line_length] = '\0';
            result->used++;
            result->count++;
        }
    }

    return result;
}

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

int search_count(char *filename, char *target) {
    char *buffer;
    long file_size;
    size_t target_length;
    int worker_count = MAX_WORKERS;
    int total = 0;
    int created = 0;
    pthread_t threads[MAX_WORKERS];
    struct worker_args args[MAX_WORKERS];
    int *partials[MAX_WORKERS] = {NULL};

    if (filename == NULL || target == NULL || *target == '\0') return 0;
    target_length = strlen(target);
    buffer = read_file_buffer(filename, &file_size);
    if (buffer == NULL || file_size == 0) {
        free(buffer);
        return 0;
    }
    if (worker_count > file_size) worker_count = (int)file_size;

    for (int i = 0; i < worker_count; i++) {
        args[i].data = buffer;
        args[i].start = (file_size * i) / worker_count;
        args[i].end = (file_size * (i + 1)) / worker_count;
        args[i].data_size = file_size;
        args[i].target = target;
        args[i].target_length = target_length;
        if (pthread_create(&threads[i], NULL, count_worker, &args[i]) != 0) {
            for (int j = 0; j < created; j++) pthread_join(threads[j], (void **)&partials[j]);
            for (int j = 0; j < created; j++) free(partials[j]);
            free(buffer);
            return 0;
        }
        created++;
    }
    for (int i = 0; i < worker_count; i++) {
        pthread_join(threads[i], (void **)&partials[i]);
        if (partials[i] == NULL) {
            for (int j = 0; j < worker_count; j++) free(partials[j]);
            free(buffer);
            return 0;
        }
        total += *partials[i];
        free(partials[i]);
    }
    free(buffer);
    return total;
}

struct count_result search_instance(char *filename, char *target) {
    struct count_result result = {0, NULL};
    char *buffer;
    long file_size;
    size_t target_length;
    int worker_count = MAX_WORKERS;
    int created = 0;
    int total = 0;
    pthread_t threads[MAX_WORKERS];
    struct worker_args args[MAX_WORKERS];
    struct worker_result *locals[MAX_WORKERS] = {NULL};

    if (filename == NULL || target == NULL || *target == '\0') return result;
    target_length = strlen(target);
    buffer = read_file_buffer(filename, &file_size);
    if (buffer == NULL || file_size == 0) {
        free(buffer);
        return result;
    }
    if (worker_count > file_size) worker_count = (int)file_size;

    for (int i = 0; i < worker_count; i++) {
        args[i].data = buffer;
        args[i].start = (file_size * i) / worker_count;
        args[i].end = (file_size * (i + 1)) / worker_count;
        args[i].data_size = file_size;
        args[i].target = target;
        args[i].target_length = target_length;
        if (pthread_create(&threads[i], NULL, instance_worker, &args[i]) != 0) {
            for (int j = 0; j < created; j++) pthread_join(threads[j], (void **)&locals[j]);
            for (int j = 0; j < created; j++) free_worker_result(locals[j]);
            free(buffer);
            return result;
        }
        created++;
    }
    for (int i = 0; i < worker_count; i++) {
        pthread_join(threads[i], (void **)&locals[i]);
        if (locals[i] == NULL) {
            for (int j = 0; j < worker_count; j++) free_worker_result(locals[j]);
            free(buffer);
            return result;
        }
        total += locals[i]->count;
    }
    if (total > 0) {
        int next = 0;
        result.instances = calloc((size_t)total, sizeof(*result.instances));
        if (result.instances == NULL) {
            for (int i = 0; i < worker_count; i++) free_worker_result(locals[i]);
            free(buffer);
            return result;
        }
        for (int i = 0; i < worker_count; i++) {
            for (int j = 0; j < locals[i]->used; j++) {
                result.instances[next++] = locals[i]->instances[j];
                locals[i]->instances[j] = NULL;
            }
        }
    }
    result.count = total;
    for (int i = 0; i < worker_count; i++) free_worker_result(locals[i]);
    free(buffer);
    return result;
}