#include "engine.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <pthread.h>
#include <unistd.h>
#include <ctype.h>

#define MAX_LINE_LENGTH 256
#define MAX_WORKERS 4

struct worker_args {
    char *data;
    long start;
    long end;
    char *target;
};

struct worker_result {
    int count;
    char **instances;
    int used;
    int capacity;
};

void *count_worker(void *arg) {
    struct worker_args *args = (struct worker_args *)arg;
    char *line_start = args->data + args->start;
    char *data_end = args->data + args->end;
    int *result = malloc(sizeof(int));

    if (result == NULL) {
        return NULL;
    }

    *result = 0;

    while (line_start < data_end) {
        char *line_end = line_start;
        char *match;
        char line[MAX_LINE_LENGTH];
        size_t line_len;

        while (line_end < data_end && *line_end != '\n') {
            line_end++;
        }

        line_len = (size_t)(line_end - line_start);
        if (line_len >= MAX_LINE_LENGTH) {
            line_len = MAX_LINE_LENGTH - 1;
        }
        memcpy(line, line_start, line_len);
        line[line_len] = '\0';

        char *trimmed = line;
        while (*trimmed != '\0' && isspace((unsigned char)*trimmed)) {
            trimmed++;
        }

        char *tail = trimmed + strlen(trimmed);
        while (tail > trimmed && isspace((unsigned char)*(tail - 1))) {
            tail--;
        }
        *tail = '\0';

        if (*trimmed != '\0') {
            match = trimmed;
            while ((match = strstr(match, args->target)) != NULL) {
                (*result)++;
                match++;
            }
        }

        if (line_end < data_end) {
            line_start = line_end + 1;
        } else {
            break;
        }
    }

    return result;
}

void *instance_worker(void *arg) {
    struct worker_args *args = (struct worker_args *)arg;
    struct worker_result *result = calloc(1, sizeof(struct worker_result));
    char *line_start = args->data + args->start;
    char *data_end = args->data + args->end;

    if (result == NULL) {
        return NULL;
    }

    while (line_start < data_end) {
        char *line_end = line_start;
        char *match;
        char line[MAX_LINE_LENGTH];
        size_t line_len;

        while (line_end < data_end && *line_end != '\n') {
            line_end++;
        }

        line_len = (size_t)(line_end - line_start);
        if (line_len >= MAX_LINE_LENGTH) {
            line_len = MAX_LINE_LENGTH - 1;
        }
        memcpy(line, line_start, line_len);
        line[line_len] = '\0';

        char *trimmed = line;
        while (*trimmed != '\0' && isspace((unsigned char)*trimmed)) {
            trimmed++;
        }

        char *tail = trimmed + strlen(trimmed);
        while (tail > trimmed && isspace((unsigned char)*(tail - 1))) {
            tail--;
        }
        *tail = '\0';

        if (*trimmed != '\0') {
            int occurrences = 0;
            match = trimmed;
            while ((match = strstr(match, args->target)) != NULL) {
                occurrences++;
                match++;
            }

            if (occurrences > 0) {
                int i;
                char **grown;

                for (i = 0; i < occurrences; i++) {
                    if (result->used == result->capacity) {
                        int new_capacity = (result->capacity == 0) ? 8 : result->capacity * 2;
                        grown = realloc(result->instances, sizeof(char *) * new_capacity);
                        if (grown == NULL) {
                            int j;
                            for (j = 0; j < result->used; j++) {
                                free(result->instances[j]);
                            }
                            free(result->instances);
                            free(result);
                            return NULL;
                        }
                        result->instances = grown;
                        result->capacity = new_capacity;
                    }

                    result->instances[result->used] = malloc(strlen(trimmed) + 1);
                    if (result->instances[result->used] == NULL) {
                        int j;
                        for (j = 0; j < result->used; j++) {
                            free(result->instances[j]);
                        }
                        free(result->instances);
                        free(result);
                        return NULL;
                    }

                    strcpy(result->instances[result->used], trimmed);
                    result->used++;
                }

                result->count += occurrences;
            }
        }

        if (line_end < data_end) {
            line_start = line_end + 1;
        } else {
            break;
        }
    }

    return result;
}

int search_count(char *filename, char *target) {
    FILE *file = fopen(filename, "rb");
    char *buffer;
    long file_size;
    long *line_starts;
    long line_capacity;
    long total_lines = 0;
    int worker_count;
    int i;
    int total = 0;
    pthread_t *threads;
    struct worker_args *args;

    if (file == NULL) {
        return 0;
    }

    if (fseek(file, 0, SEEK_END) != 0) {
        fclose(file);
        return 0;
    }
    file_size = ftell(file);
    if (file_size < 0) {
        fclose(file);
        return 0;
    }
    rewind(file);

    buffer = malloc((size_t)file_size + 1);
    if (buffer == NULL) {
        fclose(file);
        return 0;
    }

    if (fread(buffer, 1, (size_t)file_size, file) != (size_t)file_size) {
        free(buffer);
        fclose(file);
        return 0;
    }
    buffer[file_size] = '\0';
    fclose(file);

    if (file_size == 0) {
        free(buffer);
        return 0;
    }

    line_capacity = 1;
    for (long position = 0; position + 1 < file_size; position++) {
        if (buffer[position] == '\n' && buffer[position + 1] != '\0') {
            line_capacity++;
        }
    }

    line_starts = malloc(sizeof(long) * (size_t)line_capacity);
    if (line_starts == NULL) {
        free(buffer);
        return 0;
    }

    line_starts[total_lines++] = 0;
    for (long position = 0; position + 1 < file_size; position++) {
        if (buffer[position] == '\n' && buffer[position + 1] != '\0') {
            line_starts[total_lines++] = position + 1;
        }
    }

    worker_count = (int)sysconf(_SC_NPROCESSORS_ONLN);
    if (worker_count <= 0) {
        worker_count = 1;
    }
    if (worker_count > MAX_WORKERS) {
        worker_count = MAX_WORKERS;
    }
    if (worker_count > total_lines) {
        worker_count = (int)total_lines;
    }
    if (worker_count < 1) {
        worker_count = 1;
    }

    threads = malloc(sizeof(pthread_t) * (size_t)worker_count);
    args = calloc((size_t)worker_count, sizeof(struct worker_args));
    if (threads == NULL || args == NULL) {
        free(threads);
        free(args);
        free(line_starts);
        free(buffer);
        return 0;
    }

    for (i = 0; i < worker_count; i++) {
        long start_line = (total_lines * i) / worker_count;
        long end_line = (total_lines * (i + 1)) / worker_count;
        long start = line_starts[start_line];
        long end = (end_line >= total_lines) ? file_size : line_starts[end_line];

        args[i].data = buffer;
        args[i].start = start;
        args[i].end = end;
        args[i].target = target;

        if (pthread_create(&threads[i], NULL, count_worker, &args[i]) != 0) {
            int j;
            for (j = 0; j < i; j++) {
                pthread_join(threads[j], NULL);
            }
            free(threads);
            free(args);
            free(line_starts);
            free(buffer);
            return 0;
        }
    }

    for (i = 0; i < worker_count; i++) {
        int *partial = NULL;
        pthread_join(threads[i], (void **)&partial);
        if (partial != NULL) {
            total += *partial;
            free(partial);
        }
    }

    free(threads);
    free(args);
    free(line_starts);
    free(buffer);
    return total;
}

struct count_result search_instance(char *filename, char *target) {
    FILE *file = fopen(filename, "rb");
    char *buffer;
    long file_size;
    long *line_starts;
    long line_capacity;
    long total_lines = 0;
    int worker_count;
    int i;
    struct count_result result = {0, NULL};
    pthread_t *threads;
    struct worker_args *args;
    struct worker_result **locals;
    int total_instances = 0;

    if (file == NULL) {
        result.count = 0;
        result.instances = NULL;
        return result;
    }

    if (fseek(file, 0, SEEK_END) != 0) {
        fclose(file);
        result.count = 0;
        result.instances = NULL;
        return result;
    }
    file_size = ftell(file);
    if (file_size < 0) {
        fclose(file);
        result.count = 0;
        result.instances = NULL;
        return result;
    }
    rewind(file);

    buffer = malloc((size_t)file_size + 1);
    if (buffer == NULL) {
        fclose(file);
        result.count = 0;
        result.instances = NULL;
        return result;
    }

    if (fread(buffer, 1, (size_t)file_size, file) != (size_t)file_size) {
        free(buffer);
        fclose(file);
        result.count = 0;
        result.instances = NULL;
        return result;
    }
    buffer[file_size] = '\0';
    fclose(file);

    if (file_size == 0) {
        free(buffer);
        result.count = 0;
        result.instances = NULL;
        return result;
    }

    line_capacity = 1;
    for (long position = 0; position + 1 < file_size; position++) {
        if (buffer[position] == '\n' && buffer[position + 1] != '\0') {
            line_capacity++;
        }
    }

    line_starts = malloc(sizeof(long) * (size_t)line_capacity);
    if (line_starts == NULL) {
        free(buffer);
        result.count = 0;
        result.instances = NULL;
        return result;
    }

    line_starts[total_lines++] = 0;
    for (long position = 0; position + 1 < file_size; position++) {
        if (buffer[position] == '\n' && buffer[position + 1] != '\0') {
            line_starts[total_lines++] = position + 1;
        }
    }

    worker_count = (int)sysconf(_SC_NPROCESSORS_ONLN);
    if (worker_count <= 0) {
        worker_count = 1;
    }
    if (worker_count > MAX_WORKERS) {
        worker_count = MAX_WORKERS;
    }
    if (worker_count > total_lines) {
        worker_count = (int)total_lines;
    }
    if (worker_count < 1) {
        worker_count = 1;
    }

    threads = malloc(sizeof(pthread_t) * (size_t)worker_count);
    args = calloc((size_t)worker_count, sizeof(struct worker_args));
    locals = calloc((size_t)worker_count, sizeof(struct worker_result *));
    if (threads == NULL || args == NULL || locals == NULL) {
        free(threads);
        free(args);
        free(locals);
        free(line_starts);
        free(buffer);
        result.count = 0;
        result.instances = NULL;
        return result;
    }

    for (i = 0; i < worker_count; i++) {
        long start_line = (total_lines * i) / worker_count;
        long end_line = (total_lines * (i + 1)) / worker_count;
        long start = line_starts[start_line];
        long end = (end_line >= total_lines) ? file_size : line_starts[end_line];

        args[i].data = buffer;
        args[i].start = start;
        args[i].end = end;
        args[i].target = target;

        if (pthread_create(&threads[i], NULL, instance_worker, &args[i]) != 0) {
            int j;
            for (j = 0; j < i; j++) {
                void *joined = NULL;
                pthread_join(threads[j], &joined);
                if (joined != NULL) {
                    struct worker_result *local = (struct worker_result *)joined;
                    for (int k = 0; k < local->used; k++) {
                        free(local->instances[k]);
                    }
                    free(local->instances);
                    free(local);
                }
            }
            free(threads);
            free(args);
            free(locals);
            free(line_starts);
            free(buffer);
            result.count = 0;
            result.instances = NULL;
            return result;
        }
    }

    for (i = 0; i < worker_count; i++) {
        void *joined = NULL;
        pthread_join(threads[i], &joined);
        locals[i] = (struct worker_result *)joined;
        if (locals[i] != NULL) {
            total_instances += locals[i]->used;
        }
    }

    result.count = total_instances;
    result.instances = NULL;
    if (total_instances > 0) {
        int next_index = 0;
        result.instances = calloc((size_t)total_instances, sizeof(char *));
        if (result.instances == NULL) {
            int j;
            for (j = 0; j < worker_count; j++) {
                if (locals[j] != NULL) {
                    int k;
                    for (k = 0; k < locals[j]->used; k++) {
                        free(locals[j]->instances[k]);
                    }
                    free(locals[j]->instances);
                    free(locals[j]);
                }
            }
            free(threads);
            free(args);
            free(locals);
            free(line_starts);
            free(buffer);
            result.count = 0;
            result.instances = NULL;
            return result;
        }

        for (i = 0; i < worker_count; i++) {
            if (locals[i] != NULL) {
                int j;
                for (j = 0; j < locals[i]->used; j++) {
                    result.instances[next_index++] = locals[i]->instances[j];
                }
                free(locals[i]->instances);
                free(locals[i]);
            }
        }
    }

    free(threads);
    free(args);
    free(locals);
    free(line_starts);
    free(buffer);
    return result;
}