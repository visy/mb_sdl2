#ifndef GLYPHS_H
#define GLYPHS_H

#include <SDL2/SDL.h>

typedef enum {
    GLYPH_SHOVEL_POINTER,
    GLYPH_ARROW_POINTER,
    // Add other glyphs as needed
} GlyphType;

typedef struct {
    SDL_Texture* texture;
} Glyphs;

void glyphs_init(Glyphs* glyphs, SDL_Texture* texture);
void glyphs_render(Glyphs* glyphs, SDL_Renderer* renderer, int x, int y, GlyphType type);
void glyphs_dimensions(GlyphType type, int* w, int* h);

#endif // GLYPHS_H
