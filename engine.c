#include "engine.h"
#include <string.h>
#include <stdlib.h>
#include <stdbool.h>
#include <stdio.h>
#include <ctype.h>

#define MAX_LINE_LENGTH 256

int search_count(char *filename, char *target) {
    FILE *file = fopen(filename, "r");
    char line[MAX_LINE_LENGTH];
    int total = 0;

    if (file == NULL) {
        return 0;
    }

    while (fgets(line, sizeof(line), file) != NULL) {
        char *trimmed = line;
        char *end;
        char *match;

        while (*trimmed != '\0' && isspace((unsigned char)*trimmed)) {
            trimmed++;
        }

        end = trimmed + strlen(trimmed);
        while (end > trimmed && isspace((unsigned char)*(end - 1))) {
            *(--end) = '\0';
        }

        if (*trimmed == '\0') {
            continue;
        }

        match = trimmed;
        while ((match = strstr(match, target)) != NULL) {
            total++;
            match++;
        }
    }

    fclose(file);
    return total;
}

struct count_result search_instance(char *filename,char *target){
    FILE *file = fopen(filename, "r");
    char line[MAX_LINE_LENGTH];
    struct count_result result = {0, NULL};
    char **instances = NULL;
    int capacity = 16;
    int used = 0;

    if (file == NULL) {
        return result;
    }

    instances = malloc(sizeof(char *) * capacity);
    if (instances == NULL) {
        fclose(file);
        return result;
    }

    while (fgets(line, sizeof(line), file) != NULL) {
        char *trimmed = line;
        char *end;

        while (*trimmed != '\0' && isspace((unsigned char)*trimmed)) {
            trimmed++;
        }

        end = trimmed + strlen(trimmed);
        while (end > trimmed && isspace((unsigned char)*(end - 1))) {
            *(--end) = '\0';
        }

        if (*trimmed == '\0') {
            continue;
        }

        {
            char *match = trimmed;
            int occurrences = 0;

            while ((match = strstr(match, target)) != NULL) {
                occurrences++;
                match++;
            }

            if (occurrences > 0) {
                char *instance_line;
                int i;

                for (i = 0; i < occurrences; i++) {
                    if (used == capacity) {
                        char **grown;
                        capacity *= 2;
                        grown = realloc(instances, sizeof(char *) * capacity);
                        if (grown == NULL) {
                            int j;
                            for (j = 0; j < used; j++) {
                                free(instances[j]);
                            }
                            free(instances);
                            fclose(file);
                            result.count = 0;
                            result.instances = NULL;
                            return result;
                        }
                        instances = grown;
                    }

                    instance_line = malloc(strlen(trimmed) + 1);
                    if (instance_line == NULL) {
                        int j;
                        for (j = 0; j < used; j++) {
                            free(instances[j]);
                        }
                        free(instances);
                        fclose(file);
                        result.count = 0;
                        result.instances = NULL;
                        return result;
                    }

                    strcpy(instance_line, trimmed);
                    instances[used++] = instance_line;
                }

                result.count += occurrences;
            }
        }
    }

    fclose(file);
    result.instances = instances;
    return result;
}