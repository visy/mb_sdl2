#include "app.h"
#include <stdio.h>
#include <string.h>
#include <dirent.h>
#include <ctype.h>

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

    if (!context_load_spy(ctx, "TITLEBE.SPY", &app->title)) return false;
    if (!context_load_spy(ctx, "MAIN3.SPY", &app->main_menu)) return false;
    if (!context_load_spy(ctx, "SIKA.SPY", &app->sika)) return false;

    if (!context_load_spy(ctx, "INFO1.SPY", &app->info[0])) return false;
    if (!context_load_spy(ctx, "INFO3.SPY", &app->info[1])) return false;
    if (!context_load_spy(ctx, "SHAPET.SPY", &app->info[2])) return false;
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

    // Load all .VOC files
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
                        app->sound_count++;
                    } else {
                        printf("Failed to load %s: %s\n", dir->d_name, Mix_GetError());
                    }
                }
            }
        }
        closedir(d);
    }
    printf("Successfully discovered %d VOC files in %s\n", app->sound_count, ctx->game_dir); fflush(stdout);

    return true;
}

void app_destroy(App* app) {
    destroy_texture_palette(&app->title);
    destroy_texture_palette(&app->main_menu);
    destroy_texture_palette(&app->sika);
    for (int i = 0; i < 4; ++i) {
        destroy_texture_palette(&app->info[i]);
    }
    destroy_texture_palette(&app->codes);
    destroy_font(&app->font);

    for (int i = 0; i < app->sound_count; ++i) {
        if (app->sounds[i]) {
            Mix_FreeChunk(app->sounds[i]);
        }
    }
}

static void render_main_menu(App* app, ApplicationContext* ctx, SelectedMenu selected) {
    SDL_SetRenderTarget(ctx->renderer, ctx->buffer);
    SDL_RenderCopy(ctx->renderer, app->main_menu.texture, NULL, NULL);

    int pos = (26 - strlen(app->registered)) * 4 + 254;
    SDL_Color* pal = app->main_menu.palette;

    render_text(ctx->renderer, &app->font, pos - 1, 437, pal[10], app->registered);
    render_text(ctx->renderer, &app->font, pos + 1, 437, pal[8], app->registered);
    render_text(ctx->renderer, &app->font, pos, 437, pal[0], app->registered);

    int sx, sy;
    get_shovel_pos(selected, &sx, &sy);
    glyphs_render(&app->glyphs, ctx->renderer, sx, sy, GLYPH_SHOVEL_POINTER);

    SDL_SetRenderTarget(ctx->renderer, NULL);
}

static void update_shovel(App* app, ApplicationContext* ctx, SelectedMenu previous, SelectedMenu selected) {
    SDL_SetRenderTarget(ctx->renderer, ctx->buffer);

    int old_x, old_y;
    get_shovel_pos(previous, &old_x, &old_y);

    int w, h;
    glyphs_dimensions(GLYPH_SHOVEL_POINTER, &w, &h);

    SDL_SetRenderDrawColor(ctx->renderer, 0, 0, 0, 255);
    SDL_Rect r = { old_x, old_y, w, h };
    SDL_RenderFillRect(ctx->renderer, &r);

    int nx, ny;
    get_shovel_pos(selected, &nx, &ny);
    glyphs_render(&app->glyphs, ctx->renderer, nx, ny, GLYPH_SHOVEL_POINTER);

    SDL_SetRenderTarget(ctx->renderer, NULL);
    context_present(ctx);
}

static void app_run_sound_test(App* app, ApplicationContext* ctx) {
    if (app->sound_count == 0) {
        printf("Error: No VOC files were loaded. Sound test unavailable.\n"); fflush(stdout);
        return;
    }

    int selected = 0;
    bool running = true;

    while (running) {
        SDL_SetRenderTarget(ctx->renderer, ctx->buffer);
        SDL_SetRenderDrawColor(ctx->renderer, 0, 0, 0, 255);
        SDL_RenderClear(ctx->renderer);

        SDL_Color white = {255, 255, 255, 255};
        SDL_Color yellow = {255, 255, 0, 255};

        render_text(ctx->renderer, &app->font, 240, 20, white, "SOUND TEST");
        render_text(ctx->renderer, &app->font, 20, 450, white, "UP/DOWN: BROWSE  ENTER: PLAY  ESC: BACK");

        int start_idx = selected - 10;
        if (start_idx < 0) start_idx = 0;
        
        for (int i = 0; i < 20 && (start_idx + i) < app->sound_count; ++i) {
            int idx = start_idx + i;
            render_text(ctx->renderer, &app->font, 50, 60 + i * 18, 
                        (idx == selected) ? yellow : white, 
                        app->sound_names[idx]);
            
            if (idx == selected) {
                render_text(ctx->renderer, &app->font, 30, 60 + i * 18, yellow, ">");
            }
        }

        SDL_SetRenderTarget(ctx->renderer, NULL);
        context_present(ctx);

        SDL_Scancode key = context_wait_key_pressed(ctx);
        switch (key) {
            case SDL_SCANCODE_UP:
                selected = (selected + app->sound_count - 1) % app->sound_count;
                break;
            case SDL_SCANCODE_DOWN:
                selected = (selected + 1) % app->sound_count;
                break;
            case SDL_SCANCODE_RETURN:
            case SDL_SCANCODE_KP_ENTER:
                context_play_sample(app->sounds[selected]);
                break;
            case SDL_SCANCODE_ESCAPE:
                running = false;
                break;
            default:
                break;
        }
    }
}

static void app_run_info_menu(App* app, ApplicationContext* ctx) {
    for (int i = 0; i < 4; ++i) {
        context_render_texture(ctx, app->info[i].texture);
        context_animate(ctx, ANIMATION_FADE_UP, 7);
        
        SDL_Scancode key = context_wait_key_pressed(ctx);
        
        if (key == SDL_SCANCODE_LEFT) {
            context_animate(ctx, ANIMATION_FADE_DOWN, 7);
            app_run_sound_test(app, ctx);
            return;
        }

        context_animate(ctx, ANIMATION_FADE_DOWN, 7);
        if (key == SDL_SCANCODE_ESCAPE) {
            return;
        }
    }
}

void app_run_main_menu(App* app, ApplicationContext* ctx, bool campaign_mode) {
    (void)campaign_mode;
    context_play_music(ctx, "HUIPPE.S3M");
    context_render_texture(ctx, app->title.texture);
    context_animate(ctx, ANIMATION_FADE_UP, 7);

    SDL_Scancode key = context_wait_key_pressed(ctx);
    
    context_animate(ctx, ANIMATION_FADE_DOWN, 7);
    if (key == SDL_SCANCODE_ESCAPE) return;

    SelectedMenu selected = MENU_NEW_GAME;
    bool running = true;

    while (running) {
        render_main_menu(app, ctx, selected);
        context_animate(ctx, ANIMATION_FADE_UP, 7);

        bool navigating = true;
        bool entering_sound_test = false;

        while (navigating) {
            SDL_Scancode sc = context_wait_key_pressed(ctx);
            switch (sc) {
                case SDL_SCANCODE_LEFT:
                    if (selected == MENU_INFO) {
                        entering_sound_test = true;
                        navigating = false;
                    }
                    break;
                case SDL_SCANCODE_DOWN:
                case SDL_SCANCODE_KP_2:
                {
                    SelectedMenu next = menu_next(selected);
                    update_shovel(app, ctx, selected, next);
                    selected = next;
                    break;
                }
                case SDL_SCANCODE_UP:
                case SDL_SCANCODE_KP_8:
                {
                    SelectedMenu prev = menu_prev(selected);
                    update_shovel(app, ctx, selected, prev);
                    selected = prev;
                    break;
                }
                case SDL_SCANCODE_ESCAPE:
                    selected = MENU_QUIT;
                    navigating = false;
                    running = false;
                    break;
                case SDL_SCANCODE_RETURN:
                case SDL_SCANCODE_RETURN2:
                case SDL_SCANCODE_KP_ENTER:
                    navigating = false;
                    break;
                default:
                    break;
            }
        }

        if (entering_sound_test) {
            context_animate(ctx, ANIMATION_FADE_DOWN, 7);
            app_run_sound_test(app, ctx);
            continue; // Go back to main menu loop
        }

        if (selected == MENU_QUIT) {
            context_animate(ctx, ANIMATION_FADE_DOWN, 7);
            break;
        } else if (selected == MENU_NEW_GAME) {
            printf("New game selected!\n");
            context_animate(ctx, ANIMATION_FADE_DOWN, 7);
        } else if (selected == MENU_OPTIONS) {
            printf("Options selected!\n");
            context_animate(ctx, ANIMATION_FADE_DOWN, 7);
        } else if (selected == MENU_INFO) {
            app_run_info_menu(app, ctx);
        }
    }
    context_stop_music(ctx);
}
