#include "editor.h"
#include "game.h"
#include "fonts.h"
#include "glyphs.h"
#include "input.h"
#include "context.h"
#include "compat.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <ctype.h>

// ==================== Level Editor ====================

// Toolbar layout from MINEDIT2.SPY (exact pixel coordinates from help screen guide lines)
#define ED_TB_H         30
#define ED_MAP_Y        ED_TB_H
#define ED_MAP_SIZE     (66 * 45)
#define ED_UNDO_MAX     64

// Left/right tile preview (display only)
#define ED_LTILE_X   5
#define ED_RTILE_X  18
#define ED_TILE_PY   5   // preview Y start (tiles are 10x10, shown at Y=5)

// Drawing tool buttons
#define ED_LINE_X1  36
#define ED_LINE_X2  51
#define ED_BOX_X1   60
#define ED_BOX_X2   77
#define ED_FILL_X1  87
#define ED_FILL_X2 101

// UNDO button
#define ED_UNDO_X1  97
#define ED_UNDO_X2 134

// Continuous draw indicator
#define ED_CONT_X  141
#define ED_CONT_Y   5
#define ED_CONT_W    8
#define ED_CONT_H    8

// Tile palette: 21 cols x 2 rows, 13px pitch, starts at x=155
#define ED_PAL_X1   155
#define ED_PAL_COLS   21
#define ED_PAL_SLOT   13
#define ED_PAL_ROW0_Y  4
#define ED_PAL_ROW1_Y 16
#define ED_PAL_X2    (ED_PAL_X1 + ED_PAL_COLS * ED_PAL_SLOT)

// Brush size slider
#define ED_BRUSH_X1  426
#define ED_BRUSH_X2  547

// File buttons (from right-side guide lines)
#define ED_NEW_X1   571
#define ED_NEW_X2   603
#define ED_SAVE_X1  604
#define ED_SAVE_X2  639

typedef enum { EDMODE_DOT, EDMODE_LINE, EDMODE_BOX, EDMODE_FILL } EditorMode;

// Tile palette for the editor: tiles available for placement
static const uint8_t EDITOR_TILES[] = {
    // Row 0 — matches EDITHELP.SPY palette layout exactly
    TILE_PASSAGE, TILE_SAND1, TILE_GRAVEL_LIGHT, TILE_GRAVEL_HEAVY,
    TILE_STONE_TOP_LEFT, TILE_STONE_TOP_RIGHT, TILE_STONE_BOTTOM_RIGHT, TILE_STONE_BOTTOM_LEFT,
    TILE_BOULDER, TILE_STONE1, TILE_WALL, TILE_BARREL,
    TILE_MINE, TILE_TELEPORT, TILE_GRENADIER_RIGHT, TILE_ALIEN_RIGHT,
    TILE_MEDIKIT, TILE_STONE_CRACKED_HEAVY, TILE_BRICK, TILE_BRICK_CRACKED_HEAVY, TILE_BUTTON_OFF,
    // Row 1
    TILE_SMALL_PICKAXE, TILE_LARGE_PICKAXE, TILE_DRILL,
    TILE_GOLD_SHIELD, TILE_GOLD_EGG, TILE_GOLD_PILE, TILE_GOLD_BRACELET,
    TILE_GOLD_BAR, TILE_GOLD_CROSS, TILE_GOLD_SCEPTER, TILE_GOLD_RUBIN, TILE_GOLD_CROWN,
    TILE_WEAPONS_CRATE, TILE_FURRY_RIGHT, TILE_SLIME_RIGHT,
    TILE_EXIT, TILE_BIOMASS, TILE_STONE_CRACKED_LIGHT, TILE_BRICK_CRACKED_LIGHT,
    TILE_PLASTIC, TILE_DOOR,
};
#define EDITOR_TILE_COUNT (int)(sizeof(EDITOR_TILES) / sizeof(EDITOR_TILES[0]))

// Treasure tiles for random placement (F3/Z)
static const uint8_t TREASURE_TILES[] = {
    TILE_GOLD_SHIELD, TILE_GOLD_EGG, TILE_GOLD_PILE, TILE_GOLD_BRACELET,
    TILE_GOLD_BAR, TILE_GOLD_CROSS, TILE_GOLD_SCEPTER, TILE_GOLD_RUBIN, TILE_GOLD_CROWN,
    TILE_DIAMOND,
};
#define TREASURE_COUNT (int)(sizeof(TREASURE_TILES) / sizeof(TREASURE_TILES[0]))

// Map monster spawner tile values to their monster glyph (right-facing, frame 0)
static GlyphType editor_tile_glyph(uint8_t val) {
    switch (val) {
        case TILE_FURRY_RIGHT:  case TILE_FURRY_LEFT:  case TILE_FURRY_UP:  case TILE_FURRY_DOWN:
            return (GlyphType)(GLYPH_MONSTER_FURRY + (val - TILE_FURRY_RIGHT));
        case TILE_GRENADIER_RIGHT: case TILE_GRENADIER_LEFT: case TILE_GRENADIER_UP: case TILE_GRENADIER_DOWN:
            return (GlyphType)(GLYPH_MONSTER_GRENADIER + (val - TILE_GRENADIER_RIGHT));
        case TILE_SLIME_RIGHT: case TILE_SLIME_LEFT: case TILE_SLIME_UP: case TILE_SLIME_DOWN:
            return (GlyphType)(GLYPH_MONSTER_SLIME + (val - TILE_SLIME_RIGHT));
        case TILE_ALIEN_RIGHT: case TILE_ALIEN_LEFT: case TILE_ALIEN_UP: case TILE_ALIEN_DOWN:
            return (GlyphType)(GLYPH_MONSTER_ALIEN + (val - TILE_ALIEN_RIGHT));
        default:
            return (GlyphType)(GLYPH_MAP_START + val);
    }
}

static void editor_render_map(App* app, ApplicationContext* ctx, uint8_t* tiles, int map_y) {
    SDL_SetRenderTarget(ctx->renderer, ctx->buffer);
    for (int y = 0; y < MAP_HEIGHT; y++) {
        for (int x = 0; x < MAP_WIDTH; x++) {
            uint8_t val = tiles[y * 66 + x];
            glyphs_render(&app->glyphs, ctx->renderer, x * TILE_SIZE, y * TILE_SIZE + map_y, editor_tile_glyph(val));
        }
    }
}

static void editor_render_cursor(ApplicationContext* ctx, int cx, int cy, int brush, int map_y) {
    SDL_SetRenderTarget(ctx->renderer, ctx->buffer);
    SDL_SetRenderDrawColor(ctx->renderer, 255, 255, 255, 255);
    float rad = brush * 0.5f;
    float rsq = rad * rad;
    int ri = (int)(rad + 1);
    for (int dy = -ri; dy <= ri; dy++)
        for (int dx = -ri; dx <= ri; dx++) {
            if ((float)(dx * dx + dy * dy) <= rsq) {
                int tx = cx + dx, ty = cy + dy;
                if (tx >= 1 && tx < MAP_WIDTH - 1 && ty >= 1 && ty < MAP_HEIGHT - 1) {
                    SDL_Rect rect = {tx * TILE_SIZE, ty * TILE_SIZE + map_y, TILE_SIZE, TILE_SIZE};
                    SDL_RenderDrawRect(ctx->renderer, &rect);
                }
            }
        }
}

// Auto-vary sand: when placing SAND1, randomly pick SAND1/SAND2/SAND3
static uint8_t editor_vary_tile(uint8_t tile) {
    if (tile == TILE_SAND1) {
        static const uint8_t sands[] = {TILE_SAND1, TILE_SAND2, TILE_SAND3};
        return sands[rand() % 3];
    }
    return tile;
}

static void editor_place_brush(uint8_t* tiles, int cx, int cy, int brush, uint8_t tile) {
    float rad = brush * 0.5f;
    float rsq = rad * rad;
    int ri = (int)(rad + 1);
    for (int dy = -ri; dy <= ri; dy++)
        for (int dx = -ri; dx <= ri; dx++) {
            if ((float)(dx * dx + dy * dy) <= rsq) {
                int tx = cx + dx, ty = cy + dy;
                if (tx >= 1 && tx < MAP_WIDTH - 1 && ty >= 1 && ty < MAP_HEIGHT - 1)
                    tiles[ty * 66 + tx] = editor_vary_tile(tile);
            }
        }
}

static void editor_draw_box(uint8_t* tiles, int x0, int y0, int x1, int y1, int brush, uint8_t tile) {
    (void)brush;
    int minx = x0 < x1 ? x0 : x1, maxx = x0 > x1 ? x0 : x1;
    int miny = y0 < y1 ? y0 : y1, maxy = y0 > y1 ? y0 : y1;
    if (minx < 1) minx = 1;
    if (maxx > MAP_WIDTH - 2) maxx = MAP_WIDTH - 2;
    if (miny < 1) miny = 1;
    if (maxy > MAP_HEIGHT - 2) maxy = MAP_HEIGHT - 2;
    for (int y = miny; y <= maxy; y++)
        for (int x = minx; x <= maxx; x++)
            tiles[y * 66 + x] = editor_vary_tile(tile);
}

static void editor_flood_fill(uint8_t* tiles, int sx, int sy, uint8_t tile) {
    uint8_t target = tiles[sy * 66 + sx];
    // For sand auto-vary, treat all sand variants as same target
    bool target_is_sand = (target == TILE_SAND1 || target == TILE_SAND2 || target == TILE_SAND3);
    bool fill_is_sand = (tile == TILE_SAND1);
    if (target == tile) return;
    if (target_is_sand && fill_is_sand) return;  // already sand
    typedef struct { int16_t x, y; } Pt;
    Pt* stack = (Pt*)malloc(MAP_WIDTH * MAP_HEIGHT * sizeof(Pt));
    if (!stack) return;
    int top = 0;
    stack[top++] = (Pt){(int16_t)sx, (int16_t)sy};
    tiles[sy * 66 + sx] = editor_vary_tile(tile);
    while (top > 0) {
        Pt p = stack[--top];
        const int dx[] = {0, 0, -1, 1};
        const int dy[] = {-1, 1, 0, 0};
        for (int d = 0; d < 4; d++) {
            int nx = p.x + dx[d], ny = p.y + dy[d];
            if (nx >= 1 && nx < MAP_WIDTH - 1 && ny >= 1 && ny < MAP_HEIGHT - 1) {
                uint8_t t = tiles[ny * 66 + nx];
                if (t == target || (target_is_sand && (t == TILE_SAND1 || t == TILE_SAND2 || t == TILE_SAND3))) {
                    tiles[ny * 66 + nx] = editor_vary_tile(tile);
                    stack[top++] = (Pt){(int16_t)nx, (int16_t)ny};
                }
            }
        }
    }
    free(stack);
}

static void editor_mirror_lr(uint8_t* tiles) {
    for (int y = 0; y < MAP_HEIGHT; y++)
        for (int x = 0; x < MAP_WIDTH / 2; x++)
            tiles[y * 66 + (MAP_WIDTH - 1 - x)] = tiles[y * 66 + x];
}

static void editor_add_random_treasures(uint8_t* tiles, int count) {
    for (int i = 0; i < count; i++) {
        int x = rand() % (MAP_WIDTH - 2) + 1;
        int y = rand() % (MAP_HEIGHT - 2) + 1;
        uint8_t cur = tiles[y * 66 + x];
        // Only place on sand/gravel/passage
        if (cur == TILE_PASSAGE || cur == TILE_SAND1 || cur == TILE_SAND2 || cur == TILE_SAND3 ||
            cur == TILE_GRAVEL_LIGHT || cur == TILE_GRAVEL_HEAVY) {
            tiles[y * 66 + x] = TREASURE_TILES[rand() % TREASURE_COUNT];
        }
    }
}

// Render toolbar: MINEDIT2.SPY background + dynamic overlays
static void editor_render_toolbar(App* app, ApplicationContext* ctx, int pal_scroll,
                                   int left_idx, int right_idx, EditorMode mode,
                                   bool continuous, int brush) {
    SDL_SetRenderTarget(ctx->renderer, ctx->buffer);

    // Blit MINEDIT2.SPY toolbar area
    if (app->edit_panel.texture) {
        SDL_Rect src = {0, 0, 640, ED_TB_H};
        SDL_Rect dst = {0, 0, 640, ED_TB_H};
        SDL_RenderCopy(ctx->renderer, app->edit_panel.texture, &src, &dst);
    } else {
        SDL_Rect bar = {0, 0, 640, ED_TB_H};
        SDL_SetRenderDrawColor(ctx->renderer, 30, 30, 30, 255);
        SDL_RenderFillRect(ctx->renderer, &bar);
    }

    // Left/right tile previews (display only at exact positions)
    // Left preview: left-click tile shown twice stacked
    glyphs_render(&app->glyphs, ctx->renderer, ED_LTILE_X, ED_TILE_PY, editor_tile_glyph(EDITOR_TILES[left_idx]));
    glyphs_render(&app->glyphs, ctx->renderer, ED_LTILE_X, ED_TILE_PY + 10, editor_tile_glyph(EDITOR_TILES[left_idx]));
    // Right preview: right-click tile shown twice stacked
    glyphs_render(&app->glyphs, ctx->renderer, ED_RTILE_X, ED_TILE_PY, editor_tile_glyph(EDITOR_TILES[right_idx]));
    glyphs_render(&app->glyphs, ctx->renderer, ED_RTILE_X, ED_TILE_PY + 10, editor_tile_glyph(EDITOR_TILES[right_idx]));

    // Highlight active drawing tool (line/box/fill) — DOT has no dedicated button
    {
        int hx = -1, hw = 0;
        if (mode == EDMODE_LINE) { hx = ED_LINE_X1; hw = ED_LINE_X2 - ED_LINE_X1; }
        else if (mode == EDMODE_BOX) { hx = ED_BOX_X1; hw = ED_BOX_X2 - ED_BOX_X1; }
        else if (mode == EDMODE_FILL) { hx = ED_FILL_X1; hw = ED_FILL_X2 - ED_FILL_X1; }
        if (hx >= 0) {
            SDL_Rect sel = {hx - 1, 3, hw + 2, 24};
            SDL_SetRenderDrawColor(ctx->renderer, 255, 255, 255, 255);
            SDL_RenderDrawRect(ctx->renderer, &sel);
        }
    }

    // Continuous drawing indicator light
    if (continuous) {
        SDL_Rect ci = {ED_CONT_X, ED_CONT_Y, ED_CONT_W, ED_CONT_H};
        SDL_SetRenderDrawColor(ctx->renderer, 0, 255, 0, 255);
        SDL_RenderFillRect(ctx->renderer, &ci);
    }

    // Tile palette: 21 cols x 2 rows rendered over the palette area
    for (int row = 0; row < 2; row++) {
        int base_y = (row == 0) ? ED_PAL_ROW0_Y : ED_PAL_ROW1_Y;
        for (int col = 0; col < ED_PAL_COLS; col++) {
            int idx = pal_scroll + row * ED_PAL_COLS + col;
            if (idx < 0 || idx >= EDITOR_TILE_COUNT) continue;
            int px = ED_PAL_X1 + col * ED_PAL_SLOT;
            glyphs_render(&app->glyphs, ctx->renderer, px, base_y, editor_tile_glyph(EDITOR_TILES[idx]));
            // Yellow highlight for left-selected tile
            if (idx == left_idx) {
                SDL_Rect sel = {px - 1, base_y - 1, 12, 12};
                SDL_SetRenderDrawColor(ctx->renderer, 255, 255, 0, 255);
                SDL_RenderDrawRect(ctx->renderer, &sel);
            }
            // Cyan highlight for right-selected tile
            if (idx == right_idx) {
                SDL_Rect sel = {px - 2, base_y - 2, 14, 14};
                SDL_SetRenderDrawColor(ctx->renderer, 0, 200, 255, 255);
                SDL_RenderDrawRect(ctx->renderer, &sel);
            }
        }
    }

    // Brush size handle on slider track
    {
        int slider_x = ED_BRUSH_X1 + 5;
        int slider_w = ED_BRUSH_X2 - ED_BRUSH_X1 - 10;
        int handle_x = slider_x + (brush - 1) * slider_w / 4;
        SDL_Rect handle = {handle_x - 2, 3, 5, 14};
        SDL_SetRenderDrawColor(ctx->renderer, 255, 255, 255, 255);
        SDL_RenderFillRect(ctx->renderer, &handle);
    }
}

static bool editor_save_level(uint8_t* tiles, const char* game_dir, const char* filename) {
    char path[MAX_PATH];
#ifdef _WIN32
    snprintf(path, sizeof(path), "%s\\%s", game_dir, filename);
#else
    snprintf(path, sizeof(path), "%s/%s", game_dir, filename);
#endif
    FILE* f = fopen(path, "wb");
    if (!f) return false;
    fwrite(tiles, 1, 66 * 45, f);
    fclose(f);
    return true;
}

static bool editor_load_level(uint8_t* tiles, const char* game_dir, const char* filename) {
    char path[MAX_PATH];
#ifdef _WIN32
    snprintf(path, sizeof(path), "%s\\%s", game_dir, filename);
#else
    snprintf(path, sizeof(path), "%s/%s", game_dir, filename);
#endif
    FILE* f = fopen(path, "rb");
    if (!f) return false;
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (size < 2970) { fclose(f); return false; }
    fread(tiles, 1, 66 * 45, f);
    fclose(f);
    return true;
}

// ==================== Shared text entry with on-screen keyboard ====================
// Supports keyboard typing AND gamepad with on-screen keyboard.
// OSK toggled with X button (weapon switch). Auto-lowercase after first uppercase char.
// flags: TEXT_UPPER = force all uppercase, TEXT_FILENAME = restrict to alnum/_/-

bool text_entry_dialog(App* app, ApplicationContext* ctx, char* out_buf, int max_len,
                              const char* prompt, const char* initial, int flags) {
    static const char* osk_upper[] = {
        "ABCDEFGHIJ",
        "KLMNOPQRST",
        "UVWXYZ0123",
        "456789_-  ",
    };
    static const char* osk_lower[] = {
        "abcdefghij",
        "klmnopqrst",
        "uvwxyz0123",
        "456789_-  ",
    };
    static const int OSK_ROWS = 4, OSK_COLS = 10;

    char buf[128] = "";
    int len = 0;
    if (initial && initial[0]) {
        snprintf(buf, sizeof(buf), "%s", initial);
        len = (int)strlen(buf);
    }
    if (len >= max_len - 1) len = max_len - 2;

    int osk_row = 0, osk_col = 0;
    // Start uppercase, auto-switch to lowercase after first character (unless TEXT_UPPER)
    bool osk_shift = false;
    bool result = false;

    SDL_StartTextInput();
    for (;;) {
        const char** osk_rows = ((flags & TEXT_UPPER) || osk_shift) ? osk_upper : osk_lower;

        // --- Render ---
        SDL_SetRenderTarget(ctx->renderer, ctx->buffer);
        SDL_Rect bg = {100, 150, 440, 180};
        SDL_SetRenderDrawColor(ctx->renderer, 0, 0, 60, 255);
        SDL_RenderFillRect(ctx->renderer, &bg);
        SDL_SetRenderDrawColor(ctx->renderer, 255, 255, 255, 255);
        SDL_RenderDrawRect(ctx->renderer, &bg);

        SDL_Color white = {255, 255, 255, 255};
        SDL_Color yellow = {255, 255, 0, 255};
        SDL_Color gray = {120, 120, 120, 255};
        SDL_Color cyan = {0, 255, 255, 255};
        SDL_Color green = {0, 200, 0, 255};

        render_text(ctx->renderer, &app->font, 110, 158, white, prompt);
        render_text(ctx->renderer, &app->font, 110, 176, yellow, buf);
        render_text(ctx->renderer, &app->font, 110 + len * 8, 176, white, "_");

        {
            int osk_x = 140, osk_y = 196;
            for (int r = 0; r < OSK_ROWS; r++) {
                for (int c = 0; c < OSK_COLS && osk_rows[r][c]; c++) {
                    char ch_str[2] = { osk_rows[r][c], 0 };
                    if (ch_str[0] == ' ') continue;
                    SDL_Color col = (r == osk_row && c == osk_col) ? cyan : gray;
                    if (r == osk_row && c == osk_col) {
                        SDL_SetRenderDrawColor(ctx->renderer, 0, 80, 80, 255);
                        SDL_Rect hr = {osk_x + c * 16 - 1, osk_y + r * 16, 12, 14};
                        SDL_RenderFillRect(ctx->renderer, &hr);
                    }
                    render_text(ctx->renderer, &app->font, osk_x + c * 16, osk_y + r * 16, col, ch_str);
                }
            }
            render_text(ctx->renderer, &app->font, 310, 196, gray, "A:TYPE  B:DEL");
            render_text(ctx->renderer, &app->font, 310, 210, gray, "START:OK");
            render_text(ctx->renderer, &app->font, 310, 224, gray, "BACK:CANCEL");
            if (!(flags & TEXT_UPPER)) {
                render_text(ctx->renderer, &app->font, 310, 238, gray, "Y:");
                render_text(ctx->renderer, &app->font, 310 + 16, 238, osk_shift ? green : gray, osk_shift ? "ABC" : "abc");
            }
            render_text(ctx->renderer, &app->font, 110, 270, gray, "ENTER:OK  ESC:CANCEL");
        }

        SDL_SetRenderTarget(ctx->renderer, NULL);
        context_present(ctx);

        // --- Input ---
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            app_handle_hotplug(app, ctx, &e);
            if (e.type == SDL_QUIT) { SDL_StopTextInput(); return false; }

            // Keyboard input
            if (e.type == SDL_KEYDOWN && !e.key.repeat) {
                if (e.key.keysym.scancode == SDL_SCANCODE_RETURN || e.key.keysym.scancode == SDL_SCANCODE_KP_ENTER) {
                    if (len > 0) { result = true; goto done; }
                }
                if (e.key.keysym.scancode == SDL_SCANCODE_ESCAPE) goto done;
                if (e.key.keysym.scancode == SDL_SCANCODE_BACKSPACE && len > 0) buf[--len] = '\0';
            }
            if (e.type == SDL_TEXTINPUT && len < max_len - 1) {
                for (const char* p = e.text.text; *p && len < max_len - 1; p++) {
                    char c = *p;
                    if (flags & TEXT_FILENAME) {
                        if (!((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
                              (c >= '0' && c <= '9') || c == '_' || c == '-')) continue;
                    } else {
                        if (c < 32 || c > 126) continue;
                    }
                    if (flags & TEXT_UPPER) { if (c >= 'a' && c <= 'z') c -= 32; }
                    buf[len++] = c;
                    buf[len] = '\0';
                }
            }

            // Gamepad buttons
            if (e.type == SDL_CONTROLLERBUTTONDOWN) {
                if (e.cbutton.button == SDL_CONTROLLER_BUTTON_DPAD_UP) osk_row = (osk_row + OSK_ROWS - 1) % OSK_ROWS;
                else if (e.cbutton.button == SDL_CONTROLLER_BUTTON_DPAD_DOWN) osk_row = (osk_row + 1) % OSK_ROWS;
                else if (e.cbutton.button == SDL_CONTROLLER_BUTTON_DPAD_LEFT) osk_col = (osk_col + OSK_COLS - 1) % OSK_COLS;
                else if (e.cbutton.button == SDL_CONTROLLER_BUTTON_DPAD_RIGHT) osk_col = (osk_col + 1) % OSK_COLS;
                else if (e.cbutton.button == SDL_CONTROLLER_BUTTON_A) {
                    char ch = osk_rows[osk_row][osk_col];
                    if (ch != ' ' && len < max_len - 1) {
                        buf[len++] = ch;
                        buf[len] = '\0';
                        if (!(flags & TEXT_UPPER) && len == 1) osk_shift = false;
                    }
                }
                else if (e.cbutton.button == SDL_CONTROLLER_BUTTON_B) {
                    if (len > 0) buf[--len] = '\0';
                    if (len == 0 && !(flags & TEXT_UPPER)) osk_shift = true;
                }
                else if (e.cbutton.button == SDL_CONTROLLER_BUTTON_Y && !(flags & TEXT_UPPER)) {
                    osk_shift = !osk_shift;
                }
                else if (e.cbutton.button == SDL_CONTROLLER_BUTTON_START) {
                    if (len > 0) { result = true; goto done; }
                }
                else if (e.cbutton.button == SDL_CONTROLLER_BUTTON_BACK) goto done;
            }
            // Left analog stick navigation
            if (e.type == SDL_CONTROLLERAXISMOTION) {
                int state = 0;
                if (e.caxis.value < -16000) state = -1;
                else if (e.caxis.value > 16000) state = 1;
                if (state != 0) {
                    if (e.caxis.axis == SDL_CONTROLLER_AXIS_LEFTY) {
                        if (state < 0) osk_row = (osk_row + OSK_ROWS - 1) % OSK_ROWS;
                        else osk_row = (osk_row + 1) % OSK_ROWS;
                    } else if (e.caxis.axis == SDL_CONTROLLER_AXIS_LEFTX) {
                        if (state < 0) osk_col = (osk_col + OSK_COLS - 1) % OSK_COLS;
                        else osk_col = (osk_col + 1) % OSK_COLS;
                    }
                }
            }
        }
        // Skip over blank cells
        while (osk_rows[osk_row][osk_col] == ' ') {
            osk_col = (osk_col + 1) % OSK_COLS;
            if (osk_col == 0) osk_row = (osk_row + 1) % OSK_ROWS;
        }
        SDL_Delay(16);
    }
done:
    SDL_StopTextInput();
    if (result) {
        snprintf(out_buf, max_len, "%s", buf);
    }
    return result;
}

// Editor name dialog: asks for filename, auto-appends .MNL (has exit tiles) or .MNE (no exit tiles)
static bool editor_name_dialog(App* app, ApplicationContext* ctx, char* out_name, int max_len,
                               const char* prompt, const uint8_t* tiles) {
    if (!text_entry_dialog(app, ctx, out_name, max_len, prompt, NULL, TEXT_UPPER | TEXT_FILENAME))
        return false;
    // Auto-append extension if not present
    if (!strstr(out_name, ".MN") && !strstr(out_name, ".mn")) {
        int slen = (int)strlen(out_name);
        bool has_exit = false;
        if (tiles) {
            for (int i = 0; i < 66 * 45; i++)
                if (tiles[i] == TILE_EXIT) { has_exit = true; break; }
        }
        if (slen + 4 < max_len) strcat(out_name, has_exit ? ".MNL" : ".MNE");
    }
    return true;
}

// File browser for loading levels
static bool editor_file_browser(App* app, ApplicationContext* ctx, char* out_name, int max_len) {
    // Build file list from game_dir
    char files[256][32];
    int count = 0;

    DIR* d = opendir(ctx->game_dir);
    if (d) {
        struct dirent* dir;
        while ((dir = readdir(d)) != NULL && count < 256) {
            char* ext = strrchr(dir->d_name, '.');
            if (ext && (STRICMP(ext, ".MNL") == 0 || STRICMP(ext, ".MNE") == 0)) {
                snprintf(files[count], 32, "%s", dir->d_name);
                count++;
            }
        }
        closedir(d);
    }
    if (count == 0) return false;

    int selected = 0, scroll = 0;
    int axis_y_state = 0;
    for (;;) {
        SDL_SetRenderTarget(ctx->renderer, ctx->buffer);
        SDL_SetRenderDrawColor(ctx->renderer, 0, 0, 40, 255);
        SDL_RenderClear(ctx->renderer);
        SDL_Color white = {255, 255, 255, 255};
        SDL_Color yellow = {255, 255, 0, 255};
        SDL_Color gray = {120, 120, 120, 255};
        render_text(ctx->renderer, &app->font, 240, 10, white, "LOAD LEVEL");
        int visible = 25;
        if (selected < scroll) scroll = selected;
        if (selected >= scroll + visible) scroll = selected - visible + 1;
        for (int i = 0; i < visible && (scroll + i) < count; i++) {
            int idx = scroll + i;
            int y = 30 + i * 16;
            render_text(ctx->renderer, &app->font, 50, y, (idx == selected) ? yellow : white, files[idx]);
            if (idx == selected) render_text(ctx->renderer, &app->font, 30, y, yellow, ">");
        }
        render_text(ctx->renderer, &app->font, 50, 440, gray, "UP/DOWN:BROWSE  ENTER/A:LOAD  ESC/B:CANCEL");
        char cinfo[32];
        snprintf(cinfo, sizeof(cinfo), "%d FILES", count);
        render_text(ctx->renderer, &app->font, 450, 10, white, cinfo);
        SDL_SetRenderTarget(ctx->renderer, NULL);
        context_present(ctx);

        SDL_Event e;
        bool got_input = false;
        while (!got_input) {
            while (SDL_PollEvent(&e)) {
                app_handle_hotplug(app, ctx, &e);
                if (e.type == SDL_QUIT) return false;
                if (e.type == SDL_KEYDOWN && !e.key.repeat) {
                    got_input = true;
                    if (e.key.keysym.scancode == SDL_SCANCODE_UP) selected = (selected + count - 1) % count;
                    else if (e.key.keysym.scancode == SDL_SCANCODE_DOWN) selected = (selected + 1) % count;
                    else if (e.key.keysym.scancode == SDL_SCANCODE_RETURN || e.key.keysym.scancode == SDL_SCANCODE_KP_ENTER) {
                        snprintf(out_name, max_len, "%s", files[selected]);
                        return true;
                    }
                    else if (e.key.keysym.scancode == SDL_SCANCODE_ESCAPE) return false;
                }
                if (e.type == SDL_CONTROLLERBUTTONDOWN) {
                    got_input = true;
                    if (e.cbutton.button == SDL_CONTROLLER_BUTTON_DPAD_UP) selected = (selected + count - 1) % count;
                    else if (e.cbutton.button == SDL_CONTROLLER_BUTTON_DPAD_DOWN) selected = (selected + 1) % count;
                    else if (e.cbutton.button == SDL_CONTROLLER_BUTTON_A) {
                        snprintf(out_name, max_len, "%s", files[selected]);
                        return true;
                    }
                    else if (e.cbutton.button == SDL_CONTROLLER_BUTTON_B || e.cbutton.button == SDL_CONTROLLER_BUTTON_BACK) return false;
                }
                if (e.type == SDL_CONTROLLERAXISMOTION && e.caxis.axis == SDL_CONTROLLER_AXIS_LEFTY) {
                    int state = 0;
                    if (e.caxis.value < -16000) state = -1;
                    else if (e.caxis.value > 16000) state = 1;
                    if (state != axis_y_state) {
                        axis_y_state = state;
                        if (state != 0) {
                            got_input = true;
                            if (state < 0) selected = (selected + count - 1) % count;
                            else selected = (selected + 1) % count;
                        }
                    }
                }
            }
            SDL_Delay(16);
        }
    }
}

static void editor_push_undo(uint8_t undo_buf[][ED_MAP_SIZE], int* undo_top, int* undo_count, uint8_t* tiles) {
    memcpy(undo_buf[*undo_top], tiles, ED_MAP_SIZE);
    *undo_top = (*undo_top + 1) % ED_UNDO_MAX;
    if (*undo_count < ED_UNDO_MAX) (*undo_count)++;
}

static bool editor_pop_undo(uint8_t undo_buf[][ED_MAP_SIZE], int* undo_top, int* undo_count, uint8_t* tiles) {
    if (*undo_count == 0) return false;
    *undo_top = (*undo_top + ED_UNDO_MAX - 1) % ED_UNDO_MAX;
    (*undo_count)--;
    memcpy(tiles, undo_buf[*undo_top], ED_MAP_SIZE);
    return true;
}

static void editor_new_level(uint8_t* tiles) {
    memset(tiles, TILE_WALL, ED_MAP_SIZE);
    for (int y = 0; y < 45; y++) { tiles[y * 66 + 64] = 0; tiles[y * 66 + 65] = 0; }
    for (int y = 1; y < 44; y++)
        for (int x = 1; x < 63; x++)
            tiles[y * 66 + x] = editor_vary_tile(TILE_SAND1);
}

// Convert window mouse coords to buffer coords
static void editor_mouse_to_buf(ApplicationContext* ctx, int mx, int my, int* bx, int* by) {
    *bx = (mx - ctx->viewport.x) * 640 / ctx->viewport.w;
    *by = (my - ctx->viewport.y) * 480 / ctx->viewport.h;
}

void app_run_editor(App* app, ApplicationContext* ctx) {
    uint8_t tiles[ED_MAP_SIZE];
    editor_new_level(tiles);

    uint8_t (*undo_buf)[ED_MAP_SIZE] = (uint8_t(*)[ED_MAP_SIZE])malloc(ED_UNDO_MAX * ED_MAP_SIZE);
    int undo_top = 0, undo_count = 0;

    int cx = 1, cy = 1;
    int left_idx = 1, right_idx = 0; // left=sand, right=passage
    int brush = 1;
    EditorMode mode = EDMODE_DOT;
    bool continuous = false;
    bool mouse_drawing = false;
    int mark_x = -1, mark_y = -1, box_drag_idx = 0;
    char filename[64] = "NEWLEVEL.MNL";
    int pal_scroll = 0; // first tile index shown in palette

    Uint32 last_move = 0; // timestamp for held-key repeat suppression
    bool move_repeating = false; // true after initial delay has passed

    #define ED_UNDO_SAVE() editor_push_undo(undo_buf, &undo_top, &undo_count, tiles)
    ED_UNDO_SAVE();

    bool need_redraw = true;
    bool running = true;
    while (running) {
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            app_handle_hotplug(app, ctx, &e);
            if (e.type == SDL_QUIT) { running = false; break; }

            // --- Pause menu ---
            if (is_pause_event(&e, &app->input_config)) {
                PauseChoice pc = pause_menu(app, ctx, PAUSE_CTX_EDITOR);
                switch (pc) {
                    case PAUSE_ED_NEW:
                        ED_UNDO_SAVE(); editor_new_level(tiles);
                        snprintf(filename, sizeof(filename), "NEWLEVEL.MNL"); 
                        break;
                    case PAUSE_ED_LOAD: {
                        char ln[64];
                        if (editor_file_browser(app, ctx, ln, sizeof(ln))) {
                            ED_UNDO_SAVE();
                            if (editor_load_level(tiles, ctx->game_dir, ln)) {
                                snprintf(filename, sizeof(filename), "%s", ln); 
                            }
                        }
                        break;
                    }
                    case PAUSE_ED_SAVE:
                        if (strcmp(filename, "NEWLEVEL.MNL") == 0) {
                            char nn[64];
                            if (editor_name_dialog(app, ctx, nn, sizeof(nn), "SAVE AS (ENTER NAME):", tiles))
                                snprintf(filename, sizeof(filename), "%s", nn);
                            else break;
                        }
                        editor_save_level(tiles, ctx->game_dir, filename);
                        break;
                    case PAUSE_ED_SAVE_QUIT:
                        if (strcmp(filename, "NEWLEVEL.MNL") == 0) {
                            char nn[64];
                            if (editor_name_dialog(app, ctx, nn, sizeof(nn), "SAVE AS (ENTER NAME):", tiles))
                                snprintf(filename, sizeof(filename), "%s", nn);
                            else break;
                        }
                        editor_save_level(tiles, ctx->game_dir, filename);
                        running = false;
                        break;
                    case PAUSE_ED_QUIT:
                        running = false;
                        break;
                    default: break; // PAUSE_NONE = resume
                }
                need_redraw = true;
                continue;
            }

            // --- Mouse: toolbar clicks ---
            if (e.type == SDL_MOUSEBUTTONDOWN) {
                int bx, by;
                editor_mouse_to_buf(ctx, e.button.x, e.button.y, &bx, &by);

                if (by < ED_TB_H) {
                    // Line tool (x 36..51)
                    if (bx >= ED_LINE_X1 && bx <= ED_LINE_X2) {
                        mode = EDMODE_LINE; mark_x = -1; need_redraw = true;
                    }
                    // Box tool (x 60..77)
                    else if (bx >= ED_BOX_X1 && bx <= ED_BOX_X2) {
                        mode = EDMODE_BOX; mark_x = -1; need_redraw = true;
                    }
                    // Fill tool (x 87..101)
                    else if (bx >= ED_FILL_X1 && bx <= ED_FILL_X2) {
                        mode = EDMODE_FILL; mark_x = -1; need_redraw = true;
                    }
                    // UNDO (x 97..134)
                    else if (bx >= ED_UNDO_X1 && bx <= ED_UNDO_X2) {
                        if (editor_pop_undo(undo_buf, &undo_top, &undo_count, tiles))
                            
                        need_redraw = true;
                    }
                    // Continuous draw indicator toggle (x ~141)
                    else if (bx >= ED_CONT_X - 2 && bx <= ED_CONT_X + ED_CONT_W + 2 && by >= 2 && by <= 26) {
                        continuous = !continuous;
                        need_redraw = true;
                    }
                    // Tile palette (x 155+, 2 rows)
                    else if (bx >= ED_PAL_X1 && bx < ED_PAL_X1 + ED_PAL_COLS * ED_PAL_SLOT) {
                        int col = (bx - ED_PAL_X1) / ED_PAL_SLOT;
                        int row = (by < 15) ? 0 : 1;
                        int idx = pal_scroll + row * ED_PAL_COLS + col;
                        if (col >= 0 && col < ED_PAL_COLS && idx >= 0 && idx < EDITOR_TILE_COUNT) {
                            if (e.button.button == SDL_BUTTON_RIGHT) right_idx = idx;
                            else left_idx = idx;
                        }
                        need_redraw = true;
                    }
                    // Brush size slider (x 426..547)
                    else if (bx >= ED_BRUSH_X1 && bx <= ED_BRUSH_X2) {
                        int rel = bx - ED_BRUSH_X1 - 5;
                        int range = ED_BRUSH_X2 - ED_BRUSH_X1 - 10;
                        brush = 1 + rel * 4 / range;
                        if (brush < 1) brush = 1;
                        if (brush > 5) brush = 5;
                        need_redraw = true;
                    }
                    // NEW (x 571..603, top half)
                    else if (bx >= ED_NEW_X1 && bx <= ED_NEW_X2 && by < 14) {
                        ED_UNDO_SAVE();
                        editor_new_level(tiles);
                        snprintf(filename, sizeof(filename), "NEWLEVEL.MNL");
                        
                        need_redraw = true;
                    }
                    // LOAD (x 571..603, bottom half)
                    else if (bx >= ED_NEW_X1 && bx <= ED_NEW_X2 && by >= 14) {
                        char load_name[64];
                        if (editor_file_browser(app, ctx, load_name, sizeof(load_name))) {
                            ED_UNDO_SAVE();
                            if (editor_load_level(tiles, ctx->game_dir, load_name)) {
                                snprintf(filename, sizeof(filename), "%s", load_name);
                                
                            }
                        }
                        need_redraw = true;
                    }
                    // SAVE (x 604..639, top half)
                    else if (bx >= ED_SAVE_X1 && bx <= ED_SAVE_X2 && by < 14) {
                        editor_save_level(tiles, ctx->game_dir, filename);
                        need_redraw = true;
                    }
                    // SAVE AS (x 604..639, bottom half)
                    else if (bx >= ED_SAVE_X1 && bx <= ED_SAVE_X2 && by >= 14) {
                        char new_name[64];
                        if (editor_name_dialog(app, ctx, new_name, sizeof(new_name), "SAVE AS (ENTER NAME):", tiles)) {
                            snprintf(filename, sizeof(filename), "%s", new_name);
                            editor_save_level(tiles, ctx->game_dir, filename);
                        }
                        need_redraw = true;
                    }
                }
                // --- Mouse: map clicks ---
                else if (by >= ED_MAP_Y && by < ED_MAP_Y + MAP_HEIGHT * TILE_SIZE) {
                    int tx = bx / TILE_SIZE;
                    int ty = (by - ED_MAP_Y) / TILE_SIZE;
                    if (tx >= 0 && tx < MAP_WIDTH && ty >= 0 && ty < MAP_HEIGHT) {
                        cx = tx; cy = ty;
                        int idx = (e.button.button == SDL_BUTTON_RIGHT) ? right_idx : left_idx;
                        uint8_t tile = EDITOR_TILES[idx];
                        if (mode == EDMODE_FILL) {
                            ED_UNDO_SAVE();
                            editor_flood_fill(tiles, cx, cy, tile);
                            
                        } else if (mode == EDMODE_BOX) {
                            // Start drag for rectangle
                            mark_x = cx; mark_y = cy;
                            box_drag_idx = idx;
                            mouse_drawing = true;
                        } else {
                            // DOT and LINE modes: continuous paintbrush
                            ED_UNDO_SAVE();
                            editor_place_brush(tiles, cx, cy, brush, tile);
                            
                            mouse_drawing = true;
                        }
                        need_redraw = true;
                    }
                }
            }
            // Mouse drag on map
            if (e.type == SDL_MOUSEMOTION && mouse_drawing) {
                int bx, by;
                editor_mouse_to_buf(ctx, e.motion.x, e.motion.y, &bx, &by);
                if (by >= ED_MAP_Y && by < ED_MAP_Y + MAP_HEIGHT * TILE_SIZE) {
                    int tx = bx / TILE_SIZE, ty = (by - ED_MAP_Y) / TILE_SIZE;
                    if (tx >= 0 && tx < MAP_WIDTH && ty >= 0 && ty < MAP_HEIGHT && (tx != cx || ty != cy)) {
                        cx = tx; cy = ty;
                        if (mode == EDMODE_BOX && mark_x >= 0) {
                            // Just update cursor for live preview
                            need_redraw = true;
                        } else {
                            Uint32 btns = SDL_GetMouseState(NULL, NULL);
                            int idx = (btns & SDL_BUTTON_RMASK) ? right_idx : left_idx;
                            editor_place_brush(tiles, cx, cy, brush, EDITOR_TILES[idx]);
                            
                            need_redraw = true;
                        }
                    }
                }
            }
            if (e.type == SDL_MOUSEBUTTONUP) {
                if (mouse_drawing && mode == EDMODE_BOX && mark_x >= 0) {
                    // Commit the rectangle on mouse release
                    ED_UNDO_SAVE();
                    editor_draw_box(tiles, mark_x, mark_y, cx, cy, brush, EDITOR_TILES[box_drag_idx]);
                    mark_x = -1; mark_y = -1;
                    
                    need_redraw = true;
                }
                mouse_drawing = false;
            }

            // Mouse wheel scrolls palette
            if (e.type == SDL_MOUSEWHEEL) {
                int bx, by;
                int mx, my; SDL_GetMouseState(&mx, &my);
                editor_mouse_to_buf(ctx, mx, my, &bx, &by);
                if (by < ED_TB_H && bx >= ED_PAL_X1 && bx <= ED_PAL_X2) {
                    pal_scroll -= e.wheel.y * ED_PAL_COLS;
                    if (pal_scroll < 0) pal_scroll = 0;
                    int max_scroll = EDITOR_TILE_COUNT - ED_PAL_COLS * 2;
                    if (max_scroll < 0) max_scroll = 0;
                    if (pal_scroll > max_scroll) pal_scroll = max_scroll;
                    need_redraw = true;
                }
            }

            // --- Joypad / player 0 mapped input ---
            {
                // Skip mapped input when Ctrl/Shift/Alt held (those are keyboard-only shortcuts)
                bool has_mod = (e.type == SDL_KEYDOWN && (SDL_GetModState() & (KMOD_CTRL | KMOD_SHIFT | KMOD_ALT)));
                ActionType act = has_mod ? ACT_MAX_PLAYER : input_map_event(&e, 0, &app->input_config);
                if (act != ACT_MAX_PLAYER) {
                    int prev_cx = cx, prev_cy = cy;
                    switch (act) {
                        case ACT_UP:    if (cy > 0) cy--; break;
                        case ACT_DOWN:  if (cy < MAP_HEIGHT - 1) cy++; break;
                        case ACT_LEFT:  if (cx > 0) cx--; break;
                        case ACT_RIGHT: if (cx < MAP_WIDTH - 1) cx++; break;
                        case ACT_ACTION: {
                            uint8_t tile = EDITOR_TILES[left_idx];
                            if (mode == EDMODE_FILL) { ED_UNDO_SAVE(); editor_flood_fill(tiles, cx, cy, tile);  }
                            else if (mode == EDMODE_BOX) {
                                if (mark_x < 0) { mark_x = cx; mark_y = cy; }
                                else { ED_UNDO_SAVE(); editor_draw_box(tiles, mark_x, mark_y, cx, cy, brush, tile); mark_x = -1; mark_y = -1;  }
                            } else { ED_UNDO_SAVE(); editor_place_brush(tiles, cx, cy, brush, tile);  }
                            need_redraw = true; break;
                        }
                        case ACT_STOP: {
                            ED_UNDO_SAVE(); editor_place_brush(tiles, cx, cy, brush, EDITOR_TILES[right_idx]);
                             need_redraw = true; break;
                        }
                        case ACT_CYCLE: {
                            // Cycle mode: dot→line→box→fill→dot
                            mode = (EditorMode)((mode + 1) % 4); mark_x = -1; need_redraw = true; break;
                        }
                        case ACT_REMOTE: {
                            // Cycle left tile forward
                            left_idx = (left_idx + 1) % EDITOR_TILE_COUNT; need_redraw = true; break;
                        }
                        default: break;
                    }
                    if (cx != prev_cx || cy != prev_cy) {
                        last_move = SDL_GetTicks();
                        move_repeating = false;
                        if (continuous) { editor_place_brush(tiles, cx, cy, brush, EDITOR_TILES[left_idx]);  }
                        need_redraw = true;
                    }
                }
            }

            // --- Keyboard-only shortcuts (not duplicated by input_map_event) ---
            if (e.type == SDL_KEYDOWN && !e.key.repeat) {
                switch (e.key.keysym.scancode) {
                    case SDL_SCANCODE_PAGEUP: if (left_idx > 0) left_idx--; need_redraw = true; break;
                    case SDL_SCANCODE_PAGEDOWN: if (left_idx < EDITOR_TILE_COUNT - 1) left_idx++; need_redraw = true; break;
                    case SDL_SCANCODE_COMMA: if (right_idx > 0) right_idx--; need_redraw = true; break;
                    case SDL_SCANCODE_PERIOD: if (right_idx < EDITOR_TILE_COUNT - 1) right_idx++; need_redraw = true; break;

                    case SDL_SCANCODE_SPACE: continuous = !continuous; need_redraw = true; break;

                    case SDL_SCANCODE_TAB: {
                        uint8_t under = tiles[cy * 66 + cx];
                        for (int i = 0; i < EDITOR_TILE_COUNT; i++)
                            if (EDITOR_TILES[i] == under) {
                                if (SDL_GetModState() & KMOD_SHIFT) right_idx = i; else left_idx = i; break;
                            }
                        need_redraw = true; break;
                    }

                    case SDL_SCANCODE_1: mode = EDMODE_DOT; mark_x = -1; need_redraw = true; break;
                    case SDL_SCANCODE_2: mode = EDMODE_LINE; mark_x = -1; need_redraw = true; break;
                    case SDL_SCANCODE_3: mode = EDMODE_BOX; mark_x = -1; need_redraw = true; break;
                    case SDL_SCANCODE_4: mode = EDMODE_FILL; mark_x = -1; need_redraw = true; break;

                    case SDL_SCANCODE_F1:
                        if (app->edit_help.texture) { context_render_texture(ctx, app->edit_help.texture); context_present(ctx); context_wait_key_pressed(ctx); }
                        need_redraw = true; break;
                    case SDL_SCANCODE_F2: ED_UNDO_SAVE(); editor_mirror_lr(tiles);  need_redraw = true; break;
                    case SDL_SCANCODE_F3: ED_UNDO_SAVE(); editor_add_random_treasures(tiles, 20);  need_redraw = true; break;
                    case SDL_SCANCODE_F7: if (brush > 1) brush--; need_redraw = true; break;
                    case SDL_SCANCODE_F8: if (brush < 5) brush++; need_redraw = true; break;
                    case SDL_SCANCODE_F9: ED_UNDO_SAVE(); editor_new_level(tiles);  need_redraw = true; break;

                    case SDL_SCANCODE_Z:
                        if (SDL_GetModState() & KMOD_CTRL) {
                            if (editor_pop_undo(undo_buf, &undo_top, &undo_count, tiles)) {  need_redraw = true; }
                        }
                        break;
                    case SDL_SCANCODE_U:
                        if (editor_pop_undo(undo_buf, &undo_top, &undo_count, tiles)) {  need_redraw = true; }
                        break;

                    case SDL_SCANCODE_S:
                        if (SDL_GetModState() & KMOD_CTRL) {
                            editor_save_level(tiles, ctx->game_dir, filename);
                            need_redraw = true;
                        } else if (SDL_GetModState() & KMOD_SHIFT) {
                            char nn[64]; if (editor_name_dialog(app, ctx, nn, sizeof(nn), "SAVE AS (ENTER NAME):", tiles)) {
                                snprintf(filename, sizeof(filename), "%s", nn);
                                editor_save_level(tiles, ctx->game_dir, filename);
                            } need_redraw = true;
                        }
                        break;
                    case SDL_SCANCODE_L:
                        if (SDL_GetModState() & KMOD_CTRL) {
                            char ln[64]; if (editor_file_browser(app, ctx, ln, sizeof(ln))) {
                                ED_UNDO_SAVE(); if (editor_load_level(tiles, ctx->game_dir, ln)) {
                                    snprintf(filename, sizeof(filename), "%s", ln); 
                                }
                            } need_redraw = true;
                        }
                        break;
                    case SDL_SCANCODE_N:
                        if (SDL_GetModState() & KMOD_CTRL) {
                            ED_UNDO_SAVE(); editor_new_level(tiles);
                            snprintf(filename, sizeof(filename), "NEWLEVEL.MNL");  need_redraw = true;
                        }
                        break;

                    default: break;
                }
            }
        }

        // Held-key / held-stick cursor repeat (300ms initial delay, then 50ms repeat)
        {
            Uint32 now = SDL_GetTicks();
            Uint32 delay = move_repeating ? 50 : 300;
            if (now - last_move > delay) {
                bool hu = input_action_held(&app->input_config, 0, ACT_UP);
                bool hd = input_action_held(&app->input_config, 0, ACT_DOWN);
                bool hl = input_action_held(&app->input_config, 0, ACT_LEFT);
                bool hr = input_action_held(&app->input_config, 0, ACT_RIGHT);
                if (hu || hd || hl || hr) {
                    int prev_cx = cx, prev_cy = cy;
                    if (hu) { if (cy > 0) cy--; }
                    if (hd) { if (cy < MAP_HEIGHT - 1) cy++; }
                    if (hl) { if (cx > 0) cx--; }
                    if (hr) { if (cx < MAP_WIDTH - 1) cx++; }
                    if (cx != prev_cx || cy != prev_cy) {
                        last_move = now;
                        move_repeating = true;
                        if (continuous) { editor_place_brush(tiles, cx, cy, brush, EDITOR_TILES[left_idx]);  }
                        need_redraw = true;
                    }
                } else {
                    move_repeating = false;
                }
            }
        }

        if (need_redraw) {
            need_redraw = false;
            SDL_SetRenderTarget(ctx->renderer, ctx->buffer);
            SDL_SetRenderDrawColor(ctx->renderer, 0, 0, 0, 255);
            SDL_RenderClear(ctx->renderer);

            // Map
            editor_render_map(app, ctx, tiles, ED_MAP_Y);

            // Box preview (two-click rectangle)
            if (mode == EDMODE_BOX && mark_x >= 0) {
                SDL_SetRenderTarget(ctx->renderer, ctx->buffer);
                SDL_SetRenderDrawColor(ctx->renderer, 255, 255, 0, 255);
                int x0 = (mark_x < cx ? mark_x : cx) * TILE_SIZE;
                int y0 = (mark_y < cy ? mark_y : cy) * TILE_SIZE + ED_MAP_Y;
                int x1 = (mark_x > cx ? mark_x : cx) * TILE_SIZE + TILE_SIZE;
                int y1 = (mark_y > cy ? mark_y : cy) * TILE_SIZE + ED_MAP_Y + TILE_SIZE;
                SDL_Rect pr = {x0, y0, x1 - x0, y1 - y0};
                SDL_RenderDrawRect(ctx->renderer, &pr);
            }

            // Toolbar (MINEDIT2.SPY bg + overlays)
            editor_render_toolbar(app, ctx, pal_scroll, left_idx, right_idx, mode, continuous, brush);

            // Cursor
            editor_render_cursor(ctx, cx, cy, brush, ED_MAP_Y);

            SDL_SetRenderTarget(ctx->renderer, NULL);
            context_present(ctx);
        }
        SDL_Delay(16);
    }

    free(undo_buf);
    #undef ED_UNDO_SAVE
}
