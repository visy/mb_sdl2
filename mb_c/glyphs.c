#include "glyphs.h"

static SDL_Rect get_glyph_rect(GlyphType type) {
    SDL_Rect r = {0, 0, 0, 0};
    switch (type) {
        case GLYPH_SHOVEL_POINTER:
            r.x = 150; r.y = 140;
            r.w = 215 - 150 + 1; // 66
            r.h = 160 - 140 + 1; // 21
            break;
        case GLYPH_ARROW_POINTER:
            r.x = 205; r.y = 99;
            r.w = 231 - 205 + 1;
            r.h = 109 - 99 + 1;
            break;
    }
    return r;
}

void glyphs_init(Glyphs* glyphs, SDL_Texture* texture) {
    glyphs->texture = texture;
}

void glyphs_render(Glyphs* glyphs, SDL_Renderer* renderer, int x, int y, GlyphType type) {
    SDL_Rect src = get_glyph_rect(type);
    SDL_Rect dst = { x, y, src.w, src.h };
    SDL_RenderCopy(renderer, glyphs->texture, &src, &dst);
}

void glyphs_dimensions(GlyphType type, int* w, int* h) {
    SDL_Rect r = get_glyph_rect(type);
    if (w) *w = r.w;
    if (h) *h = r.h;
}
