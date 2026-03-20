#include "args.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <direct.h>
#define getcwd _getcwd
#define PATH_SEP '\\'
#else
#include <unistd.h>
#define PATH_SEP '/'
#endif
#include <sys/stat.h>

static bool is_file(const char* path) {
    struct stat path_stat;
    if (stat(path, &path_stat) != 0) {
        return false;
    }
    return S_ISREG(path_stat.st_mode);
}

static bool is_dir(const char* path) {
    struct stat path_stat;
    if (stat(path, &path_stat) != 0) {
        return false;
    }
    return S_ISDIR(path_stat.st_mode);
}

void parse_args(int argc, char** argv, Args* out_args) {
    out_args->campaign_mode = false;
    out_args->windowed = false;
    out_args->path[0] = '\0';

    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "--campaign") == 0) {
            out_args->campaign_mode = true;
        } else if (strcmp(argv[i], "-w") == 0 || strcmp(argv[i], "--windowed") == 0) {
            out_args->windowed = true;
        } else if (strcmp(argv[i], "--help") == 0) {
            fprintf(stderr, "MineBombers 3.11\n\n");
            fprintf(stderr, "USAGE:\n");
            fprintf(stderr, "    mb-reloaded [--campaign] [-w] [game-path]\n");
            exit(0);
        } else {
            strncpy(out_args->path, argv[i], MAX_PATH - 1);
            out_args->path[MAX_PATH - 1] = '\0';
        }
    }

    if (out_args->path[0] == '\0') {
        if (getcwd(out_args->path, MAX_PATH) == NULL) {
            fprintf(stderr, "Cannot detect current directory\n");
            exit(255);
        }
    }

    if (!is_dir(out_args->path)) {
        fprintf(stderr, "'%s' is not a valid game directory (must be a directory with 'TITLEBE.SPY' file).\n", out_args->path);
        exit(1);
    }

    char title_spy_path[MAX_PATH + 20];
    snprintf(title_spy_path, sizeof(title_spy_path), "%s%cTITLEBE.SPY", out_args->path, PATH_SEP);

    if (!is_file(title_spy_path)) {
        fprintf(stderr, "'%s' is not a valid game directory (must be a directory with 'TITLEBE.SPY' file).\n", out_args->path);
        exit(1);
    }
}
