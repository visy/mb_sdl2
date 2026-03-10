#ifndef GLYPHS_H
#define GLYPHS_H

#include <SDL2/SDL.h>

typedef enum {
    GLYPH_SHOVEL_POINTER,
    GLYPH_ARROW_POINTER,
    GLYPH_SHOP_SLOT_UNSELECTED,
    GLYPH_SHOP_SLOT_SELECTED,
    GLYPH_READY,
    GLYPH_EQUIPMENT_START,
    GLYPH_MAP_START = 100,
    GLYPH_PLAYER_START = 1000,
    GLYPH_PLAYER_DIG_START = 2000
} GlyphType;

typedef struct {
    SDL_Texture* texture;
} Glyphs;

void glyphs_init(Glyphs* glyphs, SDL_Texture* texture);
void glyphs_render(Glyphs* glyphs, SDL_Renderer* renderer, int x, int y, GlyphType type);
void glyphs_dimensions(GlyphType type, int* w, int* h);

#endif // GLYPHS_H
