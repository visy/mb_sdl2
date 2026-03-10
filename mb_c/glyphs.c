#include "glyphs.h"
#include <string.h>

static const int EQUIPMENT_GLYPHS[][2] = {
    {0, 170}, {30, 170}, {60, 170}, {216, 140}, {240, 170}, {210, 170}, {246, 140}, {270, 170}, {90, 170},
    {120, 170}, {246, 110}, {90, 140}, {150, 170}, {180, 170}, {276, 140}, {276, 110}, {216, 110}, {0, 140},
    {30, 140}, {60, 140}, {30, 40}, {232, 80}, {262, 80}, {0, 40}, {105, 40}, {60, 40}, {0, 90}
};

static const int UNMAPPED[2] = {50, 70};

static const int MAP_GLYPHS[][2] = {
    {0, 0}, {10, 0}, {20, 0}, {30, 0}, {40, 0}, {50, 0}, {60, 0}, {70, 0}, {80, 0}, {90, 0},
    {50, 70}, {50, 70}, {50, 70}, {50, 70}, {50, 70}, {50, 70}, {50, 70},
    {100, 0}, {110, 0}, {120, 0}, {130, 0}, {140, 0}, {150, 0},
    {50, 70}, {50, 70}, {50, 70}, {50, 70}, {50, 70}, {50, 70}, {50, 70}, {50, 70},
    {50, 70}, {50, 70}, {50, 70}, {50, 70}, {50, 70}, {50, 70}, {50, 70}, {50, 70},
    {0, 10}, {10, 10}, {20, 10}, {90, 10},
    {50, 70}, {50, 70}, {50, 70}, {50, 70}, {50, 70}, {50, 70},
    {100, 10}, {110, 10}, {120, 10}, {130, 10}, {140, 10}, {150, 10},
    {20, 30}, {30, 30}, {40, 30}, {50, 30}, {0, 30}, {10, 30}, {60, 30},
    {50, 70}, {70, 30}, {100, 30}, {90, 30}, {50, 70}, {150, 30},
    {50, 70}, {50, 70}, {50, 70},
    {0, 20}, {10, 20}, {20, 20},
    {50, 70}, {50, 70}, {110, 30}, {120, 30}, {130, 30},
    {40, 10}, {50, 10}, {60, 10}, {70, 10}, {80, 10}, {90, 10}, {90, 10}, {100, 10}, {110, 10},
    {50, 70}, {90, 10}, {50, 20}, {60, 20}, {70, 20}, {80, 20}, {90, 20}, {100, 20}, {110, 20},
    {120, 20}, {130, 20}, {140, 20}, {150, 20}, {160, 20}, {170, 20}, {180, 20}, {190, 20},
    {200, 20}, {210, 20}, {220, 20}, {230, 20}, {240, 20}, {250, 20}, {260, 20}, {270, 20},
    {280, 20}, {290, 20}, {40, 20}, {300, 20}, {310, 20}, {310, 20}, {310, 20}, {310, 20},
    {0, 0}, {140, 30}, {150, 40}, {0, 70}, {10, 70}, {20, 70}, {140, 40}, {90, 10}, {100, 10}, {110, 10},
    {136, 50}, {30, 70}, {40, 70}, {50, 70}
};

void glyphs_init(Glyphs* glyphs, SDL_Texture* texture) {
    glyphs->texture = texture;
}

static SDL_Rect get_glyph_rect(GlyphType type) {
    SDL_Rect r = {0, 0, 10, 10};

    if (type >= GLYPH_PLAYER_DIG_START) {
        int p_idx = type - GLYPH_PLAYER_DIG_START;
        int dir = p_idx % 4;
        int anim = (p_idx / 4) % 4;
        int base_x = 160;
        int base_y = 200;
        r.x = base_x + (dir * 40) + (anim * 10);
        r.y = base_y;
        return r;
    }

    if (type >= GLYPH_PLAYER_START) {
        int p_idx = type - GLYPH_PLAYER_START;
        int dir = p_idx % 4;
        int anim = (p_idx / 4) % 4;
        int base_x = 160;
        int base_y = 10;
        r.x = base_x + (dir * 40) + (anim * 10);
        r.y = base_y;
        return r;
    }

    if (type >= GLYPH_MAP_START) {
        int val = type - GLYPH_MAP_START;
        int idx = val - 0x30;
        if (idx >= 0 && idx < 135) {
            r.x = MAP_GLYPHS[idx][0];
            r.y = MAP_GLYPHS[idx][1];
        } else {
            r.x = UNMAPPED[0];
            r.y = UNMAPPED[1];
        }
        return r;
    }

    if (type >= GLYPH_EQUIPMENT_START) {
        int idx = type - GLYPH_EQUIPMENT_START;
        if (idx < 27) {
            r.x = EQUIPMENT_GLYPHS[idx][0];
            r.y = EQUIPMENT_GLYPHS[idx][1];
            r.w = 29; r.h = 29;
        }
        return r;
    }

    switch (type) {
        case GLYPH_SHOVEL_POINTER:
            r.x = 150; r.y = 140; r.w = 66; r.h = 21;
            break;
        case GLYPH_ARROW_POINTER:
            r.x = 205; r.y = 99; r.w = 27; r.h = 11;
            break;
        case GLYPH_SHOP_SLOT_UNSELECTED:
            r.x = 64; r.y = 92; r.w = 64; r.h = 48;
            break;
        case GLYPH_SHOP_SLOT_SELECTED:
            r.x = 128; r.y = 92; r.w = 64; r.h = 48;
            break;
        case GLYPH_READY:
            r.x = 120; r.y = 140; r.w = 30; r.h = 30;
            break;
        default: break;
    }
    return r;
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
