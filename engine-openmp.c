#include "engine.h"
#include <string.h>
#include <stdlib.h>
#include <stdbool.h>
#include <stdio.h>
#include <sys/stat.h>
#include <omp.h>
#include <ctype.h>

#define MAX_LINE_LENGTH 256

struct local_instance {
    char **items;
    int used;
};

int search_count(char *filename, char *target) {
    FILE *file = fopen(filename, "r");
    char buffer[MAX_LINE_LENGTH];
    char **lines = NULL;
    int line_count = 0;
    int capacity = 0;
    int total = 0;
    int failed = 0;

    if (file == NULL || target == NULL || *target == '\0') {
        if (file != NULL) {
            fclose(file);
        }
        return 0;
    }

    while (fgets(buffer, sizeof(buffer), file) != NULL) {
        char *start = buffer;
        char *end;
        char **grown;

        while (*start != '\0' && isspace((unsigned char)*start)) {
            start++;
        }
        end = start + strlen(start);
        while (end > start && isspace((unsigned char)*(end - 1))) {
            *(--end) = '\0';
        }
        if (*start == '\0') {
            continue;
        }

        if (line_count == capacity) {
            int new_capacity = capacity == 0 ? 16 : capacity * 2;
            grown = realloc(lines, sizeof(*lines) * (size_t)new_capacity);
            if (grown == NULL) {
                failed = 1;
                break;
            }
            lines = grown;
            capacity = new_capacity;
        }

        lines[line_count] = malloc(strlen(start) + 1);
        if (lines[line_count] == NULL) {
            failed = 1;
            break;
        }
        strcpy(lines[line_count++], start);
    }
    fclose(file);

    if (failed) {
        for (int i = 0; i < line_count; i++) {
            free(lines[i]);
        }
        free(lines);
        return 0;
    }

    #pragma omp parallel for reduction(+:total)
    for (int i = 0; i < line_count; i++) {
        char *match = lines[i];
        while ((match = strstr(match, target)) != NULL) {
            total++;
            match++;
        }
    }

    for (int i = 0; i < line_count; i++) {
        free(lines[i]);
    }
    free(lines);
    return total;
}


struct count_result search_instance(char *filename,char *target){
    FILE *file = fopen(filename, "r");
    char buffer[MAX_LINE_LENGTH];
    char **lines = NULL;
    int line_count = 0;
    int capacity = 0;
    int total = 0;
    int failed = 0;
    int *offsets;
    int allocation_failed = 0;
    struct count_result result = {0, NULL};

    if (file == NULL || target == NULL || *target == '\0') {
        if (file != NULL) {
            fclose(file);
        }
        return result;
    }

    while (fgets(buffer, sizeof(buffer), file) != NULL) {
        char *start = buffer;
        char *end;
        char **grown;

        while (*start != '\0' && isspace((unsigned char)*start)) {
            start++;
        }
        end = start + strlen(start);
        while (end > start && isspace((unsigned char)*(end - 1))) {
            *(--end) = '\0';
        }
        if (*start == '\0') {
            continue;
        }

        if (line_count == capacity) {
            int new_capacity = capacity == 0 ? 16 : capacity * 2;
            grown = realloc(lines, sizeof(*lines) * (size_t)new_capacity);
            if (grown == NULL) {
                failed = 1;
                break;
            }
            lines = grown;
            capacity = new_capacity;
        }

        lines[line_count] = malloc(strlen(start) + 1);
        if (lines[line_count] == NULL) {
            failed = 1;
            break;
        }
        strcpy(lines[line_count++], start);
    }
    fclose(file);

    if (failed) {
        for (int i = 0; i < line_count; i++) {
            free(lines[i]);
        }
        free(lines);
        return result;
    }

    if (line_count == 0) {
        free(lines);
        return result;
    }

    offsets = calloc((size_t)line_count + 1, sizeof(*offsets));
    if (offsets == NULL) {
        for (int i = 0; i < line_count; i++) {
            free(lines[i]);
        }
        free(lines);
        return result;
    }

    #pragma omp parallel
    {
        #pragma omp for reduction(+:total)
        for (int i = 0; i < line_count; i++) {
            char *match = lines[i];
            int matches = 0;

            while ((match = strstr(match, target)) != NULL) {
                matches++;
                match++;
            }
            offsets[i + 1] = matches;
            total += matches;
        }

        #pragma omp single
        {
            for (int i = 0; i < line_count; i++) {
                offsets[i + 1] += offsets[i];
            }

            if (total > 0) {
                result.instances = calloc((size_t)total, sizeof(*result.instances));
                if (result.instances == NULL) {
                    allocation_failed = 1;
                }
            }
        }

        #pragma omp for
        for (int i = 0; i < line_count; i++) {
            if (!allocation_failed) {
                for (int j = offsets[i]; j < offsets[i + 1]; j++) {
                    result.instances[j] = malloc(strlen(lines[i]) + 1);
                    if (result.instances[j] != NULL) {
                        strcpy(result.instances[j], lines[i]);
                    }
                }
            }
        }
    }

    if (allocation_failed) {
        free(offsets);
        for (int i = 0; i < line_count; i++) {
            free(lines[i]);
        }
        free(lines);
        return result;
    }

    for (int i = 0; i < total; i++) {
        if (result.instances[i] == NULL) {
            for (int j = 0; j < total; j++) {
                free(result.instances[j]);
            }
            free(result.instances);
            result.instances = NULL;
            free(offsets);
            for (int j = 0; j < line_count; j++) {
                free(lines[j]);
            }
            free(lines);
            return result;
        }
    }
    result.count = total;

    free(offsets);
    for (int i = 0; i < line_count; i++) {
        free(lines[i]);
    }
    free(lines);
    return result;
}