#include "app.h"
#include "game.h"
#include "persist.h"
#include "shop.h"
#include "editor.h"
#include "playersel.h"
#include "campaign.h"
#include "menus.h"
#include "netgame.h"
#include "compat.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <ctype.h>
#include <time.h>

// ==================== Gamepad Hotplug ====================

void app_handle_hotplug(App* app, ApplicationContext* ctx, const SDL_Event* e) {
    if (e->type == SDL_CONTROLLERDEVICEADDED && ctx->num_pads < 4) {
        if (SDL_IsGameController(e->cdevice.which)) {
            SDL_GameController* gc = SDL_GameControllerOpen(e->cdevice.which);
            if (gc) {
                ctx->pads[ctx->num_pads++] = gc;
                input_assign_pads(&app->input_config, ctx->pads, ctx->num_pads);
                // Build notification: show which player each pad is assigned to
                char msg[128] = "";
                int pos = 0;
                for (int p = 0; p < 4 && pos < 100; p++) {
                    if (app->input_config.pad_id[p] >= 0)
                        pos += snprintf(msg + pos, sizeof(msg) - pos, "P%d:PAD%d ", p + 1, p);
                }
                if (pos > 0) context_notify(ctx, msg, 3000);
            }
        }
    } else if (e->type == SDL_CONTROLLERDEVICEREMOVED) {
        for (int i = 0; i < ctx->num_pads; i++) {
            SDL_Joystick* joy = SDL_GameControllerGetJoystick(ctx->pads[i]);
            if (joy && SDL_JoystickInstanceID(joy) == e->cdevice.which) {
                SDL_GameControllerClose(ctx->pads[i]);
                for (int j = i; j < ctx->num_pads - 1; j++) ctx->pads[j] = ctx->pads[j + 1];
                ctx->pads[--ctx->num_pads] = NULL;
                break;
            }
        }
        input_assign_pads(&app->input_config, ctx->pads, ctx->num_pads);
        context_notify(ctx, "GAMEPAD DISCONNECTED", 3000);
    }
}

// ==================== Pause Menu ====================

bool is_pause_event(const SDL_Event* e, InputConfig* config) {
    if (e->type == SDL_CONTROLLERAXISMOTION) return false;
    for (int p = 0; p < 4; p++) {
        ActionType act = input_map_event(e, p, config);
        if (act == ACT_PAUSE) return true;
    }
    return false;
}

static PauseChoice pause_menu_impl(App* app, ApplicationContext* ctx, PauseContext pctx, NetContext* net) {
    const char* items[8];
    PauseChoice values[8];
    int count = 0;

    switch (pctx) {
        case PAUSE_CTX_GAMEPLAY:
            items[count] = "EXIT LEVEL";    values[count++] = PAUSE_EXIT_LEVEL;
            items[count] = "END GAME";      values[count++] = PAUSE_END_GAME;
            break;
        case PAUSE_CTX_SHOP:
            items[count] = "END GAME";      values[count++] = PAUSE_END_GAME;
            break;
        case PAUSE_CTX_MAINMENU:
            items[count] = "EDITOR";        values[count++] = PAUSE_EDITOR;
            break;
        case PAUSE_CTX_EDITOR:
            items[count] = "NEW LEVEL";     values[count++] = PAUSE_ED_NEW;
            items[count] = "LOAD LEVEL";    values[count++] = PAUSE_ED_LOAD;
            items[count] = "SAVE";          values[count++] = PAUSE_ED_SAVE;
            items[count] = "SAVE & QUIT";   values[count++] = PAUSE_ED_SAVE_QUIT;
            items[count] = "QUIT";          values[count++] = PAUSE_ED_QUIT;
            break;
    }

    int selected = 0;
    bool running = true;
    int max_len = 0;
    for (int i = 0; i < count; i++) {
        int len = (int)strlen(items[i]);
        if (len > max_len) max_len = len;
    }
    int char_w = 8, line_h = 18;
    int box_w = max_len * char_w + 40;
    int box_h = count * line_h + 24;
    int box_x = (SCREEN_WIDTH - box_w) / 2;
    int box_y = (SCREEN_HEIGHT - box_h) / 2;

    SDL_Texture* snapshot = SDL_CreateTexture(ctx->renderer, SDL_PIXELFORMAT_RGBA8888,
        SDL_TEXTUREACCESS_TARGET, SCREEN_WIDTH, SCREEN_HEIGHT);
    SDL_SetRenderTarget(ctx->renderer, snapshot);
    SDL_RenderCopy(ctx->renderer, ctx->buffer, NULL, NULL);

    SDL_Color white = {255, 255, 255, 255};
    SDL_Color yellow = {255, 255, 0, 255};
    SDL_Color gray = {160, 160, 160, 255};
    PauseChoice result = PAUSE_NONE;

    while (running) {
        SDL_SetRenderTarget(ctx->renderer, ctx->buffer);
        SDL_RenderCopy(ctx->renderer, snapshot, NULL, NULL);
        SDL_SetRenderDrawBlendMode(ctx->renderer, SDL_BLENDMODE_BLEND);
        SDL_SetRenderDrawColor(ctx->renderer, 0, 0, 0, 180);
        SDL_Rect bg = {box_x - 4, box_y - 4, box_w + 8, box_h + 8};
        SDL_RenderFillRect(ctx->renderer, &bg);
        SDL_SetRenderDrawColor(ctx->renderer, 200, 200, 200, 255);
        SDL_RenderDrawRect(ctx->renderer, &bg);

        const char* title = "PAUSED";
        int title_x = box_x + (box_w - (int)strlen(title) * char_w) / 2;
        render_text(ctx->renderer, &app->font, title_x, box_y + 2, white, title);

        for (int i = 0; i < count; i++) {
            int ix = box_x + 20;
            int iy = box_y + 22 + i * line_h;
            SDL_Color col = (i == selected) ? yellow : gray;
            if (i == selected) render_text(ctx->renderer, &app->font, ix - 12, iy, yellow, ">");
            render_text(ctx->renderer, &app->font, ix, iy, col, items[i]);
        }

        SDL_SetRenderTarget(ctx->renderer, NULL);
        context_present(ctx);

        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            app_handle_hotplug(app, ctx, &e);
            if (e.type == SDL_QUIT) { result = PAUSE_END_GAME; running = false; break; }
            if (e.type == SDL_KEYDOWN && e.key.repeat) continue;
            if (is_pause_event(&e, &app->input_config)) { running = false; break; }

            ActionType act = input_map_event(&e, 0, &app->input_config);
            if (act == ACT_UP) selected = (selected + count - 1) % count;
            else if (act == ACT_DOWN) selected = (selected + 1) % count;
            else if (act == ACT_ACTION || (e.type == SDL_KEYDOWN && (e.key.keysym.scancode == SDL_SCANCODE_RETURN || e.key.keysym.scancode == SDL_SCANCODE_KP_ENTER))) {
                result = values[selected]; running = false; break;
            }
            else if (act == ACT_STOP) { running = false; break; }
        }
#ifdef MB_NET
        if (net && net->host) {
            NetMessage nmsg; ENetPeer* npeer;
            while (net_poll(net, &nmsg, &npeer) > 0) {}
        }
#else
        (void)net;
#endif
        SDL_Delay(16);
    }

    SDL_SetRenderTarget(ctx->renderer, ctx->buffer);
    SDL_RenderCopy(ctx->renderer, snapshot, NULL, NULL);
    SDL_DestroyTexture(snapshot);
    return result;
}

PauseChoice pause_menu(App* app, ApplicationContext* ctx, PauseContext pctx) {
    return pause_menu_impl(app, ctx, pctx, NULL);
}

PauseChoice pause_menu_net(App* app, ApplicationContext* ctx, PauseContext pctx, NetContext* net) {
    return pause_menu_impl(app, ctx, pctx, net);
}

// ==================== App Init / Destroy ====================

bool app_init(App* app, ApplicationContext* ctx) {
    memset(app, 0, sizeof(App));
    srand((unsigned int)time(NULL));

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
    context_load_spy(ctx, "IDENTIFW.SPY", &app->select_players);
    context_load_spy(ctx, "EDITHELP.SPY", &app->edit_help);
    context_load_spy(ctx, "MINEDIT2.SPY", &app->edit_panel);

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
    snprintf(font_path, sizeof(font_path), "%s%cFONTTI.FON", ctx->game_dir, PATH_SEP);
    if (!load_font(ctx->renderer, font_path, &app->font)) return false;
    ctx->notify_font = &app->font;

    load_registered(ctx->game_dir, app->registered, sizeof(app->registered));

    if (!input_load_config(&app->input_config, "INPUT.CFG")) {
        printf("ERROR: INPUT.CFG is missing or invalid. Program will not start.\n");
        fflush(stdout);
        return false;
    }
    input_assign_pads(&app->input_config, ctx->pads, ctx->num_pads);
    input_print(&app->input_config);

    for (int p = 0; p < MAX_PLAYERS; ++p)
        snprintf(app->player_name[p], 16, "Plr %d", p + 1);

    app->options.cash = 750;
    app->options.treasures = 45;
    app->options.rounds = 15;
    app->options.round_time_secs = 420;
    app->options.players = 2;
    app->options.speed = 100;
    app->options.bomb_damage = 100;
    app->options.darkness = false;
    app->options.free_market = false;
    app->options.selling = false;
    app->options.win_by_money = true;

    options_load(&app->options, ctx->game_dir);
    roster_load(&app->roster, ctx->game_dir);
    identities_load(&app->roster, ctx->game_dir);
    highscores_load(app->highscores, ctx->game_dir);

    // Load sound effects
    DIR* d = opendir(ctx->game_dir);
    if (d) {
        struct dirent* dir;
        while ((dir = readdir(d)) != NULL) {
            char* ext = strrchr(dir->d_name, '.');
            if (ext && STRICMP(ext, ".VOC") == 0 && app->sound_count < 64) {
                app->sounds[app->sound_count] = context_load_sample(ctx, dir->d_name);
                if (app->sounds[app->sound_count]) {
                    snprintf(app->sound_names[app->sound_count], 32, "%s", dir->d_name);
                    if (STRICMP(dir->d_name, "KILI.VOC") == 0) app->sound_kili = app->sounds[app->sound_count];
                    else if (STRICMP(dir->d_name, "PICAXE.VOC") == 0) app->sound_picaxe = app->sounds[app->sound_count];
                    else if (STRICMP(dir->d_name, "PIKKUPOM.VOC") == 0) app->sound_pikkupom = app->sounds[app->sound_count];
                    else if (STRICMP(dir->d_name, "EXPLOS1.VOC") == 0) app->sound_explos1 = app->sounds[app->sound_count];
                    else if (STRICMP(dir->d_name, "EXPLOS2.VOC") == 0) app->sound_explos2 = app->sounds[app->sound_count];
                    else if (STRICMP(dir->d_name, "EXPLOS3.VOC") == 0) app->sound_explos3 = app->sounds[app->sound_count];
                    else if (STRICMP(dir->d_name, "AARGH.VOC") == 0) app->sound_aargh = app->sounds[app->sound_count];
                    else if (STRICMP(dir->d_name, "URETHAN.VOC") == 0) app->sound_urethan = app->sounds[app->sound_count];
                    else if (STRICMP(dir->d_name, "EXPLOS4.VOC") == 0) app->sound_explos4 = app->sounds[app->sound_count];
                    else if (STRICMP(dir->d_name, "EXPLOS5.VOC") == 0) app->sound_explos5 = app->sounds[app->sound_count];
                    else if (STRICMP(dir->d_name, "APPLAUSE.VOC") == 0) app->sound_applause = app->sounds[app->sound_count];
                    app->sound_count++;
                }
            }
        }
        closedir(d);
    }

    // Load multiplayer levels (*.MNL, *.MNE, excluding campaign LEVELn files)
    d = opendir(ctx->game_dir);
    if (d) {
        struct dirent* dir;
        while ((dir = readdir(d)) != NULL) {
            char* ext = strrchr(dir->d_name, '.');
            if (ext && (STRICMP(ext, ".MNL") == 0 || STRICMP(ext, ".MNE") == 0)
                && STRNICMP(dir->d_name, "LEVEL", 5) != 0 && app->level_count < 128) {
                char path[MAX_PATH];
                snprintf(path, sizeof(path), "%s%c%s", ctx->game_dir, PATH_SEP, dir->d_name);
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
        closedir(d);
    }

    // Load campaign levels (LEVEL0.MNL - LEVEL14.MNL)
    for (int i = 0; i < 15; i++) {
        char fname[32], path[MAX_PATH];
        snprintf(fname, sizeof(fname), "LEVEL%d.MNL", i);
        snprintf(path, sizeof(path), "%s%c%s", ctx->game_dir, PATH_SEP, fname);
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

// ==================== Main Menu ====================

void app_run_main_menu(App* app, ApplicationContext* ctx, bool campaign_mode) {
    (void)campaign_mode;
    context_play_music(ctx, "HUIPPE.S3M");
    context_render_texture(ctx, app->title.texture); context_animate(ctx, ANIMATION_FADE_UP, 7);
    SDL_Scancode key = context_wait_key_pressed(ctx); context_animate(ctx, ANIMATION_FADE_DOWN, 7);
    if (key == SDL_SCANCODE_ESCAPE) return;
    SelectedMenu selected = MENU_NEW_GAME;
    bool running = true;
    while (running) {
        if (strcmp(ctx->current_music, "HUIPPE.S3M") != 0)
            context_play_music(ctx, "HUIPPE.S3M");
        render_main_menu(app, ctx, selected); context_animate(ctx, ANIMATION_FADE_UP, 7);
        { SDL_Event flush; while (SDL_PollEvent(&flush)) { if (flush.type == SDL_QUIT) { selected = MENU_QUIT; running = false; } } }
        bool navigating = running, entering_debug = false;
        void (*debug_func)(App*, ApplicationContext*) = NULL;
        while (navigating) {
            SDL_Event e;
            while (SDL_PollEvent(&e)) {
                app_handle_hotplug(app, ctx, &e);
                if (e.type == SDL_QUIT) { selected = MENU_QUIT; navigating = false; running = false; break; }
                if (e.type == SDL_KEYDOWN && e.key.repeat) continue;
                ActionType act = input_map_event(&e, 0, &app->input_config);
                if (act == ACT_UP) { SelectedMenu p = menu_prev(selected); update_shovel(app, ctx, selected, p); selected = p; }
                else if (act == ACT_DOWN) { SelectedMenu n = menu_next(selected); update_shovel(app, ctx, selected, n); selected = n; }
                else if (act == ACT_ACTION || (e.type == SDL_KEYDOWN && (e.key.keysym.scancode == SDL_SCANCODE_RETURN || e.key.keysym.scancode == SDL_SCANCODE_KP_ENTER))) navigating = false;
                else if (act == ACT_STOP) { selected = MENU_QUIT; navigating = false; running = false; }
                else if (act == ACT_PAUSE) {
                    PauseChoice pc = pause_menu(app, ctx, PAUSE_CTX_MAINMENU);
                    if (pc == PAUSE_EDITOR) { entering_debug = true; debug_func = app_run_editor; navigating = false; }
                }
                else if (act == ACT_LEFT && selected == MENU_INFO) { entering_debug = true; debug_func = app_run_sound_test; navigating = false; }
                else if (act == ACT_RIGHT && selected == MENU_INFO) { entering_debug = true; debug_func = app_run_level_test; navigating = false; }
                else if (e.type == SDL_KEYDOWN && e.key.keysym.scancode == SDL_SCANCODE_M) { entering_debug = true; debug_func = app_run_music_debugger; navigating = false; }
#ifdef MB_NET
                else if (e.type == SDL_KEYDOWN && e.key.keysym.scancode == SDL_SCANCODE_F1) { entering_debug = true; debug_func = app_run_netgame; navigating = false; }
#endif
                else if (e.type == SDL_KEYDOWN && e.key.keysym.scancode == SDL_SCANCODE_F2) { entering_debug = true; debug_func = app_run_editor; navigating = false; }
                else if (e.type == SDL_CONTROLLERBUTTONDOWN && e.cbutton.button == SDL_CONTROLLER_BUTTON_BACK) { entering_debug = true; debug_func = app_run_editor; navigating = false; }
            }
            SDL_Delay(1);
        }
        if (entering_debug && debug_func) { context_animate(ctx, ANIMATION_FADE_DOWN, 7); debug_func(app, ctx); continue; }
        context_animate(ctx, ANIMATION_FADE_DOWN, 7);
        if (selected == MENU_QUIT) break;
        else if (selected == MENU_NEW_GAME) {
            if (!app_run_player_select(app, ctx)) continue;
            if (app->options.players == 1) {
                app_run_campaign(app, ctx);
            } else {
                app->current_round = 0;
                memset(app->game_stats, 0, sizeof(app->game_stats));
                for (int p = 0; p < MAX_PLAYERS; ++p) {
                    app->player_cash[p] = app->options.cash;
                    app->player_rounds_won[p] = 0;
                    memset(app->player_inventory[p], 0, sizeof(app->player_inventory[p]));
                }
                bool auto_levels = false;
                if (app->selected_level_count == 0 && app->level_count > 0) {
                    auto_levels = true;
                    int count = app->options.rounds;
                    if (count > 128) count = 128;
                    int pool[128];
                    for (int i = 0; i < app->level_count; i++) pool[i] = i;
                    int filled = 0;
                    while (filled < count) {
                        for (int i = app->level_count - 1; i > 0; i--) {
                            int j = rand() % (i + 1);
                            int tmp = pool[i]; pool[i] = pool[j]; pool[j] = tmp;
                        }
                        int take = count - filled;
                        if (take > app->level_count) take = app->level_count;
                        for (int i = 0; i < take; i++)
                            app->selected_levels[filled++] = pool[i] + 1;
                    }
                    app->selected_level_count = count;
                }
                app_run_shop(app, ctx);
                bool completed = app->current_round >= app->options.rounds;
                for (int p = 0; p < app->options.players; p++) {
                    int8_t ri = app->roster.identities[p];
                    if (ri >= 0 && app->roster.entries[ri].active) {
                        bool won = completed && compute_score(app, p, app->options.players) == PLAYER_WIN;
                        roster_update_stats(&app->roster.entries[ri], &app->game_stats[p], won);
                    }
                }
                roster_save(&app->roster, ctx->game_dir);
                if (auto_levels) app->selected_level_count = 0;
            }
        }
        else if (selected == MENU_OPTIONS) app_run_options(app, ctx);
        else if (selected == MENU_INFO) app_run_info_menu(app, ctx);
    }
    context_stop_music(ctx);
}
