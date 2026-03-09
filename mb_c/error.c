#include "error.h"
#include <stdio.h>
#include <stdlib.h>
#include <SDL2/SDL.h>

void report_error(const char* message) {
    fprintf(stderr, "Error: %s\n", message);
    exit(1);
}

void report_sdl_error(const char* message) {
    fprintf(stderr, "SDL Error: %s - %s\n", message, SDL_GetError());
    exit(1);
}
