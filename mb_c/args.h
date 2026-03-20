#ifndef ARGS_H
#define ARGS_H

#include <stdbool.h>

#define MAX_PATH 260

typedef struct {
    char path[MAX_PATH];
    bool campaign_mode;
    bool windowed;
} Args;

void parse_args(int argc, char** argv, Args* out_args);

#endif // ARGS_H
