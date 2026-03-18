#include "app.h"
#include "game.h"
#include <stdio.h>
#include <string.h>
#include <dirent.h>
#include <ctype.h>
#include <time.h>

#ifdef _WIN32
#define STRICMP _stricmp
#define STRNICMP _strnicmp
#else
#include <strings.h>
#define STRICMP strcasecmp
#define STRNICMP strncasecmp
#endif

typedef enum {
    MENU_NEW_GAME,
    MENU_OPTIONS,
    MENU_INFO,
    MENU_QUIT
} SelectedMenu;

static SelectedMenu menu_next(SelectedMenu current) {
    return (SelectedMenu)((current + 1) % 4);
}

static SelectedMenu menu_prev(SelectedMenu current) {
    return (SelectedMenu)((current + 3) % 4);
}

static void get_shovel_pos(SelectedMenu menu, int* x, int* y) {
    *x = 222;
    *y = 136 + 48 * menu;
}

static void load_registered(const char* game_dir, char* out_buf, size_t max_len) {
    out_buf[0] = '\0';
    char path[MAX_PATH];
#ifdef _WIN32
    snprintf(path, sizeof(path), "%s\\REGISTER.DAT", game_dir);
#else
    snprintf(path, sizeof(path), "%s/REGISTER.DAT", game_dir);
#endif

    FILE* f = fopen(path, "rb");
    if (!f) return;

    uint8_t len;
    if (fread(&len, 1, 1, f) == 1) {
        if (len < 26 && len < max_len) {
            fread(out_buf, 1, len, f);
            out_buf[len] = '\0';
        }
    }
    fclose(f);
}

bool app_init(App* app, ApplicationContext* ctx) {
    memset(app, 0, sizeof(App));
    srand((unsigned int)time(NULL));
    memset(app, 0, sizeof(App));

    if (!context_load_spy(ctx, "TITLEBE.SPY", &app->title)) return false;
    if (!context_load_spy(ctx, "MAIN3.SPY", &app->main_menu)) return false;
    if (!context_load_spy(ctx, "SIKA.SPY", &app->sika)) return false;
    if (!context_load_spy(ctx, "SHOPPIC.SPY", &app->shop)) return false;
    if (!context_load_spy(ctx, "PLAYERS.SPY", &app->players)) return false;

    if (!context_load_spy(ctx, "INFO1.SPY", &app->info[0])) return false;
    if (!context_load_spy(ctx, "INFO3.SPY", &app->info[1])) return false;
    if (!context_load_spy(ctx, "SHAPET.SPY", &app->info[2])) return false;
    if (!context_load_spy(ctx, "INFO2.SPY", &app->info[3])) return false;
    
    if (!context_load_spy(ctx, "CODES.SPY", &app->codes)) return false;
    if (!context_load_spy(ctx, "OPTIONS5.SPY", &app->options_menu)) return false;
    if (!context_load_spy(ctx, "LEVSELEC.SPY", &app->level_select)) return false;
    if (!context_load_spy(ctx, "FINAL.SPY", &app->final_screen)) return false;
    context_load_spy(ctx, "GAMEOVER.SPY", &app->game_over);
    context_load_spy(ctx, "CONGRATU.SPY", &app->congratu);
    context_load_spy(ctx, "HALLOFFA.SPY", &app->halloffa);

    // Player avatars (PPM format)
    static const char* avatar_win_files[] = {"SINVOIT.PPM", "PUNVOIT.PPM", "VIHVOIT.PPM", "KELVOIT.PPM"};
    static const char* avatar_lose_files[] = {"SINLOSE.PPM", "PUNLOSE.PPM", "VIHLOSE.PPM", "KELLOSE.PPM"};
    static const char* avatar_draw_files[] = {"SINDRAW.PPM", "PUNDRAW.PPM", "VIHDRAW.PPM", "KELDRAW.PPM"};
    for (int i = 0; i < MAX_PLAYERS; i++) {
        context_load_ppm(ctx, avatar_win_files[i], &app->avatar_win[i]);
        context_load_ppm(ctx, avatar_lose_files[i], &app->avatar_lose[i]);
        context_load_ppm(ctx, avatar_draw_files[i], &app->avatar_draw[i]);
    }

    glyphs_init(&app->glyphs, app->sika.texture);

    char font_path[MAX_PATH];
#ifdef _WIN32
    snprintf(font_path, sizeof(font_path), "%s\\FONTTI.FON", ctx->game_dir);
#else
    snprintf(font_path, sizeof(font_path), "%s/FONTTI.FON", ctx->game_dir);
#endif

    if (!load_font(ctx->renderer, font_path, &app->font)) return false;

    load_registered(ctx->game_dir, app->registered, sizeof(app->registered));

    if (!input_load_config(&app->input_config, "INPUT.CFG")) {
        printf("ERROR: INPUT.CFG is missing or invalid. Program will not start.\n");
        fflush(stdout);
        return false;
    }
    input_print(&app->input_config);

    for (int p = 0; p < MAX_PLAYERS; ++p) {
        snprintf(app->player_name[p], 16, "Plr %d", p + 1);
    }

    app->current_round = 0;
    app->selected_level_count = 0;
    app->campaign_level_count = 0;
    app->player_lives = 3;
    memset(app->campaign_levels, 0, sizeof(app->campaign_levels));

    app->options.cash = 750;
    app->options.treasures = 45;
    app->options.rounds = 15;
    app->options.round_time_secs = 420;
    app->options.players = 2;
    app->options.speed = 8;
    app->options.bomb_damage = 100;
    app->options.darkness = false;
    app->options.free_market = false;
    app->options.selling = false;
    app->options.win_by_money = true;

    DIR* d = opendir(ctx->game_dir);
    if (d) {
        struct dirent* dir;
        while ((dir = readdir(d)) != NULL) {
            char* ext = strrchr(dir->d_name, '.');
            if (ext && STRICMP(ext, ".VOC") == 0) {
                if (app->sound_count < 64) {
                    app->sounds[app->sound_count] = context_load_sample(ctx, dir->d_name);
                    if (app->sounds[app->sound_count]) {
                        strncpy(app->sound_names[app->sound_count], dir->d_name, 31);
                        app->sound_names[app->sound_count][31] = '\0';
                        if (STRICMP(dir->d_name, "KILI.VOC") == 0) app->sound_kili = app->sounds[app->sound_count];
                        if (STRICMP(dir->d_name, "PICAXE.VOC") == 0) app->sound_picaxe = app->sounds[app->sound_count];
                        if (STRICMP(dir->d_name, "PIKKUPOM.VOC") == 0) app->sound_pikkupom = app->sounds[app->sound_count];
                        if (STRICMP(dir->d_name, "EXPLOS1.VOC") == 0) app->sound_explos1 = app->sounds[app->sound_count];
                        if (STRICMP(dir->d_name, "EXPLOS2.VOC") == 0) app->sound_explos2 = app->sounds[app->sound_count];
                        if (STRICMP(dir->d_name, "EXPLOS3.VOC") == 0) app->sound_explos3 = app->sounds[app->sound_count];
                        if (STRICMP(dir->d_name, "AARGH.VOC") == 0) app->sound_aargh = app->sounds[app->sound_count];
                        if (STRICMP(dir->d_name, "URETHAN.VOC") == 0) app->sound_urethan = app->sounds[app->sound_count];
                        if (STRICMP(dir->d_name, "EXPLOS4.VOC") == 0) app->sound_explos4 = app->sounds[app->sound_count];
                        if (STRICMP(dir->d_name, "EXPLOS5.VOC") == 0) app->sound_explos5 = app->sounds[app->sound_count];
                        if (STRICMP(dir->d_name, "APPLAUSE.VOC") == 0) app->sound_applause = app->sounds[app->sound_count];
                        app->sound_count++;
                    }
                }
            }
        }
        closedir(d);
    }

    d = opendir(ctx->game_dir);
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
                                strncpy(app->level_names[app->level_count], dir->d_name, 31);
                                app->level_names[app->level_count][31] = '\0';
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
    // Load campaign levels (LEVEL0.MNL - LEVEL14.MNL)
    for (int i = 0; i < 15; i++) {
        char fname[32];
        snprintf(fname, sizeof(fname), "LEVEL%d.MNL", i);
        char path[MAX_PATH];
#ifdef _WIN32
        snprintf(path, sizeof(path), "%s\\%s", ctx->game_dir, fname);
#else
        snprintf(path, sizeof(path), "%s/%s", ctx->game_dir, fname);
#endif
        FILE* lf = fopen(path, "rb");
        if (lf) {
            fseek(lf, 0, SEEK_END);
            long size = ftell(lf);
            fseek(lf, 0, SEEK_SET);
            if (size >= 2970) {
                app->campaign_levels[i] = malloc(size);
                if (app->campaign_levels[i]) {
                    fread(app->campaign_levels[i], 1, size, lf);
                    app->campaign_level_count = i + 1;
                }
            }
            fclose(lf);
        }
    }

    return true;
}

void app_destroy(App* app) {
    input_save_config(&app->input_config, "INPUT.CFG");
    destroy_texture_palette(&app->title);
    destroy_texture_palette(&app->main_menu);
    destroy_texture_palette(&app->sika);
    destroy_texture_palette(&app->shop);
    destroy_texture_palette(&app->players);
    for (int i = 0; i < 4; ++i) destroy_texture_palette(&app->info[i]);
    destroy_texture_palette(&app->codes);
    destroy_texture_palette(&app->options_menu);
    destroy_texture_palette(&app->level_select);
    destroy_texture_palette(&app->final_screen);
    for (int i = 0; i < MAX_PLAYERS; i++) {
        destroy_texture_palette(&app->avatar_win[i]);
        destroy_texture_palette(&app->avatar_lose[i]);
        destroy_texture_palette(&app->avatar_draw[i]);
    }
    destroy_texture_palette(&app->game_over);
    destroy_texture_palette(&app->congratu);
    destroy_texture_palette(&app->halloffa);
    destroy_font(&app->font);
    for (int i = 0; i < app->sound_count; ++i) if (app->sounds[i]) Mix_FreeChunk(app->sounds[i]);
    for (int i = 0; i < app->level_count; ++i) if (app->level_data[i]) free(app->level_data[i]);
    for (int i = 0; i < 15; ++i) if (app->campaign_levels[i]) free(app->campaign_levels[i]);
}

static void render_main_menu(App* app, ApplicationContext* ctx, SelectedMenu selected) {
    SDL_SetRenderTarget(ctx->renderer, ctx->buffer);
    SDL_RenderCopy(ctx->renderer, app->main_menu.texture, NULL, NULL);
    int pos = (26 - strlen(app->registered)) * 4 + 254;
    SDL_Color* pal = app->main_menu.palette;
    render_text(ctx->renderer, &app->font, pos - 1, 437, pal[10], app->registered);
    render_text(ctx->renderer, &app->font, pos + 1, 437, pal[8], app->registered);
    render_text(ctx->renderer, &app->font, pos, 437, pal[0], app->registered);
    int sx, sy; get_shovel_pos(selected, &sx, &sy);
    glyphs_render(&app->glyphs, ctx->renderer, sx, sy, GLYPH_SHOVEL_POINTER);
    SDL_SetRenderTarget(ctx->renderer, NULL);
}

static void update_shovel(App* app, ApplicationContext* ctx, SelectedMenu previous, SelectedMenu selected) {
    SDL_SetRenderTarget(ctx->renderer, ctx->buffer);
    int old_x, old_y; get_shovel_pos(previous, &old_x, &old_y);
    int w, h; glyphs_dimensions(GLYPH_SHOVEL_POINTER, &w, &h);
    SDL_SetRenderDrawColor(ctx->renderer, 0, 0, 0, 255);
    SDL_Rect r = { old_x, old_y, w, h }; SDL_RenderFillRect(ctx->renderer, &r);
    int nx, ny; get_shovel_pos(selected, &nx, &ny);
    glyphs_render(&app->glyphs, ctx->renderer, nx, ny, GLYPH_SHOVEL_POINTER);
    SDL_SetRenderTarget(ctx->renderer, NULL); context_present(ctx);
}

static void app_run_sound_test(App* app, ApplicationContext* ctx) {
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

static void app_run_level_test(App* app, ApplicationContext* ctx) {
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

static void app_run_info_menu(App* app, ApplicationContext* ctx) {
    for (int i = 0; i < 4; ++i) {
        context_render_texture(ctx, app->info[i].texture); context_animate(ctx, ANIMATION_FADE_UP, 7);
        SDL_Scancode key = context_wait_key_pressed(ctx);
        if (key == SDL_SCANCODE_LEFT) { context_animate(ctx, ANIMATION_FADE_DOWN, 7); app_run_sound_test(app, ctx); return; }
        if (key == SDL_SCANCODE_RIGHT) { context_animate(ctx, ANIMATION_FADE_DOWN, 7); app_run_level_test(app, ctx); return; }
        context_animate(ctx, ANIMATION_FADE_DOWN, 7);
        if (key == SDL_SCANCODE_ESCAPE) return;
    }
}

static void app_run_music_debugger(App* app, ApplicationContext* ctx) {
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
        case OPT_SPEED: if (o->speed < 33) o->speed++; break;
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
        case OPT_SPEED: if (o->speed > 0) o->speed--; break;
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
            case OPT_SPEED: bar_w = (100 - 3 * o->speed) * 165 / 100; break;
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
            case OPT_SPEED: snprintf(text, sizeof(text), " %d%%", 100 - 3 * o->speed); break;
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

static void app_run_level_select(App* app, ApplicationContext* ctx);

static void app_run_options(App* app, ApplicationContext* ctx) {
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
                    o->round_time_secs = 420; o->players = 2; o->speed = 8;
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

        int color_idx;
        if (i == 0) {
            // Random: cursor state doesn't affect color
            color_idx = is_picked ? 5 : 4;
        } else {
            if (is_cursor) color_idx = is_picked ? 6 : 0;
            else color_idx = is_picked ? 7 : 1;
        }

        const char* name = (i == 0) ? "Random" : app->level_names[i - 1];
        char short_name[12]; memset(short_name, 0, sizeof(short_name));
        strncpy(short_name, name, 8);
        // Remove extension
        char* dot = strrchr(short_name, '.');
        if (dot) *dot = '\0';

        render_text(ctx->renderer, &app->font, x + 4, y, pal[color_idx], short_name);
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

static void app_run_level_select(App* app, ApplicationContext* ctx) {
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

static const uint32_t EQUIPMENT_PRICES[] = {
    1, 3, 10, 650, 15, 65, 300, 25, 500, 80, 90, 35, 145, 15, 80, 120, 50, 400, 1100, 1600, 70, 400, 50, 80, 800, 95, 575
};

static void render_shop_ui(App* app, ApplicationContext* ctx, int selected_item[], int shop_players[], int num_panels) {
    SDL_SetRenderTarget(ctx->renderer, ctx->buffer); SDL_RenderCopy(ctx->renderer, app->shop.texture, NULL, NULL);
    SDL_Color yellow = {255, 255, 0, 255}, white = {255, 255, 255, 255}, red = {255, 0, 0, 255};

    for (int panel = 0; panel < num_panels; ++panel) {
        int p_idx = shop_players[panel];
        int stats_x = panel == 0 ? 0 : 420;
        int items_x = panel * 320;

        char cash_str[32]; snprintf(cash_str, sizeof(cash_str), "%u", app->player_cash[p_idx]);
        SDL_Rect rect_cash = {35 + stats_x, 44, 56, 8}; SDL_SetRenderDrawColor(ctx->renderer, 0, 0, 0, 255); SDL_RenderFillRect(ctx->renderer, &rect_cash);
        render_text(ctx->renderer, &app->font, 35 + stats_x, 44, yellow, cash_str);

        SDL_Rect rect_name = {35 + stats_x, 16, 56, 8}; SDL_RenderFillRect(ctx->renderer, &rect_name);
        render_text(ctx->renderer, &app->font, 35 + stats_x, 16, white, app->player_name[p_idx]);

        int drill_power = 1 + app->player_inventory[p_idx][EQUIP_SMALL_PICKAXE]
            + 3 * app->player_inventory[p_idx][EQUIP_LARGE_PICKAXE]
            + 5 * app->player_inventory[p_idx][EQUIP_DRILL];
        char power_str[16]; snprintf(power_str, sizeof(power_str), "%d", drill_power);
        SDL_Rect rect_power = {35 + stats_x, 30, 56, 8}; SDL_RenderFillRect(ctx->renderer, &rect_power);
        render_text(ctx->renderer, &app->font, 35 + stats_x, 30, red, power_str);

        int sel = selected_item[panel];
        if (sel < EQUIP_TOTAL) {
            char count_str[16]; snprintf(count_str, sizeof(count_str), "%u", app->player_inventory[p_idx][sel]);
            SDL_Rect rect_count = {35 + stats_x, 58, 56, 8}; SDL_RenderFillRect(ctx->renderer, &rect_count);
            render_text(ctx->renderer, &app->font, 35 + stats_x, 58, white, count_str);
        }

        for (int i = 0; i < EQUIP_TOTAL; ++i) {
            int col = i % 4, row = i / 4, x = col * 64 + 32 + items_x, y = row * 48 + 96;
            glyphs_render(&app->glyphs, ctx->renderer, x, y, (i == sel) ? GLYPH_SHOP_SLOT_SELECTED : GLYPH_SHOP_SLOT_UNSELECTED);
            glyphs_render(&app->glyphs, ctx->renderer, x + 17, y + 3, (GlyphType)(GLYPH_EQUIPMENT_START + i));
            char price_str[16]; snprintf(price_str, sizeof(price_str), "%u$", EQUIPMENT_PRICES[i]);
            render_text(ctx->renderer, &app->font, x + 12, y + 36, yellow, price_str);
            if (app->player_inventory[p_idx][i] > 0) {
                int bar_x = x + 56, bar_y = y + 3, bar_h = (app->player_inventory[p_idx][i] * 2);
                if (bar_h > 40) bar_h = 40;
                int colors[] = {14, 13, 12, 11, 7}; SDL_Color* pal = app->shop.palette;
                for (int c = 0; c < 5; ++c) {
                    SDL_SetRenderDrawColor(ctx->renderer, pal[colors[c]].r, pal[colors[c]].g, pal[colors[c]].b, 255);
                    SDL_RenderDrawLine(ctx->renderer, bar_x + c, bar_y + 40, bar_x + c, bar_y + 40 - bar_h);
                }
            }
        }
        int rx = (EQUIP_TOTAL%4)*64+32 + items_x, ry = (EQUIP_TOTAL/4)*48+96;
        glyphs_render(&app->glyphs, ctx->renderer, rx, ry, (sel == EQUIP_TOTAL) ? GLYPH_SHOP_SLOT_SELECTED : GLYPH_SHOP_SLOT_UNSELECTED);
        glyphs_render(&app->glyphs, ctx->renderer, rx + 17, ry + 3, GLYPH_READY);
        render_text(ctx->renderer, &app->font, rx + 12, ry + 36, yellow, "READY");
    }
    
    {
        int total_rounds = (app->options.players == 1) ? 15 : app->options.rounds;
        int remaining = total_rounds - app->current_round;
        char rounds_str[32]; snprintf(rounds_str, sizeof(rounds_str), "%d", remaining);
        render_text(ctx->renderer, &app->font, 306, 120, white, rounds_str);
    }

    // Level preview (multiplayer only - SP levels are a mystery)
    uint8_t* preview_map = NULL;
    if (app->options.players > 1 && app->level_count > 0) {
        int lvl_idx;
        if (app->selected_level_count > 0 && app->current_round < app->selected_level_count) {
            int sel = app->selected_levels[app->current_round];
            lvl_idx = (sel == 0) ? (app->current_round % app->level_count) : (sel - 1);
        } else {
            lvl_idx = app->current_round % app->level_count;
        }
        preview_map = app->level_data[lvl_idx];
    }
    if (preview_map) {
        uint8_t* map = preview_map;
        SDL_Surface* prev = SDL_CreateRGBSurfaceWithFormat(0, 64, 45, 32, SDL_PIXELFORMAT_RGBA32);
        uint32_t* pixels = (uint32_t*)prev->pixels;
        for (int p_ry=0; p_ry<45; p_ry++) {
            for (int p_rx=0; p_rx<64; p_rx++) {
                uint8_t val = map[p_ry*66 + p_rx];
                int c = 12;
                if (val == 0x30 || val == 0x66 || val == 0xAF) c = 14; 
                else if (val == 0x31) c = 8; 
                else if ((val >= 0x37 && val <= 0x46) || val == 0x41 || val == 0x42 || val == 0x70 || val == 0x71) c = 9; 
                else if (val == 0x73 || (val >= 0x8F && val <= 0x9A) || val == 0x6D || val == 0x79 || val == 0xB3) c = 5; 
                
                SDL_Color pal_c = app->shop.palette[c];
                pixels[p_ry*64 + p_rx] = SDL_MapRGBA(prev->format, pal_c.r, pal_c.g, pal_c.b, 255);
            }
        }
        SDL_Texture* tex = SDL_CreateTextureFromSurface(ctx->renderer, prev);
        SDL_Rect tgt = {288, 51, 64, 45};
        SDL_RenderCopy(ctx->renderer, tex, NULL, &tgt);
        SDL_DestroyTexture(tex);
        SDL_FreeSurface(prev);
    }
    
    SDL_SetRenderTarget(ctx->renderer, NULL); context_present(ctx);
}

// ==================== VICTORY SCREEN ====================

typedef enum { PLAYER_WIN, PLAYER_LOSE, PLAYER_DRAW } PlayerResult;

static PlayerResult compute_score(App* app, int player_idx, int nplayers) {
    uint32_t score;
    if (app->options.win_by_money) score = app->player_cash[player_idx];
    else score = app->player_rounds_won[player_idx];

    // Find the highest score among all players
    uint32_t best = 0;
    for (int i = 0; i < nplayers; i++) {
        uint32_t s = app->options.win_by_money ? app->player_cash[i] : app->player_rounds_won[i];
        if (s > best) best = s;
    }

    if (score < best) return PLAYER_LOSE;

    // score == best: check if anyone else also has best
    for (int i = 0; i < nplayers; i++) {
        if (i == player_idx) continue;
        uint32_t other = app->options.win_by_money ? app->player_cash[i] : app->player_rounds_won[i];
        if (other == best) return PLAYER_DRAW;
    }
    return PLAYER_WIN;
}

static void app_run_victory_screen(App* app, ApplicationContext* ctx) {
    int nplayers = app->options.players;
    SDL_SetRenderTarget(ctx->renderer, ctx->buffer);
    SDL_RenderCopy(ctx->renderer, app->final_screen.texture, NULL, NULL);
    SDL_Color color = app->final_screen.palette[1];

    for (int i = 0; i < nplayers; i++) {
        PlayerResult result = compute_score(app, i, nplayers);
        SDL_Texture* avatar = NULL;
        switch (result) {
            case PLAYER_WIN: avatar = app->avatar_win[i].texture; break;
            case PLAYER_LOSE: avatar = app->avatar_lose[i].texture; break;
            case PLAYER_DRAW: avatar = app->avatar_draw[i].texture; break;
        }
        if (avatar) {
            SDL_Rect dest = {32 + 150 * i, 95, 132, 218};
            SDL_RenderCopy(ctx->renderer, avatar, NULL, &dest);
        }

        render_text(ctx->renderer, &app->font, 36 + 150 * i, 330, color, app->player_name[i]);

        char cash_str[32]; snprintf(cash_str, sizeof(cash_str), "%u", app->player_cash[i]);
        render_text(ctx->renderer, &app->font, 36 + 150 * i, 346, color, cash_str);

        char wins_str[32]; snprintf(wins_str, sizeof(wins_str), "%u", app->player_rounds_won[i]);
        render_text(ctx->renderer, &app->font, 36 + 150 * i, 362, color, wins_str);
    }

    SDL_SetRenderTarget(ctx->renderer, NULL);
    context_animate(ctx, ANIMATION_FADE_UP, 7);
    if (app->sound_applause) context_play_sample_freq(app->sound_applause, 11000);
    context_wait_key_pressed(ctx);
    context_animate(ctx, ANIMATION_FADE_DOWN, 7);
}

// Run a shop batch for a pair of players (panels). Returns false if quit requested.
static bool app_run_shop_batch(App* app, ApplicationContext* ctx, int players[], int num_panels) {
    int selected[2] = {0, 0}; bool ready[2] = {false, false};
    if (num_panels < 2) ready[1] = true;
    bool running = true;
    while (running) {
        render_shop_ui(app, ctx, selected, players, num_panels);
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_QUIT) return false;
            for (int panel = 0; panel < num_panels; ++panel) {
                int pi = players[panel];
                ActionType act = input_map_event(&e, pi, &app->input_config);
                if (ready[panel]) continue;
                if (act != ACT_MAX_PLAYER) {
                    switch (act) {
                        case ACT_UP:    if (selected[panel] >= 4) selected[panel] -= 4; break;
                        case ACT_DOWN:  if (selected[panel] + 4 <= EQUIP_TOTAL) selected[panel] += 4; else selected[panel] = EQUIP_TOTAL; break;
                        case ACT_LEFT:  selected[panel] = (selected[panel] + EQUIP_TOTAL) % (EQUIP_TOTAL + 1); break;
                        case ACT_RIGHT: selected[panel] = (selected[panel] + 1) % (EQUIP_TOTAL + 1); break;
                        case ACT_ACTION:
                            if (selected[panel] == EQUIP_TOTAL) { ready[panel] = true; }
                            else if (app->player_cash[pi] >= EQUIPMENT_PRICES[selected[panel]]) {
                                app->player_cash[pi] -= EQUIPMENT_PRICES[selected[panel]];
                                app->player_inventory[pi][selected[panel]]++;
                            }
                            break;
                        case ACT_STOP:
                            if (selected[panel] != EQUIP_TOTAL && app->player_inventory[pi][selected[panel]] > 0) {
                                app->player_inventory[pi][selected[panel]]--;
                                app->player_cash[pi] += EQUIPMENT_PRICES[selected[panel]] / 2;
                            }
                            break;
                        default: break;
                    }
                }
            }
        }
        if (ready[0] && ready[1]) running = false;
        SDL_Delay(16);
    }
    return true;
}

static void app_run_shop(App* app, ApplicationContext* ctx) {
    int nplayers = app->options.players;
    bool running = true;
    while (running) {
        context_play_music_at(ctx, "OEKU.S3M", 83);

        // Shop in batches of 2 players
        bool quit = false;
        for (int batch_start = 0; batch_start < nplayers && !quit; batch_start += 2) {
            int batch[2]; int num_panels = 0;
            for (int i = batch_start; i < nplayers && i < batch_start + 2; i++)
                batch[num_panels++] = i;
            // Swap so first player is on the right (matching original game)
            if (num_panels == 2) { int tmp = batch[0]; batch[0] = batch[1]; batch[1] = tmp; }
            if (!app_run_shop_batch(app, ctx, batch, num_panels)) { quit = true; break; }
        }
        if (quit) break;

        if (app->level_count > 0) {
            int lvl_idx;
            if (app->selected_level_count > 0 && app->current_round < app->selected_level_count) {
                int sel = app->selected_levels[app->current_round];
                if (sel == 0) lvl_idx = rand() % app->level_count;
                else lvl_idx = sel - 1;
            } else {
                lvl_idx = app->current_round % app->level_count;
            }
            context_animate(ctx, ANIMATION_FADE_DOWN, 7);
            RoundResult result = game_run(app, ctx, app->level_data[lvl_idx]);

            int alive = 0;
            for (int p = 0; p < nplayers; p++) if (result.player_survived[p]) alive++;
            if (alive > 0 && alive < nplayers) {
                for (int p = 0; p < nplayers; p++)
                    if (result.player_survived[p]) app->player_rounds_won[p]++;
            }
            for (int p = 0; p < nplayers; p++)
                if (app->player_cash[p] < 100) app->player_cash[p] += 150;

            context_linger_music_start(ctx);
            context_animate(ctx, ANIMATION_FADE_DOWN, 7);
            context_linger_music_end(ctx);
            app->current_round++;
            if (app->current_round >= app->options.rounds || result.end_type == ROUND_END_FINAL) running = false;
        }
    }

    context_stop_music(ctx);
    app_run_victory_screen(app, ctx);
}

static void app_run_campaign_end(App* app, ApplicationContext* ctx, bool win) {
    context_stop_music(ctx);
    SDL_Texture* tex = win ? app->congratu.texture : app->game_over.texture;
    if (tex) {
        SDL_SetRenderTarget(ctx->renderer, ctx->buffer);
        SDL_RenderCopy(ctx->renderer, tex, NULL, NULL);
        SDL_SetRenderTarget(ctx->renderer, NULL);
        context_animate(ctx, ANIMATION_FADE_UP, 7);
        if (win && app->sound_applause) context_play_sample_freq(app->sound_applause, 11000);
        context_wait_key_pressed(ctx);
        context_animate(ctx, ANIMATION_FADE_DOWN, 7);
    }

    // Hall of fame
    if (app->halloffa.texture) {
        SDL_SetRenderTarget(ctx->renderer, ctx->buffer);
        SDL_RenderCopy(ctx->renderer, app->halloffa.texture, NULL, NULL);
        SDL_Color color = app->halloffa.palette[1];
        char score_str[64];
        snprintf(score_str, sizeof(score_str), "1    %-20s Level %-2d Money %u",
                 app->player_name[0], app->current_round, app->player_cash[0]);
        render_text(ctx->renderer, &app->font, 127, 179, color, score_str);
        SDL_SetRenderTarget(ctx->renderer, NULL);
        context_animate(ctx, ANIMATION_FADE_UP, 7);
        context_wait_key_pressed(ctx);
        context_animate(ctx, ANIMATION_FADE_DOWN, 7);
    }
}

static void app_run_campaign(App* app, ApplicationContext* ctx) {
    if (app->campaign_level_count == 0) return;

    app->current_round = 0;
    app->player_lives = 3;
    app->player_cash[0] = 250;
    app->player_rounds_won[0] = 0;
    memset(app->player_inventory[0], 0, sizeof(app->player_inventory[0]));

    int max_rounds = app->campaign_level_count;

    while (app->current_round < max_rounds && app->player_lives > 0) {
        // Shop before each round
        context_play_music_at(ctx, "OEKU.S3M", 83);
        int selected[2] = {0, 0}; bool ready[2] = {false, true}; // P2 always ready (single player)
        int sp_players[1] = {0}; int sp_panels = 1;
        bool shopping = true;
        while (shopping) {
            render_shop_ui(app, ctx, selected, sp_players, sp_panels);
            SDL_Event e;
            while (SDL_PollEvent(&e)) {
                if (e.type == SDL_QUIT) { app->player_lives = 0; shopping = false; break; }
                ActionType act = input_map_event(&e, 0, &app->input_config);
                bool start_pressed = false;
                if (e.type == SDL_CONTROLLERBUTTONDOWN && e.cbutton.button == SDL_CONTROLLER_BUTTON_START) start_pressed = true;
                if (e.type == SDL_KEYDOWN && (e.key.keysym.scancode == SDL_SCANCODE_RETURN || e.key.keysym.scancode == SDL_SCANCODE_KP_ENTER)) start_pressed = true;

                if (ready[0]) { if (start_pressed || act == ACT_ACTION) shopping = false; continue; }

                if (start_pressed) { selected[0] = EQUIP_TOTAL; ready[0] = true; shopping = false; }
                if (act != ACT_MAX_PLAYER) {
                    switch (act) {
                        case ACT_UP:    if (selected[0] >= 4) selected[0] -= 4; break;
                        case ACT_DOWN:  if (selected[0] + 4 <= EQUIP_TOTAL) selected[0] += 4; else selected[0] = EQUIP_TOTAL; break;
                        case ACT_LEFT:  selected[0] = (selected[0] + EQUIP_TOTAL) % (EQUIP_TOTAL + 1); break;
                        case ACT_RIGHT: selected[0] = (selected[0] + 1) % (EQUIP_TOTAL + 1); break;
                        case ACT_ACTION:
                            if (selected[0] == EQUIP_TOTAL) { ready[0] = true; shopping = false; }
                            else if (app->player_cash[0] >= EQUIPMENT_PRICES[selected[0]]) {
                                app->player_cash[0] -= EQUIPMENT_PRICES[selected[0]];
                                app->player_inventory[0][selected[0]]++;
                            }
                            break;
                        case ACT_STOP:
                            if (selected[0] != EQUIP_TOTAL && app->player_inventory[0][selected[0]] > 0) {
                                app->player_inventory[0][selected[0]]--;
                                app->player_cash[0] += EQUIPMENT_PRICES[selected[0]] / 2;
                            }
                            break;
                        default: break;
                    }
                }
            }
            SDL_Delay(16);
        }

        if (app->player_lives == 0) break;

        // Play the level
        context_animate(ctx, ANIMATION_FADE_DOWN, 7);
        RoundResult result = game_run(app, ctx, app->campaign_levels[app->current_round]);
        app->player_lives += result.lives_gained;

        context_linger_music_start(ctx);
        context_animate(ctx, ANIMATION_FADE_DOWN, 7);
        context_linger_music_end(ctx);

        if (result.end_type == ROUND_END_EXITED) {
            app->current_round++;
        } else if (result.end_type == ROUND_END_FAILED) {
            app->player_lives--;
            // Don't advance round - retry same level
        } else if (result.end_type == ROUND_END_QUIT) {
            break;
        }

        // Minimum cash
        if (app->player_cash[0] < 100) app->player_cash[0] += 150;
    }

    bool win = (app->current_round >= max_rounds);
    app_run_campaign_end(app, ctx, win);
}

void app_run_main_menu(App* app, ApplicationContext* ctx, bool campaign_mode) {
    (void)campaign_mode;
    context_play_music(ctx, "HUIPPE.S3M");
    context_render_texture(ctx, app->title.texture); context_animate(ctx, ANIMATION_FADE_UP, 7);
    SDL_Scancode key = context_wait_key_pressed(ctx); context_animate(ctx, ANIMATION_FADE_DOWN, 7);
    if (key == SDL_SCANCODE_ESCAPE) return;
    SelectedMenu selected = MENU_NEW_GAME;
    bool running = true;
    while (running) {
        if (strcmp(ctx->current_music, "HUIPPE.S3M") != 0) {
            context_play_music(ctx, "HUIPPE.S3M");
        }
        render_main_menu(app, ctx, selected); context_animate(ctx, ANIMATION_FADE_UP, 7);
        { SDL_Event flush; while (SDL_PollEvent(&flush)) { if (flush.type == SDL_QUIT) { selected = MENU_QUIT; running = false; } } }
        bool navigating = running, entering_debug = false;
        void (*debug_func)(App*, ApplicationContext*) = NULL;
        while (navigating) {
            SDL_Event e;
            while (SDL_PollEvent(&e)) {
                if (e.type == SDL_QUIT) { selected = MENU_QUIT; navigating = false; running = false; break; }
                if (e.type == SDL_KEYDOWN && e.key.repeat) continue;
                ActionType act = input_map_event(&e, 0, &app->input_config);
                if (act == ACT_UP) { SelectedMenu p = menu_prev(selected); update_shovel(app, ctx, selected, p); selected = p; }
                else if (act == ACT_DOWN) { SelectedMenu n = menu_next(selected); update_shovel(app, ctx, selected, n); selected = n; }
                else if (act == ACT_ACTION || (e.type == SDL_KEYDOWN && (e.key.keysym.scancode == SDL_SCANCODE_RETURN || e.key.keysym.scancode == SDL_SCANCODE_KP_ENTER))) navigating = false;
                else if (act == ACT_STOP || (e.type == SDL_KEYDOWN && e.key.keysym.scancode == SDL_SCANCODE_ESCAPE)) { selected = MENU_QUIT; navigating = false; running = false; }
                else if (act == ACT_LEFT && selected == MENU_INFO) { entering_debug = true; debug_func = app_run_sound_test; navigating = false; }
                else if (act == ACT_RIGHT && selected == MENU_INFO) { entering_debug = true; debug_func = app_run_level_test; navigating = false; }
                else if (e.type == SDL_KEYDOWN && e.key.keysym.scancode == SDL_SCANCODE_M) { entering_debug = true; debug_func = app_run_music_debugger; navigating = false; }
            }
            SDL_Delay(1);
        }
        if (entering_debug && debug_func) { context_animate(ctx, ANIMATION_FADE_DOWN, 7); debug_func(app, ctx); continue; }
        context_animate(ctx, ANIMATION_FADE_DOWN, 7);
        if (selected == MENU_QUIT) break;
        else if (selected == MENU_NEW_GAME) {
            if (app->options.players == 1) {
                app_run_campaign(app, ctx);
            } else {
                app->current_round = 0;
                for (int p = 0; p < MAX_PLAYERS; ++p) {
                    app->player_cash[p] = app->options.cash;
                    app->player_rounds_won[p] = 0;
                    memset(app->player_inventory[p], 0, sizeof(app->player_inventory[p]));
                }
                app_run_shop(app, ctx);
            }
        }
        else if (selected == MENU_OPTIONS) app_run_options(app, ctx);
        else if (selected == MENU_INFO) app_run_info_menu(app, ctx);
    }
    context_stop_music(ctx);
}
