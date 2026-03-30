#include "menus.h"
#include "persist.h"
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

SelectedMenu menu_next(SelectedMenu current) {
    return (SelectedMenu)((current + 1) % 4);
}

SelectedMenu menu_prev(SelectedMenu current) {
    return (SelectedMenu)((current + 3) % 4);
}

static void get_shovel_pos(SelectedMenu menu, int* x, int* y) {
    *x = 222;
    *y = 136 + 48 * menu;
}

void render_main_menu(App* app, ApplicationContext* ctx, SelectedMenu selected) {
    SDL_SetRenderTarget(ctx->renderer, ctx->buffer);
    SDL_RenderCopy(ctx->renderer, app->main_menu.texture, NULL, NULL);
    int pos = (26 - strlen(app->registered)) * 4 + 254;
    SDL_Color* pal = app->main_menu.palette;
    render_text(ctx->renderer, &app->font, pos - 1, 437, pal[10], app->registered);
    render_text(ctx->renderer, &app->font, pos + 1, 437, pal[8], app->registered);
    render_text(ctx->renderer, &app->font, pos, 437, pal[0], app->registered);
    int sx, sy; get_shovel_pos(selected, &sx, &sy);
    glyphs_render(&app->glyphs, ctx->renderer, sx, sy, GLYPH_SHOVEL_POINTER);
    // Hint text above registered line, centered on menu area
    SDL_Color hint_col = pal[8];
    int menu_cx = 358;
    int line_y = 376;
#ifdef MB_NET
    const char* h1 = "F1:NETWORK GAME";
    render_text(ctx->renderer, &app->font, menu_cx - (int)strlen(h1) * 4, line_y, hint_col, h1);
    line_y += 10;
#endif
    const char* h2 = "F2/SELECT:LEVEL EDITOR";
    render_text(ctx->renderer, &app->font, menu_cx - (int)strlen(h2) * 4, line_y, hint_col, h2);
    SDL_SetRenderTarget(ctx->renderer, NULL);
}

void update_shovel(App* app, ApplicationContext* ctx, SelectedMenu previous, SelectedMenu selected) {
    SDL_SetRenderTarget(ctx->renderer, ctx->buffer);
    int old_x, old_y; get_shovel_pos(previous, &old_x, &old_y);
    int w, h; glyphs_dimensions(GLYPH_SHOVEL_POINTER, &w, &h);
    SDL_SetRenderDrawColor(ctx->renderer, 0, 0, 0, 255);
    SDL_Rect r = { old_x, old_y, w, h }; SDL_RenderFillRect(ctx->renderer, &r);
    int nx, ny; get_shovel_pos(selected, &nx, &ny);
    glyphs_render(&app->glyphs, ctx->renderer, nx, ny, GLYPH_SHOVEL_POINTER);
    SDL_SetRenderTarget(ctx->renderer, NULL); context_present(ctx);
}

void app_run_sound_test(App* app, ApplicationContext* ctx) {
    if (app->sound_count == 0) return;
    int selected = 0; bool running = true;
    while (running) {
        SDL_SetRenderTarget(ctx->renderer, ctx->buffer); SDL_SetRenderDrawColor(ctx->renderer, 0, 0, 0, 255); SDL_RenderClear(ctx->renderer);
        SDL_Color white = {255, 255, 255, 255}, yellow = {255, 255, 0, 255};
        render_text(ctx->renderer, &app->font, 240, 20, white, "SOUND TEST");
        render_text(ctx->renderer, &app->font, 20, 450, white, "UP/DOWN: BROWSE  ENTER: PLAY  ESC: BACK");
        int start_idx = selected - 10; if (start_idx < 0) start_idx = 0;
        for (int i = 0; i < 20 && (start_idx + i) < app->sound_count; ++i) {
            int idx = start_idx + i;
            render_text(ctx->renderer, &app->font, 50, 60 + i * 18, (idx == selected) ? yellow : white, app->sound_names[idx]);
            if (idx == selected) render_text(ctx->renderer, &app->font, 30, 60 + i * 18, yellow, ">");
        }
        SDL_SetRenderTarget(ctx->renderer, NULL); context_present(ctx);
        SDL_Scancode key = context_wait_key_pressed(ctx);
        if (key == SDL_SCANCODE_UP) selected = (selected + app->sound_count - 1) % app->sound_count;
        else if (key == SDL_SCANCODE_DOWN) selected = (selected + 1) % app->sound_count;
        else if (key == SDL_SCANCODE_RETURN || key == SDL_SCANCODE_KP_ENTER) context_play_sample(app->sounds[selected]);
        else if (key == SDL_SCANCODE_ESCAPE) running = false;
    }
}

static void render_static_level(App* app, ApplicationContext* ctx, int level_idx) {
    uint8_t* data = app->level_data[level_idx];
    SDL_SetRenderTarget(ctx->renderer, ctx->buffer); SDL_SetRenderDrawColor(ctx->renderer, 0, 0, 0, 255); SDL_RenderClear(ctx->renderer);
    for (int y = 0; y < 45; ++y) {
        uint8_t* row_ptr = &data[y * 66];
        for (int x = 0; x < 64; ++x) {
            uint8_t val = row_ptr[x];
            glyphs_render(&app->glyphs, ctx->renderer, x * 10, y * 10 + 30, (GlyphType)(GLYPH_MAP_START + val));
        }
    }
    SDL_Color white = {255, 255, 255, 255}; char title[64];
    snprintf(title, sizeof(title), "LEVEL TEST: %s (%d/%d)", app->level_names[level_idx], level_idx + 1, app->level_count);
    render_text(ctx->renderer, &app->font, 20, 460, white, title);
    render_text(ctx->renderer, &app->font, 400, 460, white, "L/R: BROWSE  ESC: BACK");
    SDL_SetRenderTarget(ctx->renderer, NULL); context_present(ctx);
}

void app_run_level_test(App* app, ApplicationContext* ctx) {
    if (app->level_count == 0) return;
    int selected = 0; bool running = true;
    while (running) {
        render_static_level(app, ctx, selected);
        SDL_Scancode key = context_wait_key_pressed(ctx);
        if (key == SDL_SCANCODE_LEFT) selected = (selected + app->level_count - 1) % app->level_count;
        else if (key == SDL_SCANCODE_RIGHT) selected = (selected + 1) % app->level_count;
        else if (key == SDL_SCANCODE_ESCAPE) running = false;
    }
}

void app_run_info_menu(App* app, ApplicationContext* ctx) {
    for (int i = 0; i < 4; ++i) {
        context_render_texture(ctx, app->info[i].texture); context_animate(ctx, ANIMATION_FADE_UP, 7);
        SDL_Scancode key = context_wait_key_pressed(ctx);
        if (key == SDL_SCANCODE_LEFT) { context_animate(ctx, ANIMATION_FADE_DOWN, 7); app_run_sound_test(app, ctx); return; }
        if (key == SDL_SCANCODE_RIGHT) { context_animate(ctx, ANIMATION_FADE_DOWN, 7); app_run_level_test(app, ctx); return; }
        context_animate(ctx, ANIMATION_FADE_DOWN, 7);
        if (key == SDL_SCANCODE_ESCAPE) return;
    }
}

void app_run_music_debugger(App* app, ApplicationContext* ctx) {
    int order = 0; bool running = true;
    while (running) {
        SDL_SetRenderTarget(ctx->renderer, ctx->buffer); SDL_SetRenderDrawColor(ctx->renderer, 0, 0, 0, 255); SDL_RenderClear(ctx->renderer);
        SDL_Color white = {255, 255, 255, 255};
        render_text(ctx->renderer, &app->font, 200, 50, white, "MUSIC DEBUGGER: OEKU.S3M");
        char order_str[64]; snprintf(order_str, sizeof(order_str), "ORDER INDEX: %d", order);
        render_text(ctx->renderer, &app->font, 240, 150, white, order_str);
        render_text(ctx->renderer, &app->font, 20, 450, white, "UP/DOWN: +-1  LEFT/RIGHT: +-10  ENTER: PLAY  ESC: BACK");
        SDL_SetRenderTarget(ctx->renderer, NULL); context_present(ctx);
        SDL_Scancode key = context_wait_key_pressed(ctx);
        if (key == SDL_SCANCODE_UP) order = (order + 91) % 92;
        else if (key == SDL_SCANCODE_DOWN) order = (order + 1) % 92;
        else if (key == SDL_SCANCODE_LEFT) order = (order + 82) % 92;
        else if (key == SDL_SCANCODE_RIGHT) order = (order + 10) % 92;
        else if (key == SDL_SCANCODE_RETURN || key == SDL_SCANCODE_KP_ENTER) context_play_music_at(ctx, "OEKU.S3M", order);
        else if (key == SDL_SCANCODE_ESCAPE) running = false;
    }
}

// ==================== OPTIONS MENU ====================

typedef enum {
    OPT_CASH, OPT_TREASURES, OPT_ROUNDS, OPT_TIME, OPT_PLAYERS,
    OPT_SPEED, OPT_BOMB_DAMAGE, OPT_DARKNESS, OPT_FREE_MARKET,
    OPT_SELLING, OPT_WINNER, OPT_REDEFINE_KEYS, OPT_LOAD_LEVELS,
    OPT_MAIN_MENU, OPT_COUNT
} OptionItem;

#define OPT_MENU_X 192
#define OPT_MENU_Y 96
#define OPT_ITEM_H 24

static void opt_cursor_pos(OptionItem item, int* x, int* y) {
    *x = OPT_MENU_X + 25;
    *y = OPT_MENU_Y + 6 + item * OPT_ITEM_H;
}

static void opt_value_minus(GameOptions* o, OptionItem item) {
    switch (item) {
        case OPT_CASH: o->cash = (o->cash >= 100) ? o->cash - 100 : 0; break;
        case OPT_TREASURES: if (o->treasures > 0) o->treasures--; break;
        case OPT_ROUNDS: if (o->rounds > 1) o->rounds--; break;
        case OPT_TIME: o->round_time_secs = (o->round_time_secs >= 15) ? o->round_time_secs - 15 : 0; break;
        case OPT_PLAYERS: if (o->players > 1) o->players--; break;
        case OPT_SPEED: if (o->speed > 50) o->speed--; break;
        case OPT_BOMB_DAMAGE: if (o->bomb_damage > 0) o->bomb_damage--; break;
        case OPT_DARKNESS: o->darkness = !o->darkness; break;
        case OPT_FREE_MARKET: o->free_market = !o->free_market; break;
        case OPT_SELLING: o->selling = !o->selling; break;
        case OPT_WINNER: o->win_by_money = !o->win_by_money; break;
        default: break;
    }
}

static void opt_value_plus(GameOptions* o, OptionItem item) {
    switch (item) {
        case OPT_CASH: o->cash += 100; if (o->cash > 2650) o->cash = 2650; break;
        case OPT_TREASURES: if (o->treasures < 75) o->treasures++; break;
        case OPT_ROUNDS: if (o->rounds < 55) o->rounds++; break;
        case OPT_TIME: o->round_time_secs += 15; if (o->round_time_secs > 22*60+40) o->round_time_secs = 22*60+40; break;
        case OPT_PLAYERS: if (o->players < 4) o->players++; break;
        case OPT_SPEED: if (o->speed < 200) o->speed++; break;
        case OPT_BOMB_DAMAGE: if (o->bomb_damage < 100) o->bomb_damage++; break;
        case OPT_DARKNESS: o->darkness = !o->darkness; break;
        case OPT_FREE_MARKET: o->free_market = !o->free_market; break;
        case OPT_SELLING: o->selling = !o->selling; break;
        case OPT_WINNER: o->win_by_money = !o->win_by_money; break;
        default: break;
    }
}

static void render_option_value(App* app, ApplicationContext* ctx, GameOptions* o, OptionItem item) {
    SDL_Color* pal = app->options_menu.palette;
    int bar_x = OPT_MENU_X + 142;
    int bar_y = OPT_MENU_Y + 5 + item * OPT_ITEM_H;

    if (item <= OPT_BOMB_DAMAGE) {
        SDL_Rect clear = {bar_x, bar_y, 166, 13};
        SDL_SetRenderDrawColor(ctx->renderer, 0, 0, 0, 255);
        SDL_RenderFillRect(ctx->renderer, &clear);

        int bar_w = 0;
        switch (item) {
            case OPT_CASH: bar_w = (int)((uint32_t)o->cash * 165 / 2650); break;
            case OPT_TREASURES: bar_w = (int)((uint32_t)o->treasures * 165 / 75); break;
            case OPT_ROUNDS: bar_w = (int)((uint32_t)o->rounds * 165 / 55); break;
            case OPT_TIME: bar_w = (int)((uint32_t)o->round_time_secs * 165 / 1359); break;
            case OPT_PLAYERS: bar_w = (o->players - 1) * 55; break;
            case OPT_SPEED: bar_w = (int)((uint32_t)(o->speed - 50) * 165 / 150); break;
            case OPT_BOMB_DAMAGE: bar_w = (int)((uint32_t)o->bomb_damage * 165 / 100); break;
            default: break;
        }
        SDL_Rect bar = {bar_x, bar_y, bar_w + 1, 13};
        SDL_SetRenderDrawColor(ctx->renderer, pal[1].r, pal[1].g, pal[1].b, 255);
        SDL_RenderFillRect(ctx->renderer, &bar);

        char text[32] = "";
        int tx = OPT_MENU_X + 208, ty = OPT_MENU_Y + 7 + item * OPT_ITEM_H;
        switch (item) {
            case OPT_CASH: snprintf(text, sizeof(text), "%u", o->cash); break;
            case OPT_TREASURES: snprintf(text, sizeof(text), "%u", o->treasures); break;
            case OPT_ROUNDS: snprintf(text, sizeof(text), "%u", o->rounds); break;
            case OPT_TIME: snprintf(text, sizeof(text), "%u:%02u min", o->round_time_secs / 60, o->round_time_secs % 60); break;
            case OPT_PLAYERS: snprintf(text, sizeof(text), " %u", o->players); break;
            case OPT_SPEED: snprintf(text, sizeof(text), " %u%%", o->speed); break;
            case OPT_BOMB_DAMAGE: snprintf(text, sizeof(text), " %u%%", o->bomb_damage); break;
            default: break;
        }
        if (text[0]) render_text(ctx->renderer, &app->font, tx, ty, pal[8], text);
    } else if (item >= OPT_DARKNESS && item <= OPT_WINNER) {
        bool enabled = false;
        switch (item) {
            case OPT_DARKNESS: enabled = o->darkness; break;
            case OPT_FREE_MARKET: enabled = o->free_market; break;
            case OPT_SELLING: enabled = o->selling; break;
            case OPT_WINNER: enabled = o->win_by_money; break;
            default: break;
        }
        int on_x = OPT_MENU_X + 185, on_y = bar_y;
        int off_x = OPT_MENU_X + 251, off_y = bar_y;
        glyphs_render(&app->glyphs, ctx->renderer, on_x, on_y, enabled ? GLYPH_RADIO_ON : GLYPH_RADIO_OFF);
        glyphs_render(&app->glyphs, ctx->renderer, off_x, off_y, enabled ? GLYPH_RADIO_OFF : GLYPH_RADIO_ON);
    }
}

static void render_options_full(App* app, ApplicationContext* ctx, GameOptions* o, OptionItem selected) {
    SDL_SetRenderTarget(ctx->renderer, ctx->buffer);
    SDL_RenderCopy(ctx->renderer, app->options_menu.texture, NULL, NULL);
    int cx, cy; opt_cursor_pos(selected, &cx, &cy);
    glyphs_render(&app->glyphs, ctx->renderer, cx, cy, GLYPH_ARROW_POINTER);
    for (int i = 0; i < OPT_COUNT; i++) render_option_value(app, ctx, o, (OptionItem)i);
    SDL_SetRenderTarget(ctx->renderer, NULL);
}

static void update_opt_pointer(App* app, ApplicationContext* ctx, OptionItem prev, OptionItem cur) {
    SDL_SetRenderTarget(ctx->renderer, ctx->buffer);
    int ox, oy; opt_cursor_pos(prev, &ox, &oy);
    int w, h; glyphs_dimensions(GLYPH_ARROW_POINTER, &w, &h);
    SDL_SetRenderDrawColor(ctx->renderer, 0, 0, 0, 255);
    SDL_Rect r = {ox, oy, w, h}; SDL_RenderFillRect(ctx->renderer, &r);
    int nx, ny; opt_cursor_pos(cur, &nx, &ny);
    glyphs_render(&app->glyphs, ctx->renderer, nx, ny, GLYPH_ARROW_POINTER);
    SDL_SetRenderTarget(ctx->renderer, NULL); context_present(ctx);
}

// ==================== LEVEL SELECTION ====================

static void render_level_grid(App* app, ApplicationContext* ctx, int cursor, int* picks, int pick_count) {
    SDL_SetRenderTarget(ctx->renderer, ctx->buffer);
    SDL_RenderCopy(ctx->renderer, app->level_select.texture, NULL, NULL);
    SDL_Color* pal = app->level_select.palette;

    char count_str[32];
    snprintf(count_str, sizeof(count_str), "%d", pick_count);
    render_text(ctx->renderer, &app->font, 15, 15, pal[7], count_str);

    int total = app->level_count + 1; // +1 for "Random" at position 0
    for (int i = 0; i < total; i++) {
        int col = i % 8, row = i / 8;
        int x = col * 80, y = row * 10 + 74;

        bool is_picked = false;
        for (int p = 0; p < pick_count; p++) { if (picks[p] == i) { is_picked = true; break; } }
        bool is_cursor = (i == cursor);

        SDL_Color color;
        if (i == 0) {
            color = pal[is_picked ? 5 : 4];
        } else if (is_cursor) {
            // Bright yellow for cursor highlight
            SDL_Color bright = {255, 255, 0, 255};
            color = is_picked ? bright : bright;
        } else {
            color = pal[is_picked ? 7 : 1];
        }

        const char* name = (i == 0) ? "Random" : app->level_names[i - 1];
        char short_name[12]; memset(short_name, 0, sizeof(short_name));
        strncpy(short_name, name, 8);
        // Remove extension
        char* dot = strrchr(short_name, '.');
        if (dot) *dot = '\0';

        render_text(ctx->renderer, &app->font, x + 4, y, color, short_name);
    }

    // Level preview
    if (cursor > 0 && cursor <= app->level_count) {
        uint8_t* map = app->level_data[cursor - 1];
        SDL_Surface* prev = SDL_CreateRGBSurfaceWithFormat(0, 64, 45, 32, SDL_PIXELFORMAT_RGBA32);
        uint32_t* pixels = (uint32_t*)prev->pixels;
        for (int py = 0; py < 45; py++) {
            for (int px = 0; px < 64; px++) {
                uint8_t val = map[py * 66 + px];
                int c = 12;
                if (val == 0x30 || val == 0x66 || val == 0xAF) c = 14;
                else if (val == 0x31) c = 8;
                else if ((val >= 0x37 && val <= 0x46) || val == 0x42 || val == 0x70 || val == 0x71) c = 9;
                else if (val == 0x73 || (val >= 0x8F && val <= 0x9A) || val == 0x6D || val == 0x79 || val == 0xB3) c = 5;
                SDL_Color pc = pal[c];
                pixels[py * 64 + px] = SDL_MapRGBA(prev->format, pc.r, pc.g, pc.b, 255);
            }
        }
        SDL_Texture* tex = SDL_CreateTextureFromSurface(ctx->renderer, prev);
        SDL_Rect tgt = {330, 7, 64, 45};
        SDL_RenderCopy(ctx->renderer, tex, NULL, &tgt);
        SDL_DestroyTexture(tex);
        SDL_FreeSurface(prev);
    }

    SDL_SetRenderTarget(ctx->renderer, NULL); context_present(ctx);
}

static void app_reload_levels(App* app, ApplicationContext* ctx) {
    for (int i = 0; i < app->level_count; ++i) { free(app->level_data[i]); app->level_data[i] = NULL; }
    app->level_count = 0;
    DIR* d = opendir(ctx->game_dir);
    if (d) {
        struct dirent* dir;
        while ((dir = readdir(d)) != NULL) {
            char* ext = strrchr(dir->d_name, '.');
            if (ext && (STRICMP(ext, ".MNL") == 0 || STRICMP(ext, ".MNE") == 0)
                && STRNICMP(dir->d_name, "LEVEL", 5) != 0) {
                if (app->level_count < 128) {
                    char path[MAX_PATH];
#ifdef _WIN32
                    snprintf(path, sizeof(path), "%s\\%s", ctx->game_dir, dir->d_name);
#else
                    snprintf(path, sizeof(path), "%s/%s", ctx->game_dir, dir->d_name);
#endif
                    FILE* lf = fopen(path, "rb");
                    if (lf) {
                        fseek(lf, 0, SEEK_END);
                        long size = ftell(lf);
                        fseek(lf, 0, SEEK_SET);
                        if (size >= 2970) {
                            app->level_data[app->level_count] = malloc(size);
                            if (app->level_data[app->level_count]) {
                                fread(app->level_data[app->level_count], 1, size, lf);
                                snprintf(app->level_names[app->level_count], 32, "%s", dir->d_name);
                                app->level_count++;
                            }
                        }
                        fclose(lf);
                    }
                }
            }
        }
        closedir(d);
    }
}

static void app_run_level_select(App* app, ApplicationContext* ctx) {
    app_reload_levels(app, ctx);
    int cursor = 0;
    int picks[128]; int pick_count = 0;
    int max_picks = app->options.rounds;
    int total = app->level_count + 1;

    render_level_grid(app, ctx, cursor, picks, pick_count);
    context_animate(ctx, ANIMATION_FADE_UP, 7);

    bool running = true;
    while (running) {
        SDL_Scancode key = context_wait_key_pressed(ctx);
        if (key == SDL_SCANCODE_LEFT || key == SDL_SCANCODE_KP_4) {
            cursor = (cursor + total - 1) % total;
        } else if (key == SDL_SCANCODE_RIGHT || key == SDL_SCANCODE_KP_6) {
            cursor = (cursor + 1) % total;
        } else if (key == SDL_SCANCODE_UP || key == SDL_SCANCODE_KP_8) {
            cursor = (cursor - 8 + total) % total;
        } else if (key == SDL_SCANCODE_DOWN || key == SDL_SCANCODE_KP_2) {
            cursor = (cursor + 8) % total;
        } else if (key == SDL_SCANCODE_RETURN || key == SDL_SCANCODE_KP_ENTER) {
            if (pick_count < max_picks && pick_count < 128) {
                picks[pick_count++] = cursor;
            }
        } else if (key == SDL_SCANCODE_F1) {
            // Fill remaining with random levels
            while (pick_count < max_picks && pick_count < 128) {
                picks[pick_count++] = rand() % total;
            }
        } else if (key == SDL_SCANCODE_ESCAPE) {
            running = false;
        }
        render_level_grid(app, ctx, cursor, picks, pick_count);
    }
    context_animate(ctx, ANIMATION_FADE_DOWN, 7);

    // Store selected levels
    app->selected_level_count = pick_count;
    for (int i = 0; i < pick_count; i++) app->selected_levels[i] = picks[i];
}

void app_run_options(App* app, ApplicationContext* ctx) {
    GameOptions* o = &app->options;
    bool running = true;
    while (running) {
        OptionItem selected = OPT_MAIN_MENU;
        render_options_full(app, ctx, o, selected);
        context_animate(ctx, ANIMATION_FADE_UP, 7);

        bool navigating = true;
        while (navigating) {
            SDL_Event e;
            while (SDL_PollEvent(&e)) {
                if (e.type == SDL_QUIT) { navigating = false; selected = OPT_MAIN_MENU; break; }
                ActionType act = input_map_event(&e, 0, &app->input_config);
                bool enter_pressed = (e.type == SDL_KEYDOWN && (e.key.keysym.scancode == SDL_SCANCODE_RETURN || e.key.keysym.scancode == SDL_SCANCODE_KP_ENTER))
                    || (e.type == SDL_CONTROLLERBUTTONDOWN && e.cbutton.button == SDL_CONTROLLER_BUTTON_START);
                bool esc_pressed = (e.type == SDL_KEYDOWN && e.key.keysym.scancode == SDL_SCANCODE_ESCAPE)
                    || (e.type == SDL_CONTROLLERBUTTONDOWN && e.cbutton.button == SDL_CONTROLLER_BUTTON_BACK);

                if (act == ACT_DOWN) {
                    OptionItem prev = selected;
                    selected = (OptionItem)((selected + 1) % OPT_COUNT);
                    update_opt_pointer(app, ctx, prev, selected);
                } else if (act == ACT_UP) {
                    OptionItem prev = selected;
                    selected = (OptionItem)((selected + OPT_COUNT - 1) % OPT_COUNT);
                    update_opt_pointer(app, ctx, prev, selected);
                } else if (act == ACT_LEFT) {
                    if (selected == OPT_REDEFINE_KEYS) {
                        navigating = false; selected = (OptionItem)-1;
                    } else {
                        opt_value_minus(o, selected);
                        SDL_SetRenderTarget(ctx->renderer, ctx->buffer);
                        render_option_value(app, ctx, o, selected);
                        SDL_SetRenderTarget(ctx->renderer, NULL); context_present(ctx);
                    }
                } else if (act == ACT_RIGHT) {
                    opt_value_plus(o, selected);
                    SDL_SetRenderTarget(ctx->renderer, ctx->buffer);
                    render_option_value(app, ctx, o, selected);
                    SDL_SetRenderTarget(ctx->renderer, NULL); context_present(ctx);
                } else if (act == ACT_ACTION || enter_pressed) {
                    if (selected == OPT_MAIN_MENU) navigating = false;
                    else if (selected == OPT_LOAD_LEVELS) navigating = false;
                    else if (selected == OPT_REDEFINE_KEYS) navigating = false;
                } else if (act == ACT_STOP || esc_pressed) {
                    navigating = false; selected = OPT_MAIN_MENU;
                } else if (e.type == SDL_KEYDOWN && e.key.keysym.scancode == SDL_SCANCODE_D) {
                    o->cash = 750; o->treasures = 45; o->rounds = 15;
                    o->round_time_secs = 420; o->players = 2; o->speed = 100;
                    o->bomb_damage = 100; o->darkness = false; o->free_market = false;
                    o->selling = false; o->win_by_money = true;
                    SDL_SetRenderTarget(ctx->renderer, ctx->buffer);
                    for (int i = 0; i < OPT_COUNT; i++) render_option_value(app, ctx, o, (OptionItem)i);
                    SDL_SetRenderTarget(ctx->renderer, NULL); context_present(ctx);
                }
            }
            SDL_Delay(1);
        }
        context_animate(ctx, ANIMATION_FADE_DOWN, 7);
        if (selected == OPT_LOAD_LEVELS) {
            app_run_level_select(app, ctx);
        } else if ((int)selected == -1) {
            app_run_music_debugger(app, ctx);
        } else {
            running = false;
        }
    }
    options_save(&app->options, ctx->game_dir);
}
