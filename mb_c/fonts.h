#ifndef FONTS_H
#define FONTS_H

#include <SDL2/SDL.h>
#include <stdbool.h>

typedef struct {
    SDL_Texture* texture;
} Font;

bool load_font(SDL_Renderer* renderer, const char* path, Font* out_font);
void destroy_font(Font* font);

void render_text(SDL_Renderer* renderer, Font* font, int x, int y, SDL_Color color, const char* text);

#endif // FONTS_H
