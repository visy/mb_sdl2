#include "app.h"
#include "game.h"
#include <stdio.h>
#include <string.h>
#include <dirent.h>
#include <ctype.h>
#include <time.h>

#ifdef _WIN32
#define STRICMP _stricmp
#else
#include <strings.h>
#define STRICMP strcasecmp
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

    if (!context_load_spy(ctx, "TITLEBE.SPY", &app->title)) return false;
    if (!context_load_spy(ctx, "MAIN3.SPY", &app->main_menu)) return false;
    if (!context_load_spy(ctx, "SIKA.SPY", &app->sika)) return false;
    if (!context_load_spy(ctx, "SHOPPIC.SPY", &app->shop)) return false;
    if (!context_load_spy(ctx, "SHAPET.SPY", &app->shapet)) return false;
    if (!context_load_spy(ctx, "PLAYERS.SPY", &app->players)) return false;

    if (!context_load_spy(ctx, "INFO1.SPY", &app->info[0])) return false;
    if (!context_load_spy(ctx, "INFO3.SPY", &app->info[1])) return false;
    if (!context_load_spy(ctx, "INFO2.SPY", &app->info[3])) return false;
    
    if (!context_load_spy(ctx, "CODES.SPY", &app->codes)) return false;

    glyphs_init(&app->glyphs, app->sika.texture);

    char font_path[MAX_PATH];
#ifdef _WIN32
    snprintf(font_path, sizeof(font_path), "%s\\FONTTI.FON", ctx->game_dir);
#else
    snprintf(font_path, sizeof(font_path), "%s/FONTTI.FON", ctx->game_dir);
#endif

    if (!load_font(ctx->renderer, font_path, &app->font)) return false;

    load_registered(ctx->game_dir, app->registered, sizeof(app->registered));

    app->player_cash = 100;
    app->player_inventory[EQUIP_SMALL_BOMB] = 5;

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
            if (ext && (STRICMP(ext, ".MNL") == 0 || STRICMP(ext, ".MNE") == 0)) {
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
    return true;
}

void app_destroy(App* app) {
    destroy_texture_palette(&app->title);
    destroy_texture_palette(&app->main_menu);
    destroy_texture_palette(&app->sika);
    destroy_texture_palette(&app->shop);
    destroy_texture_palette(&app->shapet);
    destroy_texture_palette(&app->players);
    for (int i = 0; i < 4; ++i) destroy_texture_palette(&app->info[i]);
    destroy_texture_palette(&app->codes);
    destroy_font(&app->font);
    for (int i = 0; i < app->sound_count; ++i) if (app->sounds[i]) Mix_FreeChunk(app->sounds[i]);
    for (int i = 0; i < app->level_count; ++i) if (app->level_data[i]) free(app->level_data[i]);
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

static const uint32_t EQUIPMENT_PRICES[] = {
    1, 3, 10, 650, 15, 65, 300, 25, 500, 80, 90, 35, 145, 15, 80, 120, 50, 400, 1100, 1600, 70, 400, 50, 80, 800, 95, 575
};

static void render_shop_ui(App* app, ApplicationContext* ctx, int selected_item) {
    SDL_SetRenderTarget(ctx->renderer, ctx->buffer); SDL_RenderCopy(ctx->renderer, app->shop.texture, NULL, NULL);
    SDL_Color green = {0, 255, 0, 255}, yellow = {255, 255, 0, 255};
    char cash_str[32]; snprintf(cash_str, sizeof(cash_str), "%u$", app->player_cash);
    render_text(ctx->renderer, &app->font, 35, 44, green, cash_str);
    for (int i = 0; i < EQUIP_TOTAL; ++i) {
        int col = i % 4, row = i / 4, x = col * 64 + 32, y = row * 48 + 96;
        glyphs_render(&app->glyphs, ctx->renderer, x, y, (i == selected_item) ? GLYPH_SHOP_SLOT_SELECTED : GLYPH_SHOP_SLOT_UNSELECTED);
        glyphs_render(&app->glyphs, ctx->renderer, x + 17, y + 3, (GlyphType)(GLYPH_EQUIPMENT_START + i));
        char price_str[16]; snprintf(price_str, sizeof(price_str), "%u$", EQUIPMENT_PRICES[i]);
        render_text(ctx->renderer, &app->font, x + 12, y + 36, green, price_str);
        if (app->player_inventory[i] > 0) {
            int bar_x = x + 56, bar_y = y + 3, bar_h = (app->player_inventory[i] * 2);
            if (bar_h > 40) bar_h = 40;
            int colors[] = {14, 13, 12, 11, 7}; SDL_Color* pal = app->shop.palette;
            for (int c = 0; c < 5; ++c) {
                SDL_SetRenderDrawColor(ctx->renderer, pal[colors[c]].r, pal[colors[c]].g, pal[colors[c]].b, 255);
                SDL_RenderDrawLine(ctx->renderer, bar_x + c, bar_y + 40, bar_x + c, bar_y + 40 - bar_h);
            }
        }
    }
    int rx = (EQUIP_TOTAL%4)*64+32, ry = (EQUIP_TOTAL/4)*48+96;
    glyphs_render(&app->glyphs, ctx->renderer, rx, ry, (selected_item == EQUIP_TOTAL) ? GLYPH_SHOP_SLOT_SELECTED : GLYPH_SHOP_SLOT_UNSELECTED);
    glyphs_render(&app->glyphs, ctx->renderer, rx + 17, ry + 3, GLYPH_READY);
    render_text(ctx->renderer, &app->font, rx + 12, ry + 36, yellow, "READY");
    SDL_SetRenderTarget(ctx->renderer, NULL); context_present(ctx);
}

static void app_run_shop(App* app, ApplicationContext* ctx) {
    context_play_music_at(ctx, "OEKU.S3M", 83);
    int selected = 0; bool running = true;
    while (running) {
        render_shop_ui(app, ctx, selected);
        SDL_Scancode key = context_wait_key_pressed(ctx);
        if (key == SDL_SCANCODE_LEFT) selected = (selected + EQUIP_TOTAL) % (EQUIP_TOTAL + 1);
        else if (key == SDL_SCANCODE_RIGHT) selected = (selected + 1) % (EQUIP_TOTAL + 1);
        else if (key == SDL_SCANCODE_UP && selected >= 4) selected -= 4;
        else if (key == SDL_SCANCODE_DOWN) { if (selected+4 <= EQUIP_TOTAL) selected += 4; else selected = EQUIP_TOTAL; }
        else if (key == SDL_SCANCODE_RETURN || key == SDL_SCANCODE_KP_ENTER || key == SDL_SCANCODE_SPACE) {
            if (selected == EQUIP_TOTAL) {
                if (app->level_count > 0) {
                    context_animate(ctx, ANIMATION_FADE_DOWN, 7);
                    game_run(app, ctx, app->level_data[rand() % app->level_count]);
                    running = false;
                }
            } else if (app->player_cash >= EQUIPMENT_PRICES[selected]) {
                app->player_cash -= EQUIPMENT_PRICES[selected];
                app->player_inventory[selected]++;
            }
        } else if (key == SDL_SCANCODE_ESCAPE) running = false;
    }
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
        bool navigating = true, entering_debug = false;
        void (*debug_func)(App*, ApplicationContext*) = NULL;
        while (navigating) {
            SDL_Scancode sc = context_wait_key_pressed(ctx);
            if (sc == SDL_SCANCODE_LEFT && selected == MENU_INFO) { entering_debug = true; debug_func = app_run_sound_test; navigating = false; }
            else if (sc == SDL_SCANCODE_RIGHT && selected == MENU_INFO) { entering_debug = true; debug_func = app_run_level_test; navigating = false; }
            else if (sc == SDL_SCANCODE_M) { entering_debug = true; debug_func = app_run_music_debugger; navigating = false; }
            else if (sc == SDL_SCANCODE_DOWN || sc == SDL_SCANCODE_KP_2) { SelectedMenu n = menu_next(selected); update_shovel(app, ctx, selected, n); selected = n; }
            else if (sc == SDL_SCANCODE_UP || sc == SDL_SCANCODE_KP_8) { SelectedMenu p = menu_prev(selected); update_shovel(app, ctx, selected, p); selected = p; }
            else if (sc == SDL_SCANCODE_ESCAPE) { selected = MENU_QUIT; navigating = false; running = false; }
            else if (sc == SDL_SCANCODE_RETURN || sc == SDL_SCANCODE_KP_ENTER) navigating = false;
        }
        if (entering_debug && debug_func) { context_animate(ctx, ANIMATION_FADE_DOWN, 7); debug_func(app, ctx); continue; }
        context_animate(ctx, ANIMATION_FADE_DOWN, 7);
        if (selected == MENU_QUIT) break;
        else if (selected == MENU_NEW_GAME) app_run_shop(app, ctx);
        else if (selected == MENU_INFO) app_run_info_menu(app, ctx);
    }
    context_stop_music(ctx);
}
