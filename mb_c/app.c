#include "app.h"
#include "game.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <ctype.h>
#include <time.h>

#ifdef _WIN32
#define STRICMP _stricmp
#define STRNICMP _strnicmp
#define PATH_SEP '\\'
#else
#include <strings.h>
#define STRICMP strcasecmp
#define STRNICMP strncasecmp
#define PATH_SEP '/'
#endif

static void options_load(GameOptions* o, const char* game_dir) {
    char path[MAX_PATH];
    snprintf(path, sizeof(path), "%s%cOPTIONS.CFG", game_dir, PATH_SEP);
    FILE* f = fopen(path, "rb");
    if (!f) return;
    uint8_t buf[17];
    if (fread(buf, 1, 17, f) != 17) { fclose(f); return; }
    fclose(f);
    o->players = buf[0];
    o->treasures = buf[1];
    o->rounds = buf[2] | (buf[3] << 8);
    o->cash = buf[4] | (buf[5] << 8);
    uint32_t ticks = buf[6] | (buf[7] << 8) | (buf[8] << 16) | (buf[9] << 24);
    o->round_time_secs = (uint16_t)(ticks * 10 / 182);
    o->speed = buf[10] | (buf[11] << 8);
    o->darkness = buf[12] != 0;
    o->free_market = buf[13] != 0;
    o->selling = buf[14] != 0;
    o->win_by_money = buf[15] == 0;
    o->bomb_damage = buf[16];
    if (o->players > 4) o->players = 2;
    if (o->bomb_damage > 100) o->bomb_damage = 100;
    if (o->rounds > 55) o->rounds = 55;
    if (o->treasures > 75) o->treasures = 75;
    if (o->cash > 2650) o->cash = 2650;
    if (o->speed < 50) o->speed = 50;
    if (o->speed > 200) o->speed = 200;
}

static void options_save(const GameOptions* o, const char* game_dir) {
    char path[MAX_PATH];
    snprintf(path, sizeof(path), "%s%cOPTIONS.CFG", game_dir, PATH_SEP);
    uint8_t buf[17];
    buf[0] = o->players;
    buf[1] = o->treasures;
    buf[2] = o->rounds & 0xFF;
    buf[3] = (o->rounds >> 8) & 0xFF;
    buf[4] = o->cash & 0xFF;
    buf[5] = (o->cash >> 8) & 0xFF;
    uint32_t ticks = (uint32_t)o->round_time_secs * 182 / 10;
    buf[6] = ticks & 0xFF;
    buf[7] = (ticks >> 8) & 0xFF;
    buf[8] = (ticks >> 16) & 0xFF;
    buf[9] = (ticks >> 24) & 0xFF;
    buf[10] = o->speed & 0xFF;
    buf[11] = (o->speed >> 8) & 0xFF;
    buf[12] = o->darkness ? 1 : 0;
    buf[13] = o->free_market ? 1 : 0;
    buf[14] = o->selling ? 1 : 0;
    buf[15] = o->win_by_money ? 0 : 1;
    buf[16] = o->bomb_damage;
    FILE* f = fopen(path, "wb");
    if (!f) return;
    fwrite(buf, 1, 17, f);
    fclose(f);
}

// ==================== Player Roster (PLAYERS.DAT) ====================

static void roster_load(PlayerRoster* roster, const char* game_dir) {
    memset(roster, 0, sizeof(PlayerRoster));
    for (int i = 0; i < MAX_PLAYERS; i++) roster->identities[i] = -1;

    char path[MAX_PATH];
    snprintf(path, sizeof(path), "%s%cPLAYERS.DAT", game_dir, PATH_SEP);
    FILE* f = fopen(path, "rb");
    if (!f) return;
    uint8_t data[ROSTER_MAX * ROSTER_RECORD_SIZE];
    size_t n = fread(data, 1, sizeof(data), f);
    fclose(f);
    if (n != sizeof(data)) return;

    for (int i = 0; i < ROSTER_MAX; i++) {
        const uint8_t* rec = &data[i * ROSTER_RECORD_SIZE];
        if (rec[0] != 0) continue; // non-zero = empty
        roster->entries[i].active = true;
        int len = rec[1]; if (len > 24) len = 24;
        memcpy(roster->entries[i].name, &rec[2], len);
        roster->entries[i].name[len] = '\0';
        const uint8_t* s = &rec[26];
        roster->entries[i].tournaments        = s[0]  | (s[1]<<8)  | (s[2]<<16)  | (s[3]<<24);
        roster->entries[i].tournaments_wins   = s[4]  | (s[5]<<8)  | (s[6]<<16)  | (s[7]<<24);
        roster->entries[i].rounds             = s[8]  | (s[9]<<8)  | (s[10]<<16) | (s[11]<<24);
        roster->entries[i].rounds_wins        = s[12] | (s[13]<<8) | (s[14]<<16) | (s[15]<<24);
        roster->entries[i].treasures_collected= s[16] | (s[17]<<8) | (s[18]<<16) | (s[19]<<24);
        roster->entries[i].total_money        = s[20] | (s[21]<<8) | (s[22]<<16) | (s[23]<<24);
        roster->entries[i].bombs_bought       = s[24] | (s[25]<<8) | (s[26]<<16) | (s[27]<<24);
        roster->entries[i].bombs_dropped      = s[28] | (s[29]<<8) | (s[30]<<16) | (s[31]<<24);
        roster->entries[i].deaths             = s[32] | (s[33]<<8) | (s[34]<<16) | (s[35]<<24);
        roster->entries[i].meters_ran         = s[36] | (s[37]<<8) | (s[38]<<16) | (s[39]<<24);
        memcpy(roster->entries[i].history, &rec[66], ROSTER_HISTORY_SIZE);
    }
}

static void roster_save(const PlayerRoster* roster, const char* game_dir) {
    uint8_t data[ROSTER_MAX * ROSTER_RECORD_SIZE];
    memset(data, 0, sizeof(data));
    for (int i = 0; i < ROSTER_MAX; i++) {
        uint8_t* rec = &data[i * ROSTER_RECORD_SIZE];
        if (!roster->entries[i].active) { rec[0] = 1; continue; }
        rec[0] = 0;
        int len = (int)strlen(roster->entries[i].name); if (len > 24) len = 24;
        rec[1] = (uint8_t)len;
        memcpy(&rec[2], roster->entries[i].name, len);
        uint8_t* s = &rec[26];
        const RosterInfo* e = &roster->entries[i];
        uint32_t vals[] = { e->tournaments, e->tournaments_wins, e->rounds, e->rounds_wins,
                            e->treasures_collected, e->total_money, e->bombs_bought,
                            e->bombs_dropped, e->deaths, e->meters_ran };
        for (int v = 0; v < 10; v++) {
            s[v*4+0] = vals[v] & 0xFF;
            s[v*4+1] = (vals[v] >> 8) & 0xFF;
            s[v*4+2] = (vals[v] >> 16) & 0xFF;
            s[v*4+3] = (vals[v] >> 24) & 0xFF;
        }
        memcpy(&rec[66], e->history, ROSTER_HISTORY_SIZE);
    }
    char path[MAX_PATH];
    snprintf(path, sizeof(path), "%s%cPLAYERS.DAT", game_dir, PATH_SEP);
    FILE* f = fopen(path, "wb");
    if (!f) return;
    fwrite(data, 1, sizeof(data), f);
    fclose(f);
}

static void identities_load(PlayerRoster* roster, const char* game_dir) {
    char path[MAX_PATH];
    snprintf(path, sizeof(path), "%s%cIDENTIFY.DAT", game_dir, PATH_SEP);
    FILE* f = fopen(path, "rb");
    if (!f) return;
    uint8_t buf[4];
    if (fread(buf, 1, 4, f) == 4) {
        for (int i = 0; i < MAX_PLAYERS; i++)
            roster->identities[i] = buf[i] == 0 ? -1 : (int8_t)(buf[i] - 1);
    }
    fclose(f);
}

static void identities_save(const PlayerRoster* roster, const char* game_dir) {
    uint8_t buf[4];
    for (int i = 0; i < MAX_PLAYERS; i++)
        buf[i] = roster->identities[i] < 0 ? 0 : (uint8_t)(roster->identities[i] + 1);
    char path[MAX_PATH];
    snprintf(path, sizeof(path), "%s%cIDENTIFY.DAT", game_dir, PATH_SEP);
    FILE* f = fopen(path, "wb");
    if (!f) return;
    fwrite(buf, 1, 4, f);
    fclose(f);
}

static void roster_update_stats(RosterInfo* dest, const GameStats* src, bool tournament_win) {
    if (src->rounds == 0) return;
    uint32_t hlen = ROSTER_HISTORY_SIZE;
    uint32_t hist_idx = dest->tournaments % hlen;
    uint32_t last_idx = (dest->tournaments + hlen - 1) % hlen;
    uint8_t hval = dest->history[last_idx] / 2 + (uint8_t)((129 * src->rounds_wins / src->rounds) / 2);
    dest->tournaments += 1;
    dest->tournaments_wins += tournament_win ? 1 : 0;
    dest->rounds += src->rounds;
    dest->rounds_wins += src->rounds_wins;
    dest->treasures_collected += src->treasures_collected;
    dest->total_money += src->total_money;
    dest->bombs_bought += src->bombs_bought;
    dest->bombs_dropped += src->bombs_dropped;
    dest->deaths += src->deaths;
    dest->meters_ran += src->meters_ran;
    dest->history[hist_idx] = hval;
}

// --- Pause Menu ---
// Check if event is a pause trigger (ACT_PAUSE from any player)
bool is_pause_event(const SDL_Event* e, InputConfig* config) {
    // Skip axis events — pause is only bound to keys/buttons, and calling
    // input_map_event on axis events would consume the state change needed
    // for navigation (up/down) in menus and the editor.
    if (e->type == SDL_CONTROLLERAXISMOTION) return false;
    for (int p = 0; p < 4; p++) {
        ActionType act = input_map_event(e, p, config);
        if (act == ACT_PAUSE) return true;
    }
    return false;
}

// Internal pause menu with optional network keepalive.
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

    // Compute menu box dimensions
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

    // Snapshot the current buffer so we can redraw it each frame
    SDL_Texture* snapshot = SDL_CreateTexture(ctx->renderer, SDL_PIXELFORMAT_RGBA8888,
        SDL_TEXTUREACCESS_TARGET, SCREEN_WIDTH, SCREEN_HEIGHT);
    SDL_SetRenderTarget(ctx->renderer, snapshot);
    SDL_RenderCopy(ctx->renderer, ctx->buffer, NULL, NULL);

    SDL_Color white = {255, 255, 255, 255};
    SDL_Color yellow = {255, 255, 0, 255};
    SDL_Color gray = {160, 160, 160, 255};
    PauseChoice result = PAUSE_NONE;

    while (running) {
        // Restore background from snapshot
        SDL_SetRenderTarget(ctx->renderer, ctx->buffer);
        SDL_RenderCopy(ctx->renderer, snapshot, NULL, NULL);

        // Draw semi-transparent black background for the menu
        SDL_SetRenderDrawBlendMode(ctx->renderer, SDL_BLENDMODE_BLEND);
        SDL_SetRenderDrawColor(ctx->renderer, 0, 0, 0, 180);
        SDL_Rect bg = {box_x - 4, box_y - 4, box_w + 8, box_h + 8};
        SDL_RenderFillRect(ctx->renderer, &bg);

        // Draw border
        SDL_SetRenderDrawColor(ctx->renderer, 200, 200, 200, 255);
        SDL_RenderDrawRect(ctx->renderer, &bg);

        // Title
        const char* title = "PAUSED";
        int title_x = box_x + (box_w - (int)strlen(title) * char_w) / 2;
        render_text(ctx->renderer, &app->font, title_x, box_y + 2, white, title);

        // Items
        for (int i = 0; i < count; i++) {
            int ix = box_x + 20;
            int iy = box_y + 22 + i * line_h;
            SDL_Color col = (i == selected) ? yellow : gray;
            if (i == selected) {
                render_text(ctx->renderer, &app->font, ix - 12, iy, yellow, ">");
            }
            render_text(ctx->renderer, &app->font, ix, iy, col, items[i]);
        }

        SDL_SetRenderTarget(ctx->renderer, NULL);
        context_present(ctx);

        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_QUIT) { result = PAUSE_END_GAME; running = false; break; }
            if (e.type == SDL_KEYDOWN && e.key.repeat) continue;

            // Check for pause button again = resume
            if (is_pause_event(&e, &app->input_config)) {
                running = false; break;
            }

            ActionType act = input_map_event(&e, 0, &app->input_config);
            if (act == ACT_UP) { selected = (selected + count - 1) % count; }
            else if (act == ACT_DOWN) { selected = (selected + 1) % count; }
            else if (act == ACT_ACTION || (e.type == SDL_KEYDOWN && (e.key.keysym.scancode == SDL_SCANCODE_RETURN || e.key.keysym.scancode == SDL_SCANCODE_KP_ENTER))) {
                result = values[selected]; running = false; break;
            }
            else if (act == ACT_STOP) {
                running = false; break; // B button = resume
            }
        }
        // Service network to prevent timeout during pause
#ifdef MB_NET
        if (net && net->host) {
            NetMessage nmsg; ENetPeer* npeer;
            while (net_poll(net, &nmsg, &npeer) > 0) { /* drain keepalives */ }
        }
#else
        (void)net;
#endif
        SDL_Delay(16);
    }

    // Restore buffer from snapshot
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
    context_load_spy(ctx, "IDENTIFW.SPY", &app->select_players);
    context_load_spy(ctx, "EDITHELP.SPY", &app->edit_help);
    context_load_spy(ctx, "MINEDIT2.SPY", &app->edit_panel);

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
    input_assign_pads(&app->input_config, ctx->pads, ctx->num_pads);
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
    app->options.speed = 100;
    app->options.bomb_damage = 100;
    app->options.darkness = false;
    app->options.free_market = false;
    app->options.selling = false;
    app->options.win_by_money = true;

    options_load(&app->options, ctx->game_dir);
    roster_load(&app->roster, ctx->game_dir);
    identities_load(&app->roster, ctx->game_dir);

    DIR* d = opendir(ctx->game_dir);
    if (d) {
        struct dirent* dir;
        while ((dir = readdir(d)) != NULL) {
            char* ext = strrchr(dir->d_name, '.');
            if (ext && STRICMP(ext, ".VOC") == 0) {
                if (app->sound_count < 64) {
                    app->sounds[app->sound_count] = context_load_sample(ctx, dir->d_name);
                    if (app->sounds[app->sound_count]) {
                        snprintf(app->sound_names[app->sound_count], 32, "%s", dir->d_name);
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

static const uint32_t EQUIPMENT_PRICES[] = {
    1, 3, 10, 650, 15, 65, 300, 25, 500, 80, 90, 35, 145, 15, 80, 120, 50, 400, 1100, 1600, 70, 400, 50, 80, 800, 95, 575
};

// Single source of truth for end-of-round money distribution.
// Handles campaign (1 player, never lose money) and multiplayer (any player count).
static void app_process_round_result(App* app, const RoundResult* result, int nplayers, bool campaign) {
    // Apply 7% interest on pre-round cash
    for (int p = 0; p < nplayers; p++)
        app->player_cash[p] = (107 * app->player_cash[p] + 50) / 100;

    if (campaign) {
        // Campaign: player always keeps all collected cash
        app->player_cash[0] += result->player_cash_earned[0];
    } else {
        // Multiplayer: dead players' earnings go to a pool, alive players collect
        int alive = 0;
        for (int p = 0; p < nplayers; p++)
            if (result->player_survived[p]) alive++;

        uint32_t lost_money = 0;
        for (int p = 0; p < nplayers; p++)
            if (!result->player_survived[p]) lost_money += result->player_cash_earned[p];

        if (alive == 1) lost_money += result->gold_remaining * 2 / 5;

        for (int p = 0; p < nplayers; p++) {
            if (result->player_survived[p] && alive > 0) {
                app->player_cash[p] += lost_money / alive + result->player_cash_earned[p];
                if (alive != nplayers) app->player_rounds_won[p]++;
            }
        }
    }

    // Cash floor: minimum 250 after interest (100 + 150 top-up)
    for (int p = 0; p < nplayers; p++)
        if (app->player_cash[p] < 100) app->player_cash[p] += 150;
}

static uint32_t shop_adjusted_price(App* app, int item) {
    uint32_t base = EQUIPMENT_PRICES[item];
    if (app->options.free_market) {
        uint32_t pct = 130 - (uint32_t)(rand() % 60); // 70-130%
        return ((base - 1) * pct + 50) / 100 + 1;
    }
    return base;
}

static bool shop_try_buy(App* app, int player, int item) {
    if (item < 0 || item >= EQUIP_TOTAL) return false;
    uint32_t price = shop_adjusted_price(app, item);
    if (app->player_cash[player] >= price) {
        app->player_cash[player] -= price;
        app->player_inventory[player][item]++;
        return true;
    }
    return false;
}

static bool shop_try_sell(App* app, int player, int item) {
    if (item < 0 || item >= EQUIP_TOTAL) return false;
    if (app->player_inventory[player][item] > 0) {
        app->player_inventory[player][item]--;
        app->player_cash[player] += (7 * EQUIPMENT_PRICES[item] + 5) / 10;
        return true;
    }
    return false;
}

static void render_shop_ui(App* app, ApplicationContext* ctx, int selected_item[], int shop_players[], int num_panels) {
    SDL_SetRenderTarget(ctx->renderer, ctx->buffer); SDL_RenderCopy(ctx->renderer, app->shop.texture, NULL, NULL);
    SDL_Color yellow = {255, 255, 0, 255}, white = {255, 255, 255, 255}, red = {255, 0, 0, 255};

    for (int panel = 0; panel < num_panels; ++panel) {
        int p_idx = shop_players[panel];
        int side = (num_panels == 1) ? 1 : panel; // single player uses right side
        int stats_x = side == 0 ? 0 : 420;
        int items_x = side * 320;

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
            if (is_pause_event(&e, &app->input_config)) {
                PauseChoice pc = pause_menu(app, ctx, PAUSE_CTX_SHOP);
                if (pc == PAUSE_END_GAME) return false;
                continue;
            }
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
                            else { shop_try_buy(app, pi, selected[panel]); }
                            break;
                        case ACT_STOP:
                            if (selected[panel] != EQUIP_TOTAL) { shop_try_sell(app, pi, selected[panel]); }
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
            RoundResult result = game_run(app, ctx, app->level_data[lvl_idx], NULL);

            app_process_round_result(app, &result, nplayers, false);

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
// Supports keyboard typing AND gamepad: dpad navigates grid, A=insert, B=delete, Start=confirm, Back=cancel
// flags: TEXT_UPPER = force uppercase, TEXT_FILENAME = restrict to alnum/_/-, TEXT_APPEND_MNL = append .MNL
#define TEXT_UPPER       1
#define TEXT_FILENAME    2
#define TEXT_APPEND_MNL  4

static bool text_entry_dialog(App* app, ApplicationContext* ctx, char* out_buf, int max_len,
                              const char* prompt, const char* initial, int flags) {
    // On-screen keyboard layout
    static const char* osk_rows[] = {
        "ABCDEFGHIJ",
        "KLMNOPQRST",
        "UVWXYZ0123",
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
    bool using_osk = false; // activate osk on first gamepad input
    bool result = false;

    SDL_StartTextInput();
    for (;;) {
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

        render_text(ctx->renderer, &app->font, 110, 158, white, prompt);
        render_text(ctx->renderer, &app->font, 110, 176, yellow, buf);
        render_text(ctx->renderer, &app->font, 110 + len * 8, 176, white, "_");

        if (using_osk) {
            // Draw on-screen keyboard grid
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
            render_text(ctx->renderer, &app->font, 310, 198, gray, "A:TYPE");
            render_text(ctx->renderer, &app->font, 310, 214, gray, "B:DELETE");
            render_text(ctx->renderer, &app->font, 310, 230, gray, "START:OK");
            render_text(ctx->renderer, &app->font, 310, 246, gray, "BACK:CANCEL");
        } else {
            render_text(ctx->renderer, &app->font, 110, 310, gray, "ENTER:OK  ESC:CANCEL");
        }

        SDL_SetRenderTarget(ctx->renderer, NULL);
        context_present(ctx);

        // --- Input ---
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
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

            // Gamepad input (any player's pad)
            if (e.type == SDL_CONTROLLERBUTTONDOWN) {
                using_osk = true;
                if (e.cbutton.button == SDL_CONTROLLER_BUTTON_DPAD_UP) osk_row = (osk_row + OSK_ROWS - 1) % OSK_ROWS;
                else if (e.cbutton.button == SDL_CONTROLLER_BUTTON_DPAD_DOWN) osk_row = (osk_row + 1) % OSK_ROWS;
                else if (e.cbutton.button == SDL_CONTROLLER_BUTTON_DPAD_LEFT) osk_col = (osk_col + OSK_COLS - 1) % OSK_COLS;
                else if (e.cbutton.button == SDL_CONTROLLER_BUTTON_DPAD_RIGHT) osk_col = (osk_col + 1) % OSK_COLS;
                else if (e.cbutton.button == SDL_CONTROLLER_BUTTON_A) {
                    // Insert selected character
                    char ch = osk_rows[osk_row][osk_col];
                    if (ch != ' ' && len < max_len - 1) {
                        buf[len++] = ch;
                        buf[len] = '\0';
                    }
                }
                else if (e.cbutton.button == SDL_CONTROLLER_BUTTON_B) {
                    if (len > 0) buf[--len] = '\0';
                }
                else if (e.cbutton.button == SDL_CONTROLLER_BUTTON_START) {
                    if (len > 0) { result = true; goto done; }
                }
                else if (e.cbutton.button == SDL_CONTROLLER_BUTTON_BACK) goto done;
            }
            // Left analog stick for OSK navigation
            if (e.type == SDL_CONTROLLERAXISMOTION) {
                int state = 0;
                if (e.caxis.value < -16000) state = -1;
                else if (e.caxis.value > 16000) state = 1;
                if (state != 0) {
                    using_osk = true;
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
        if (flags & TEXT_APPEND_MNL) {
            if (!strstr(out_buf, ".MNL") && !strstr(out_buf, ".mnl")) {
                int slen = (int)strlen(out_buf);
                if (slen + 4 < max_len) strcat(out_buf, ".MNL");
            }
        }
    }
    return result;
}

// Wrappers for specific contexts
static bool editor_name_dialog(App* app, ApplicationContext* ctx, char* out_name, int max_len, const char* prompt) {
    return text_entry_dialog(app, ctx, out_name, max_len, prompt, NULL, TEXT_UPPER | TEXT_FILENAME | TEXT_APPEND_MNL);
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

static void app_run_editor(App* app, ApplicationContext* ctx) {
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
    bool modified = false;
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
            if (e.type == SDL_QUIT) { running = false; break; }

            // --- Pause menu ---
            if (is_pause_event(&e, &app->input_config)) {
                PauseChoice pc = pause_menu(app, ctx, PAUSE_CTX_EDITOR);
                switch (pc) {
                    case PAUSE_ED_NEW:
                        ED_UNDO_SAVE(); editor_new_level(tiles);
                        snprintf(filename, sizeof(filename), "NEWLEVEL.MNL"); modified = false;
                        break;
                    case PAUSE_ED_LOAD: {
                        char ln[64];
                        if (editor_file_browser(app, ctx, ln, sizeof(ln))) {
                            ED_UNDO_SAVE();
                            if (editor_load_level(tiles, ctx->game_dir, ln)) {
                                snprintf(filename, sizeof(filename), "%s", ln); modified = false;
                            }
                        }
                        break;
                    }
                    case PAUSE_ED_SAVE:
                        if (strcmp(filename, "NEWLEVEL.MNL") == 0) {
                            char nn[64];
                            if (editor_name_dialog(app, ctx, nn, sizeof(nn), "SAVE AS (ENTER NAME):"))
                                snprintf(filename, sizeof(filename), "%s", nn);
                            else break;
                        }
                        if (editor_save_level(tiles, ctx->game_dir, filename)) modified = false;
                        break;
                    case PAUSE_ED_SAVE_QUIT:
                        if (strcmp(filename, "NEWLEVEL.MNL") == 0) {
                            char nn[64];
                            if (editor_name_dialog(app, ctx, nn, sizeof(nn), "SAVE AS (ENTER NAME):"))
                                snprintf(filename, sizeof(filename), "%s", nn);
                            else break;
                        }
                        if (editor_save_level(tiles, ctx->game_dir, filename)) modified = false;
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
                            modified = true;
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
                        modified = false;
                        need_redraw = true;
                    }
                    // LOAD (x 571..603, bottom half)
                    else if (bx >= ED_NEW_X1 && bx <= ED_NEW_X2 && by >= 14) {
                        char load_name[64];
                        if (editor_file_browser(app, ctx, load_name, sizeof(load_name))) {
                            ED_UNDO_SAVE();
                            if (editor_load_level(tiles, ctx->game_dir, load_name)) {
                                snprintf(filename, sizeof(filename), "%s", load_name);
                                modified = false;
                            }
                        }
                        need_redraw = true;
                    }
                    // SAVE (x 604..639, top half)
                    else if (bx >= ED_SAVE_X1 && bx <= ED_SAVE_X2 && by < 14) {
                        if (editor_save_level(tiles, ctx->game_dir, filename))
                            modified = false;
                        need_redraw = true;
                    }
                    // SAVE AS (x 604..639, bottom half)
                    else if (bx >= ED_SAVE_X1 && bx <= ED_SAVE_X2 && by >= 14) {
                        char new_name[64];
                        if (editor_name_dialog(app, ctx, new_name, sizeof(new_name), "SAVE AS (ENTER NAME):")) {
                            snprintf(filename, sizeof(filename), "%s", new_name);
                            if (editor_save_level(tiles, ctx->game_dir, filename))
                                modified = false;
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
                            modified = true;
                        } else if (mode == EDMODE_BOX) {
                            // Start drag for rectangle
                            mark_x = cx; mark_y = cy;
                            box_drag_idx = idx;
                            mouse_drawing = true;
                        } else {
                            // DOT and LINE modes: continuous paintbrush
                            ED_UNDO_SAVE();
                            editor_place_brush(tiles, cx, cy, brush, tile);
                            modified = true;
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
                            modified = true;
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
                    modified = true;
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
                            if (mode == EDMODE_FILL) { ED_UNDO_SAVE(); editor_flood_fill(tiles, cx, cy, tile); modified = true; }
                            else if (mode == EDMODE_BOX) {
                                if (mark_x < 0) { mark_x = cx; mark_y = cy; }
                                else { ED_UNDO_SAVE(); editor_draw_box(tiles, mark_x, mark_y, cx, cy, brush, tile); mark_x = -1; mark_y = -1; modified = true; }
                            } else { ED_UNDO_SAVE(); editor_place_brush(tiles, cx, cy, brush, tile); modified = true; }
                            need_redraw = true; break;
                        }
                        case ACT_STOP: {
                            ED_UNDO_SAVE(); editor_place_brush(tiles, cx, cy, brush, EDITOR_TILES[right_idx]);
                            modified = true; need_redraw = true; break;
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
                        if (continuous) { editor_place_brush(tiles, cx, cy, brush, EDITOR_TILES[left_idx]); modified = true; }
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
                    case SDL_SCANCODE_F2: ED_UNDO_SAVE(); editor_mirror_lr(tiles); modified = true; need_redraw = true; break;
                    case SDL_SCANCODE_F3: ED_UNDO_SAVE(); editor_add_random_treasures(tiles, 20); modified = true; need_redraw = true; break;
                    case SDL_SCANCODE_F7: if (brush > 1) brush--; need_redraw = true; break;
                    case SDL_SCANCODE_F8: if (brush < 5) brush++; need_redraw = true; break;
                    case SDL_SCANCODE_F9: ED_UNDO_SAVE(); editor_new_level(tiles); modified = true; need_redraw = true; break;

                    case SDL_SCANCODE_Z:
                        if (SDL_GetModState() & KMOD_CTRL) {
                            if (editor_pop_undo(undo_buf, &undo_top, &undo_count, tiles)) { modified = true; need_redraw = true; }
                        }
                        break;
                    case SDL_SCANCODE_U:
                        if (editor_pop_undo(undo_buf, &undo_top, &undo_count, tiles)) { modified = true; need_redraw = true; }
                        break;

                    case SDL_SCANCODE_S:
                        if (SDL_GetModState() & KMOD_CTRL) {
                            if (editor_save_level(tiles, ctx->game_dir, filename)) modified = false;
                            need_redraw = true;
                        } else if (SDL_GetModState() & KMOD_SHIFT) {
                            char nn[64]; if (editor_name_dialog(app, ctx, nn, sizeof(nn), "SAVE AS (ENTER NAME):")) {
                                snprintf(filename, sizeof(filename), "%s", nn);
                                if (editor_save_level(tiles, ctx->game_dir, filename)) modified = false;
                            } need_redraw = true;
                        }
                        break;
                    case SDL_SCANCODE_L:
                        if (SDL_GetModState() & KMOD_CTRL) {
                            char ln[64]; if (editor_file_browser(app, ctx, ln, sizeof(ln))) {
                                ED_UNDO_SAVE(); if (editor_load_level(tiles, ctx->game_dir, ln)) {
                                    snprintf(filename, sizeof(filename), "%s", ln); modified = false;
                                }
                            } need_redraw = true;
                        }
                        break;
                    case SDL_SCANCODE_N:
                        if (SDL_GetModState() & KMOD_CTRL) {
                            ED_UNDO_SAVE(); editor_new_level(tiles);
                            snprintf(filename, sizeof(filename), "NEWLEVEL.MNL"); modified = false; need_redraw = true;
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
                        if (continuous) { editor_place_brush(tiles, cx, cy, brush, EDITOR_TILES[left_idx]); modified = true; }
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

// ==================== Player Selection Screen ====================

#define PS_RIGHT_X 376
#define PS_RIGHT_Y 22
#define PS_LEFT_X 44
#define PS_LEFT_Y 35

static void ps_render_right_pane(App* app, ApplicationContext* ctx) {
    SDL_SetRenderTarget(ctx->renderer, ctx->buffer);
    SDL_SetRenderDrawColor(ctx->renderer, 0, 0, 0, 255);
    SDL_Rect r = {PS_RIGHT_X + 2, PS_RIGHT_Y + 1, 198, 256};
    SDL_RenderFillRect(ctx->renderer, &r);
    SDL_Color white = app->select_players.palette[1];
    SDL_Color gray = app->select_players.palette[3];
    for (int i = 0; i < ROSTER_MAX; i++) {
        int x = PS_RIGHT_X + 2, y = PS_RIGHT_Y + i * 8 + 1;
        if (app->roster.entries[i].active)
            render_text(ctx->renderer, &app->font, x, y, white, app->roster.entries[i].name);
        else
            render_text(ctx->renderer, &app->font, x, y, gray, "-");
    }
    SDL_SetRenderTarget(ctx->renderer, NULL);
}

static void ps_render_stats(App* app, ApplicationContext* ctx, const RosterInfo* stats) {
    SDL_SetRenderTarget(ctx->renderer, ctx->buffer);
    SDL_Color white = app->select_players.palette[1];
    SDL_Color red = app->select_players.palette[3];
    SDL_SetRenderDrawColor(ctx->renderer, 0, 0, 0, 255);
    // Clear stat areas
    for (int row = 0; row < 6; row++)
        for (int col = 0; col < 2; col++) {
            SDL_Rect r = {col * 146 + 64, row * 24 + 328, 95, 10};
            SDL_RenderFillRect(ctx->renderer, &r);
        }
    // Clear history area
    SDL_Rect hr = {367, 328, 198, 130};
    SDL_RenderFillRect(ctx->renderer, &hr);

    if (!stats) { SDL_SetRenderTarget(ctx->renderer, NULL); return; }

    char buf[32];
    // Tournaments
    snprintf(buf, sizeof(buf), "%u", stats->tournaments);
    render_text(ctx->renderer, &app->font, 65, 330, white, buf);
    snprintf(buf, sizeof(buf), "%u", stats->tournaments_wins);
    render_text(ctx->renderer, &app->font, 65, 354, white, buf);
    if (stats->tournaments > 0) {
        int w = 1 + (94 * stats->tournaments_wins) / stats->tournaments;
        SDL_SetRenderDrawColor(ctx->renderer, white.r, white.g, white.b, 255);
        SDL_Rect bar = {64, 376, w, 10};
        SDL_RenderFillRect(ctx->renderer, &bar);
        snprintf(buf, sizeof(buf), "%u%%", (200 * stats->tournaments_wins + stats->tournaments) / stats->tournaments / 2);
        render_text(ctx->renderer, &app->font, 65, 378, red, buf);
    }
    // Rounds
    snprintf(buf, sizeof(buf), "%u", stats->rounds);
    render_text(ctx->renderer, &app->font, 65, 402, white, buf);
    snprintf(buf, sizeof(buf), "%u", stats->rounds_wins);
    render_text(ctx->renderer, &app->font, 65, 426, white, buf);
    if (stats->rounds > 0) {
        int w = 1 + (94 * stats->rounds_wins) / stats->rounds;
        SDL_SetRenderDrawColor(ctx->renderer, white.r, white.g, white.b, 255);
        SDL_Rect bar = {64, 448, w, 10};
        SDL_RenderFillRect(ctx->renderer, &bar);
        snprintf(buf, sizeof(buf), "%u%%", (200 * stats->rounds_wins + stats->rounds) / stats->rounds / 2);
        render_text(ctx->renderer, &app->font, 65, 450, red, buf);
    }
    // Right column stats
    snprintf(buf, sizeof(buf), "%u", stats->treasures_collected);
    render_text(ctx->renderer, &app->font, 211, 330, white, buf);
    snprintf(buf, sizeof(buf), "%u", stats->total_money);
    render_text(ctx->renderer, &app->font, 211, 354, white, buf);
    snprintf(buf, sizeof(buf), "%u", stats->bombs_bought);
    render_text(ctx->renderer, &app->font, 211, 378, white, buf);
    snprintf(buf, sizeof(buf), "%u", stats->bombs_dropped);
    render_text(ctx->renderer, &app->font, 211, 402, white, buf);
    snprintf(buf, sizeof(buf), "%u", stats->deaths);
    render_text(ctx->renderer, &app->font, 211, 426, white, buf);
    snprintf(buf, sizeof(buf), "%u", stats->meters_ran);
    render_text(ctx->renderer, &app->font, 211, 450, white, buf);

    // History graph
    uint32_t offset = stats->tournaments % ROSTER_HISTORY_SIZE;
    int last_x = 367;
    int last_y = 457 - (int)stats->history[offset];
    SDL_Color* pal = app->select_players.palette;
    for (int i = 1; i < ROSTER_HISTORY_SIZE; i++) {
        offset = (offset + 1) % ROSTER_HISTORY_SIZE;
        uint8_t val = stats->history[offset];
        int y = 457 - (int)val;
        int ci = ((uint16_t)val * 4 + 67) / 134;
        SDL_Color c;
        if (ci == 0) c = pal[3];
        else if (ci == 1) c = pal[7];
        else if (ci == 2) c = pal[6];
        else if (ci == 3) c = pal[5];
        else c = pal[4];
        SDL_SetRenderDrawColor(ctx->renderer, c.r, c.g, c.b, 255);
        SDL_RenderDrawLine(ctx->renderer, last_x, last_y, last_x + 5, y);
        SDL_RenderDrawLine(ctx->renderer, last_x + 5, y, last_x + 6, y);
        last_x += 6;
        last_y = y;
    }
    SDL_SetRenderTarget(ctx->renderer, NULL);
}

static void ps_render_left_names(App* app, ApplicationContext* ctx, int num_players) {
    SDL_SetRenderTarget(ctx->renderer, ctx->buffer);
    SDL_Color color = app->select_players.palette[1];
    for (int p = 0; p < 4; p++) {
        SDL_SetRenderDrawColor(ctx->renderer, 0, 0, 0, 255);
        SDL_Rect r = {119, p * 53 + 40, 26 * 8, 10};
        SDL_RenderFillRect(ctx->renderer, &r);
        if (p < num_players) {
            int8_t ri = app->roster.identities[p];
            if (ri >= 0 && app->roster.entries[ri].active)
                render_text(ctx->renderer, &app->font, 120, p * 53 + 41, color, app->roster.entries[ri].name);
        }
    }
    SDL_SetRenderTarget(ctx->renderer, NULL);
}

static void ps_render_shovel(App* app, ApplicationContext* ctx, int prev, int cur) {
    SDL_SetRenderTarget(ctx->renderer, ctx->buffer);
    if (prev != cur) {
        int old_y = prev * 53 + PS_LEFT_Y;
        int w, h; glyphs_dimensions(GLYPH_SHOVEL_POINTER, &w, &h);
        SDL_SetRenderDrawColor(ctx->renderer, 0, 0, 0, 255);
        SDL_Rect r = {PS_LEFT_X, old_y, w, h};
        SDL_RenderFillRect(ctx->renderer, &r);
    }
    int y = cur * 53 + PS_LEFT_Y;
    glyphs_render(&app->glyphs, ctx->renderer, PS_LEFT_X, y, GLYPH_SHOVEL_POINTER);
    SDL_SetRenderTarget(ctx->renderer, NULL);
}

static void ps_render_arrow(App* app, ApplicationContext* ctx, int pos) {
    SDL_SetRenderTarget(ctx->renderer, ctx->buffer);
    int y = pos * 8 + PS_RIGHT_Y;
    glyphs_render(&app->glyphs, ctx->renderer, PS_RIGHT_X - 37, y, GLYPH_ARROW_POINTER);
    SDL_SetRenderTarget(ctx->renderer, NULL);
}

static void ps_clear_arrow(App* app __attribute__((unused)), ApplicationContext* ctx, int pos) {
    SDL_SetRenderTarget(ctx->renderer, ctx->buffer);
    int y = pos * 8 + PS_RIGHT_Y;
    int w, h; glyphs_dimensions(GLYPH_ARROW_POINTER, &w, &h);
    SDL_SetRenderDrawColor(ctx->renderer, 0, 0, 0, 255);
    SDL_Rect r = {PS_RIGHT_X - 37, y, w, h};
    SDL_RenderFillRect(ctx->renderer, &r);
    SDL_SetRenderTarget(ctx->renderer, NULL);
}

// Edit/create a player name at roster index. Returns true if name was entered.
static bool ps_edit_name(App* app, ApplicationContext* ctx, int roster_idx) {
    char name[ROSTER_NAME_MAX] = "";
    if (!text_entry_dialog(app, ctx, name, ROSTER_NAME_MAX, "ENTER PLAYER NAME:", NULL, TEXT_UPPER))
        return false;
    if (name[0] == '\0') return false;
    RosterInfo* entry = &app->roster.entries[roster_idx];
    memset(entry, 0, sizeof(RosterInfo));
    entry->active = true;
    snprintf(entry->name, ROSTER_NAME_MAX, "%s", name);
    // Redraw the player select background since text_entry_dialog drew over it
    SDL_SetRenderTarget(ctx->renderer, ctx->buffer);
    SDL_RenderCopy(ctx->renderer, app->select_players.texture, NULL, NULL);
    SDL_SetRenderTarget(ctx->renderer, NULL);
    ps_render_right_pane(app, ctx);
    return true;
}

// Name select sub-menu: browse 32 roster slots, select or create
// Returns roster index selected, or -1 if cancelled
static int ps_name_select(App* app, ApplicationContext* ctx) {
    int arrow = 0;
    ps_render_arrow(app, ctx, arrow);
    ps_render_stats(app, ctx, app->roster.entries[arrow].active ? &app->roster.entries[arrow] : NULL);
    context_present(ctx);

    int result = -1;
    bool running = true;
    int axis_y_state = 0;
    while (running) {
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_QUIT) { running = false; break; }

            int prev = arrow;
            bool nav = false; // did arrow move?

            if (e.type == SDL_KEYDOWN && !e.key.repeat) {
                switch (e.key.keysym.scancode) {
                    case SDL_SCANCODE_DOWN: case SDL_SCANCODE_KP_2:
                        arrow = (arrow + 1) % ROSTER_MAX; nav = true; break;
                    case SDL_SCANCODE_UP: case SDL_SCANCODE_KP_8:
                        arrow = (arrow + ROSTER_MAX - 1) % ROSTER_MAX; nav = true; break;
                    case SDL_SCANCODE_LEFT: case SDL_SCANCODE_KP_4:
                        if (app->roster.entries[arrow].active) result = arrow;
                        running = false; break;
                    case SDL_SCANCODE_ESCAPE: case SDL_SCANCODE_F10:
                        running = false; break;
                    case SDL_SCANCODE_BACKSPACE: case SDL_SCANCODE_DELETE:
                        app->roster.entries[arrow].active = false;
                        for (int i = 0; i < MAX_PLAYERS; i++)
                            if (app->roster.identities[i] == arrow) app->roster.identities[i] = -1;
                        ps_render_right_pane(app, ctx);
                        ps_render_stats(app, ctx, NULL);
                        context_present(ctx);
                        break;
                    case SDL_SCANCODE_RETURN: case SDL_SCANCODE_KP_ENTER:
                        ps_edit_name(app, ctx, arrow);
                        if (app->roster.entries[arrow].active) { result = arrow; running = false; }
                        ps_render_arrow(app, ctx, arrow);
                        ps_render_stats(app, ctx, app->roster.entries[arrow].active ? &app->roster.entries[arrow] : NULL);
                        context_present(ctx);
                        break;
                    default: break;
                }
            }
            // Gamepad buttons
            if (e.type == SDL_CONTROLLERBUTTONDOWN) {
                if (e.cbutton.button == SDL_CONTROLLER_BUTTON_DPAD_DOWN) { arrow = (arrow + 1) % ROSTER_MAX; nav = true; }
                else if (e.cbutton.button == SDL_CONTROLLER_BUTTON_DPAD_UP) { arrow = (arrow + ROSTER_MAX - 1) % ROSTER_MAX; nav = true; }
                else if (e.cbutton.button == SDL_CONTROLLER_BUTTON_B || e.cbutton.button == SDL_CONTROLLER_BUTTON_DPAD_LEFT) {
                    if (app->roster.entries[arrow].active) result = arrow;
                    running = false;
                }
                else if (e.cbutton.button == SDL_CONTROLLER_BUTTON_BACK) { running = false; }
                else if (e.cbutton.button == SDL_CONTROLLER_BUTTON_X) {
                    // X = delete entry
                    app->roster.entries[arrow].active = false;
                    for (int i = 0; i < MAX_PLAYERS; i++)
                        if (app->roster.identities[i] == arrow) app->roster.identities[i] = -1;
                    ps_render_right_pane(app, ctx);
                    ps_render_stats(app, ctx, NULL);
                    context_present(ctx);
                }
                else if (e.cbutton.button == SDL_CONTROLLER_BUTTON_A) {
                    // A = edit/create name
                    ps_edit_name(app, ctx, arrow);
                    if (app->roster.entries[arrow].active) { result = arrow; running = false; }
                    ps_render_arrow(app, ctx, arrow);
                    ps_render_stats(app, ctx, app->roster.entries[arrow].active ? &app->roster.entries[arrow] : NULL);
                    context_present(ctx);
                }
            }
            // Left analog Y for scrolling roster
            if (e.type == SDL_CONTROLLERAXISMOTION && e.caxis.axis == SDL_CONTROLLER_AXIS_LEFTY) {
                int state = 0;
                if (e.caxis.value < -16000) state = -1;
                else if (e.caxis.value > 16000) state = 1;
                if (state != axis_y_state) {
                    axis_y_state = state;
                    if (state < 0) { arrow = (arrow + ROSTER_MAX - 1) % ROSTER_MAX; nav = true; }
                    else if (state > 0) { arrow = (arrow + 1) % ROSTER_MAX; nav = true; }
                }
            }

            if (e.type == SDL_TEXTINPUT) {
                ps_edit_name(app, ctx, arrow);
                if (app->roster.entries[arrow].active) { result = arrow; running = false; }
                ps_render_arrow(app, ctx, arrow);
                ps_render_stats(app, ctx, app->roster.entries[arrow].active ? &app->roster.entries[arrow] : NULL);
                context_present(ctx);
            }

            if (nav && prev != arrow && running) {
                ps_clear_arrow(app, ctx, prev);
                ps_render_arrow(app, ctx, arrow);
                ps_render_stats(app, ctx, app->roster.entries[arrow].active ? &app->roster.entries[arrow] : NULL);
                context_present(ctx);
            }
        }
        SDL_Delay(16);
    }
    ps_clear_arrow(app, ctx, arrow);
    context_present(ctx);
    return result;
}

// Main player selection screen. Returns true if all players selected, false if cancelled.
static bool app_run_player_select(App* app, ApplicationContext* ctx) {
    int num_players = app->options.players;
    int active = 4; // 0-3 = player slots, 4 = "Play" button

    if (!app->select_players.texture) return true; // no texture, skip

    SDL_SetRenderTarget(ctx->renderer, ctx->buffer);
    SDL_RenderCopy(ctx->renderer, app->select_players.texture, NULL, NULL);
    // Erase unused player panels
    SDL_SetRenderDrawColor(ctx->renderer, 0, 0, 0, 255);
    for (int p = num_players; p < 4; p++) {
        SDL_Rect r = {39, p * 53 + 18, 293, 53};
        SDL_RenderFillRect(ctx->renderer, &r);
    }
    SDL_SetRenderTarget(ctx->renderer, NULL);

    ps_render_right_pane(app, ctx);
    ps_render_left_names(app, ctx, num_players);
    ps_render_shovel(app, ctx, active, active);
    // Show stats for current active slot
    {
        const RosterInfo* st = NULL;
        if (active < 4 && app->roster.identities[active] >= 0)
            st = &app->roster.entries[app->roster.identities[active]];
        ps_render_stats(app, ctx, st);
    }
    context_present(ctx);
    context_animate(ctx, ANIMATION_FADE_UP, 7);

    bool result = false;
    bool running = true;
    int axis_y_state = 0;

    // Helper: advance active down, skipping unused player slots
    #define PS_MOVE_DOWN() do { active++; \
        if (active > 4) active = 0; \
        else if (active != 4 && active >= num_players) active = 4; } while(0)
    #define PS_MOVE_UP() do { \
        if (active == 0) active = 4; \
        else { active--; if (active >= num_players) active = num_players - 1; } } while(0)
    #define PS_CONFIRM() do { \
        if (active == 4) { \
            bool all_ok = true; \
            for (int i = 0; i < num_players; i++) \
                if (app->roster.identities[i] < 0) { all_ok = false; break; } \
            if (all_ok) { result = true; running = false; } \
        } else { \
            int sel = ps_name_select(app, ctx); \
            if (sel >= 0) app->roster.identities[active] = (int8_t)sel; \
            ps_render_left_names(app, ctx, num_players); \
        } } while(0)

    while (running) {
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_QUIT) { running = false; break; }

            int prev = active;

            if (e.type == SDL_KEYDOWN && !e.key.repeat) {
                switch (e.key.keysym.scancode) {
                    case SDL_SCANCODE_DOWN: case SDL_SCANCODE_KP_2: PS_MOVE_DOWN(); break;
                    case SDL_SCANCODE_UP: case SDL_SCANCODE_KP_8: PS_MOVE_UP(); break;
                    case SDL_SCANCODE_F10: case SDL_SCANCODE_ESCAPE: running = false; break;
                    case SDL_SCANCODE_RETURN: case SDL_SCANCODE_KP_ENTER:
                    case SDL_SCANCODE_RIGHT: case SDL_SCANCODE_KP_6: PS_CONFIRM(); break;
                    default: break;
                }
            }
            // Gamepad
            if (e.type == SDL_CONTROLLERBUTTONDOWN) {
                if (e.cbutton.button == SDL_CONTROLLER_BUTTON_DPAD_DOWN) PS_MOVE_DOWN();
                else if (e.cbutton.button == SDL_CONTROLLER_BUTTON_DPAD_UP) PS_MOVE_UP();
                else if (e.cbutton.button == SDL_CONTROLLER_BUTTON_A || e.cbutton.button == SDL_CONTROLLER_BUTTON_DPAD_RIGHT) PS_CONFIRM();
                else if (e.cbutton.button == SDL_CONTROLLER_BUTTON_B || e.cbutton.button == SDL_CONTROLLER_BUTTON_BACK) running = false;
            }
            // Left analog Y
            if (e.type == SDL_CONTROLLERAXISMOTION && e.caxis.axis == SDL_CONTROLLER_AXIS_LEFTY) {
                int state = 0;
                if (e.caxis.value < -16000) state = -1;
                else if (e.caxis.value > 16000) state = 1;
                if (state != axis_y_state) {
                    axis_y_state = state;
                    if (state < 0) PS_MOVE_UP();
                    else if (state > 0) PS_MOVE_DOWN();
                }
            }

            if (prev != active) {
                ps_render_shovel(app, ctx, prev, active);
                const RosterInfo* st = NULL;
                if (active < 4 && app->roster.identities[active] >= 0)
                    st = &app->roster.entries[app->roster.identities[active]];
                ps_render_stats(app, ctx, st);
            }
            context_present(ctx);

            if (e.type == SDL_TEXTINPUT && active < 4) {
                int empty = -1;
                for (int i = 0; i < ROSTER_MAX; i++)
                    if (!app->roster.entries[i].active) { empty = i; break; }
                if (empty < 0) empty = ROSTER_MAX - 1;
                ps_edit_name(app, ctx, empty);
                if (app->roster.entries[empty].active)
                    app->roster.identities[active] = (int8_t)empty;
                ps_render_left_names(app, ctx, num_players);
                context_present(ctx);
            }
        }
        SDL_Delay(16);
    }
    #undef PS_MOVE_DOWN
    #undef PS_MOVE_UP
    #undef PS_CONFIRM

    // Copy selected names to player_name slots
    for (int i = 0; i < num_players; i++) {
        int8_t ri = app->roster.identities[i];
        if (ri >= 0 && app->roster.entries[ri].active)
            snprintf(app->player_name[i], sizeof(app->player_name[i]), "%s", app->roster.entries[ri].name);
    }

    // Save roster and identities
    roster_save(&app->roster, ctx->game_dir);
    identities_save(&app->roster, ctx->game_dir);

    // Reset per-game stats
    memset(app->game_stats, 0, sizeof(app->game_stats));

    context_animate(ctx, ANIMATION_FADE_DOWN, 7);
    return result;
}

static bool level_has_exit(const uint8_t* level_data) {
    for (int y = 0; y < 45; y++)
        for (int x = 0; x < 66; x++)
            if (level_data[y * 66 + x] == TILE_EXIT) return true;
    return false;
}

static void validate_campaign_levels(App* app) {
    for (int i = 0; i < app->campaign_level_count; i++) {
        if (!app->campaign_levels[i]) {
            fprintf(stderr, "WARNING: Campaign level %d not loaded\n", i);
            continue;
        }
        if (!level_has_exit(app->campaign_levels[i])) {
            fprintf(stderr, "WARNING: Campaign level %d (LEVEL%d.MNL) has no exit tile\n", i, i);
        }
    }
}

static void app_run_campaign(App* app, ApplicationContext* ctx) {
    if (app->campaign_level_count == 0) return;
    validate_campaign_levels(app);

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
                            else { shop_try_buy(app, 0, selected[0]); }
                            break;
                        case ACT_STOP:
                            if (selected[0] != EQUIP_TOTAL) { shop_try_sell(app, 0, selected[0]); }
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
        RoundResult result = game_run(app, ctx, app->campaign_levels[app->current_round], NULL);
        app->player_lives += result.lives_gained;

        app_process_round_result(app, &result, 1, true);

        context_linger_music_start(ctx);
        context_animate(ctx, ANIMATION_FADE_DOWN, 7);
        context_linger_music_end(ctx);

        if (result.end_type == ROUND_END_EXITED) {
            app->current_round++;
        } else if (result.end_type == ROUND_END_FAILED || result.end_type == ROUND_END_NORMAL) {
            app->player_lives--;
        } else if (result.end_type == ROUND_END_QUIT || result.end_type == ROUND_END_FINAL) {
            break;
        }
    }

    bool win = (app->current_round >= max_rounds);
    app_run_campaign_end(app, ctx, win);
}

#ifdef MB_NET
// ============================================================
// Networked game: lobby + net shop
// ============================================================

static void pack_options_to_game_start(const GameOptions* o, NetGameStart* gs) {
    gs->cash = o->cash;
    gs->treasures = o->treasures;
    gs->rounds = o->rounds;
    gs->round_time_secs = o->round_time_secs;
    gs->players = o->players;
    gs->speed = o->speed;
    gs->bomb_damage = o->bomb_damage;
    gs->flags = (o->darkness ? 1 : 0) | (o->free_market ? 2 : 0)
              | (o->selling ? 4 : 0) | (o->win_by_money ? 8 : 0);
}

static void unpack_game_start_to_options(const NetGameStart* gs, GameOptions* o) {
    o->cash = gs->cash;
    o->treasures = gs->treasures;
    o->rounds = gs->rounds;
    o->round_time_secs = gs->round_time_secs;
    o->players = gs->players;
    o->speed = gs->speed;
    o->bomb_damage = gs->bomb_damage;
    o->darkness = (gs->flags & 1) != 0;
    o->free_market = (gs->flags & 2) != 0;
    o->selling = (gs->flags & 4) != 0;
    o->win_by_money = (gs->flags & 8) != 0;
}


// Check if all active net players satisfy a condition array (e.g. all ready)
static bool net_all_players_check(const NetContext* net, const bool flags[NET_MAX_PLAYERS]) {
    for (int s = 0; s < NET_MAX_PLAYERS; s++) {
        if (net_slot_active(net, s) && !flags[s]) return false;
    }
    return true;
}

// Send a SHOP_STATE broadcast for a given player from the server
static void net_broadcast_shop_state(App* app, NetContext* net, int pi, int cursor) {
    NetMessage msg = {0};
    msg.type = NET_MSG_SHOP_STATE;
    msg.data.shop_state.player_index = pi;
    msg.data.shop_state.cash = app->player_cash[pi];
    memcpy(msg.data.shop_state.inventory, app->player_inventory[pi], sizeof(msg.data.shop_state.inventory));
    msg.data.shop_state.cursor = cursor;
    net_broadcast(net, &msg);
}

// Send cursor position to server (client) or broadcast to all (server)
static void net_send_cursor(App* app, NetContext* net, int cursor) {
    NetMessage msg = {0};
    msg.type = NET_MSG_SHOP_CURSOR;
    msg.data.shop_cursor.player_index = net->local_player;
    msg.data.shop_cursor.cursor = cursor;
    if (net->is_server)
        net_broadcast(net, &msg);
    else
        net_send_to(net->server_peer, &msg);
    (void)app;
}

// Returns: 1=all ready, 0=disconnect, -1=F10 quit match
static int app_run_net_shop(App* app, ApplicationContext* ctx) {
    NetContext* net = &app->net;
    int local = net->local_player;
    int cursors[NET_MAX_PLAYERS] = {0};

    context_play_music_at(ctx, "OEKU.S3M", 83);

    // Build ordered player list from active slots
    int all_players[NET_MAX_PLAYERS];
    int total_players = 0;
    for (int s = 0; s < NET_MAX_PLAYERS; s++) {
        if (net_slot_active(net, s))
            all_players[total_players++] = s;
    }

    // Process shop in batches of 2, like local multiplayer
    for (int batch_start = 0; batch_start < total_players; batch_start += 2) {
        int batch[2];
        int num_batch = 0;
        for (int i = batch_start; i < total_players && i < batch_start + 2; i++)
            batch[num_batch++] = all_players[i];
        // Swap so lower-index player is on right panel (matching local MP layout)
        if (num_batch == 2) { int tmp = batch[0]; batch[0] = batch[1]; batch[1] = tmp; }

        // Is local player in this batch?
        bool local_in_batch = false;
        for (int i = 0; i < num_batch; i++)
            if (batch[i] == local) local_in_batch = true;

        bool batch_done = false;
        bool ready_local = false;
        bool batch_ready[NET_MAX_PLAYERS] = {false};

        while (!batch_done) {
            // Render: show this batch's panels if local is in batch, otherwise waiting screen
            if (local_in_batch) {
                int sel_arr[2];
                for (int i = 0; i < num_batch; i++)
                    sel_arr[i] = cursors[batch[i]];
                render_shop_ui(app, ctx, sel_arr, batch, num_batch);
            } else {
                SDL_SetRenderTarget(ctx->renderer, ctx->buffer);
                SDL_RenderCopy(ctx->renderer, app->shop.texture, NULL, NULL);
                SDL_Color gray = {128, 128, 128, 255};
                char wait_msg[64];
                snprintf(wait_msg, sizeof(wait_msg), "WAITING FOR PLAYERS %d-%d",
                         all_players[batch_start] + 1,
                         all_players[batch_start + num_batch - 1] + 1);
                render_text(ctx->renderer, &app->font, 180, 230, gray, wait_msg);
                SDL_SetRenderTarget(ctx->renderer, NULL);
            }
            context_present(ctx);

            // Input: only process if local player is in this batch and not yet ready
            SDL_Event e;
            while (SDL_PollEvent(&e)) {
                if (e.type == SDL_QUIT) { return 0; }
                if (net->is_server && is_pause_event(&e, &app->input_config)) {
                    PauseChoice pc = pause_menu_net(app, ctx, PAUSE_CTX_SHOP, net);
                    if (pc == PAUSE_END_GAME) {
                        NetMessage quit_msg = {0};
                        quit_msg.type = NET_MSG_SHOP_ALL_READY;
                        quit_msg.data.shop_all_ready.next_round = -1;
                        net_broadcast(net, &quit_msg);
                        net_flush(net);
                        return -1;
                    }
                }
                if (!local_in_batch || ready_local) continue;
                ActionType act = input_map_event(&e, 0, &app->input_config);
                if (act == ACT_MAX_PLAYER) continue;
                int prev_cursor = cursors[local];
                switch (act) {
                    case ACT_UP:    if (cursors[local] >= 4) cursors[local] -= 4; break;
                    case ACT_DOWN:  if (cursors[local] + 4 <= EQUIP_TOTAL) cursors[local] += 4; else cursors[local] = EQUIP_TOTAL; break;
                    case ACT_LEFT:  cursors[local] = (cursors[local] + EQUIP_TOTAL) % (EQUIP_TOTAL + 1); break;
                    case ACT_RIGHT: cursors[local] = (cursors[local] + 1) % (EQUIP_TOTAL + 1); break;
                    case ACT_ACTION:
                        if (cursors[local] == EQUIP_TOTAL) {
                            ready_local = true;
                            if (net->is_server) {
                                batch_ready[local] = true;
                            } else {
                                NetMessage msg = {0};
                                msg.type = NET_MSG_SHOP_READY;
                                msg.data.shop_ready.player_index = local;
                                net_send_to(net->server_peer, &msg);
                            }
                        } else {
                            if (net->is_server) {
                                if (shop_try_buy(app, local, cursors[local]))
                                    net_broadcast_shop_state(app, net, local, cursors[local]);
                            } else {
                                NetMessage msg = {0};
                                msg.type = NET_MSG_SHOP_ACTION;
                                msg.data.shop_action.player_index = local;
                                msg.data.shop_action.action = SHOP_ACT_BUY;
                                msg.data.shop_action.item_index = cursors[local];
                                net_send_to(net->server_peer, &msg);
                            }
                        }
                        break;
                    case ACT_STOP:
                        if (cursors[local] != EQUIP_TOTAL) {
                            if (net->is_server) {
                                if (shop_try_sell(app, local, cursors[local]))
                                    net_broadcast_shop_state(app, net, local, cursors[local]);
                            } else {
                                NetMessage msg = {0};
                                msg.type = NET_MSG_SHOP_ACTION;
                                msg.data.shop_action.player_index = local;
                                msg.data.shop_action.action = SHOP_ACT_SELL;
                                msg.data.shop_action.item_index = cursors[local];
                                net_send_to(net->server_peer, &msg);
                            }
                        }
                        break;
                    default: break;
                }
                if (cursors[local] != prev_cursor)
                    net_send_cursor(app, net, cursors[local]);
            }

            // Server: check if all batch players are ready
            if (net->is_server) {
                bool all_batch_ready = true;
                for (int i = 0; i < num_batch; i++)
                    if (!batch_ready[batch[i]]) { all_batch_ready = false; break; }
                if (all_batch_ready) {
                    NetMessage msg = {0};
                    msg.type = NET_MSG_SHOP_ALL_READY;
                    msg.data.shop_all_ready.next_round = batch_start; // batch index
                    net_broadcast(net, &msg);
                    net_flush(net);
                    batch_done = true;
                }
            }

            // Poll network
            NetMessage net_msg;
            ENetPeer* from;
            while (net_poll(net, &net_msg, &from) > 0) {
                switch (net_msg.type) {
                    case NET_MSG_SHOP_ACTION:
                        if (net->is_server) {
                            int pi = net_msg.data.shop_action.player_index;
                            if (pi >= 0 && pi < NET_MAX_PLAYERS) {
                                if (net_msg.data.shop_action.action == SHOP_ACT_BUY)
                                    shop_try_buy(app, pi, net_msg.data.shop_action.item_index);
                                else
                                    shop_try_sell(app, pi, net_msg.data.shop_action.item_index);
                                net_broadcast_shop_state(app, net, pi, net_msg.data.shop_action.item_index);
                            }
                        }
                        break;
                    case NET_MSG_SHOP_STATE: {
                        int pi = net_msg.data.shop_state.player_index;
                        if (pi >= 0 && pi < NET_MAX_PLAYERS) {
                            app->player_cash[pi] = net_msg.data.shop_state.cash;
                            memcpy(app->player_inventory[pi], net_msg.data.shop_state.inventory, sizeof(app->player_inventory[pi]));
                            cursors[pi] = net_msg.data.shop_state.cursor;
                        }
                        break;
                    }
                    case NET_MSG_SHOP_CURSOR: {
                        int pi = net_msg.data.shop_cursor.player_index;
                        if (pi >= 0 && pi < NET_MAX_PLAYERS)
                            cursors[pi] = net_msg.data.shop_cursor.cursor;
                        if (net->is_server)
                            net_broadcast(net, &net_msg);
                        break;
                    }
                    case NET_MSG_SHOP_READY:
                        if (net->is_server) {
                            int pi = net_msg.data.shop_ready.player_index;
                            if (pi >= 0 && pi < NET_MAX_PLAYERS)
                                batch_ready[pi] = true;
                        }
                        break;
                    case NET_MSG_SHOP_ALL_READY:
                        if (net_msg.data.shop_all_ready.next_round == -1) {
                            if (net->is_server) net_broadcast(net, &net_msg);
                            return -1;
                        }
                        batch_done = true;
                        break;
                    case NET_MSG_PLAYER_LEAVE:
                        return 0;
                    default:
                        break;
                }
            }

            SDL_Delay(16);
        }
    }
    return 1;
}

static void app_run_netgame(App* app, ApplicationContext* ctx) {
    NetContext* net = &app->net;
    SDL_Color white = {255, 255, 255, 255};
    SDL_Color yellow = {255, 255, 0, 255};
    SDL_Color green = {0, 255, 0, 255};
    SDL_Color gray = {128, 128, 128, 255};

    if (!net_init()) return;

    int saved_level_count = app->selected_level_count;

    // Phase A0: Name entry
    char net_name[NET_PLAYER_NAME_LEN];
    snprintf(net_name, sizeof(net_name), "%s", app->player_name[0]);
    int name_cursor = (int)strlen(net_name);
    {
        bool entering_name = true;
        SDL_StartTextInput();
        while (entering_name) {
            SDL_SetRenderTarget(ctx->renderer, ctx->buffer);
            SDL_SetRenderDrawColor(ctx->renderer, 0, 0, 0, 255);
            SDL_RenderClear(ctx->renderer);
            render_text(ctx->renderer, &app->font, 200, 100, white, "NETGAME");
            render_text(ctx->renderer, &app->font, 160, 140, gray, "ENTER YOUR NAME:");
            char display[32];
            snprintf(display, sizeof(display), "%s_", net_name);
            render_text(ctx->renderer, &app->font, 200, 165, yellow, display);
            render_text(ctx->renderer, &app->font, 160, 220, gray, "TYPE NAME  ENTER CONFIRM  ESC BACK");
            SDL_SetRenderTarget(ctx->renderer, NULL);
            context_present(ctx);

            SDL_Event e;
            while (SDL_PollEvent(&e)) {
                if (e.type == SDL_QUIT) { SDL_StopTextInput(); net_shutdown(); return; }
                if (e.type == SDL_KEYDOWN) {
                    if (e.key.keysym.scancode == SDL_SCANCODE_ESCAPE) { SDL_StopTextInput(); net_shutdown(); return; }
                    if (e.key.keysym.scancode == SDL_SCANCODE_BACKSPACE && name_cursor > 0) {
                        net_name[--name_cursor] = '\0';
                    }
                    if (e.key.keysym.scancode == SDL_SCANCODE_RETURN || e.key.keysym.scancode == SDL_SCANCODE_KP_ENTER) {
                        if (name_cursor > 0) entering_name = false;
                    }
                }
                if (e.type == SDL_TEXTINPUT && name_cursor < NET_PLAYER_NAME_LEN - 1) {
                    char ch = e.text.text[0];
                    if (ch >= 32 && ch < 127) {
                        net_name[name_cursor++] = ch;
                        net_name[name_cursor] = '\0';
                    }
                }
            }
            SDL_Delay(16);
        }
        SDL_StopTextInput();
    }

    // Phase A: Host/Join selection
    int choice = 0; // 0=host, 1=join
    bool selecting = true;
    while (selecting) {
        SDL_SetRenderTarget(ctx->renderer, ctx->buffer);
        SDL_SetRenderDrawColor(ctx->renderer, 0, 0, 0, 255);
        SDL_RenderClear(ctx->renderer);
        render_text(ctx->renderer, &app->font, 200, 100, white, "NETGAME");
        render_text(ctx->renderer, &app->font, 200, 140, choice == 0 ? yellow : gray, "> HOST GAME");
        render_text(ctx->renderer, &app->font, 200, 160, choice == 1 ? yellow : gray, "> JOIN GAME");
        render_text(ctx->renderer, &app->font, 160, 220, gray, "UP/DOWN SELECT  ENTER CONFIRM  ESC BACK");
        SDL_SetRenderTarget(ctx->renderer, NULL);
        context_present(ctx);

        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_QUIT) { net_shutdown(); return; }
            if (e.type == SDL_KEYDOWN) {
                if (e.key.keysym.scancode == SDL_SCANCODE_ESCAPE) { net_shutdown(); return; }
                if (e.key.keysym.scancode == SDL_SCANCODE_UP) choice = 0;
                if (e.key.keysym.scancode == SDL_SCANCODE_DOWN) choice = 1;
                if (e.key.keysym.scancode == SDL_SCANCODE_RETURN || e.key.keysym.scancode == SDL_SCANCODE_KP_ENTER) selecting = false;
            }
        }
        SDL_Delay(16);
    }

    // Phase B: Connect
    if (choice == 0) {
        // Host
        if (!net_host_create(net, NET_PORT)) {
            net_shutdown();
            return;
        }
        snprintf(net->player_names[0], NET_PLAYER_NAME_LEN, "%s", net_name);
    } else {
        // Join - hostname entry
        char hostname[128] = "localhost";
        int cursor = (int)strlen(hostname);
        bool entering = true;
        bool connecting = false;
        SDL_StartTextInput();

        while (entering) {
            SDL_SetRenderTarget(ctx->renderer, ctx->buffer);
            SDL_SetRenderDrawColor(ctx->renderer, 0, 0, 0, 255);
            SDL_RenderClear(ctx->renderer);
            render_text(ctx->renderer, &app->font, 200, 100, white, "ENTER HOST ADDRESS:");
            char display[140];
            snprintf(display, sizeof(display), "%s_", hostname);
            render_text(ctx->renderer, &app->font, 200, 130, yellow, connecting ? "CONNECTING..." : display);
            render_text(ctx->renderer, &app->font, 160, 200, gray, "TYPE ADDRESS  ENTER CONNECT  ESC BACK");
            SDL_SetRenderTarget(ctx->renderer, NULL);
            context_present(ctx);

            if (connecting) {
                if (!net_client_connect(net, hostname, NET_PORT)) {
                    // Show failure briefly
                    SDL_SetRenderTarget(ctx->renderer, ctx->buffer);
                    SDL_SetRenderDrawColor(ctx->renderer, 0, 0, 0, 255);
                    SDL_RenderClear(ctx->renderer);
                    SDL_Color red = {255, 0, 0, 255};
                    render_text(ctx->renderer, &app->font, 200, 140, red, "CONNECTION FAILED");
                    SDL_SetRenderTarget(ctx->renderer, NULL);
                    context_present(ctx);
                    SDL_Delay(2000);
                    connecting = false;
                    continue;
                }
                SDL_StopTextInput();
                entering = false;
                break;
            }

            SDL_Event e;
            while (SDL_PollEvent(&e)) {
                if (e.type == SDL_QUIT) { SDL_StopTextInput(); net_shutdown(); return; }
                if (e.type == SDL_KEYDOWN) {
                    if (e.key.keysym.scancode == SDL_SCANCODE_ESCAPE) {
                        SDL_StopTextInput();
                        net_shutdown();
                        return;
                    }
                    if (e.key.keysym.scancode == SDL_SCANCODE_BACKSPACE && cursor > 0) {
                        hostname[--cursor] = '\0';
                    }
                    if (e.key.keysym.scancode == SDL_SCANCODE_RETURN || e.key.keysym.scancode == SDL_SCANCODE_KP_ENTER) {
                        connecting = true;
                    }
                }
                if (e.type == SDL_TEXTINPUT && cursor < 126) {
                    int len = (int)strlen(e.text.text);
                    if (cursor + len < 127) {
                        memcpy(hostname + cursor, e.text.text, len);
                        cursor += len;
                        hostname[cursor] = '\0';
                    }
                }
            }
            SDL_Delay(16);
        }
    }

    // Phase C: Lobby
    bool in_lobby = true;
    bool game_started = false;

    while (in_lobby) {
        // Render lobby
        SDL_SetRenderTarget(ctx->renderer, ctx->buffer);
        SDL_SetRenderDrawColor(ctx->renderer, 0, 0, 0, 255);
        SDL_RenderClear(ctx->renderer);

        render_text(ctx->renderer, &app->font, 260, 10, white, "LOBBY");
        char info[128];
        snprintf(info, sizeof(info), "CASH:%u ROUNDS:%u SPEED:%u", app->options.cash, app->options.rounds, app->options.speed);
        render_text(ctx->renderer, &app->font, 160, 30, gray, info);
        snprintf(info, sizeof(info), "PORT:%d  PLAYERS:%d", NET_PORT, net->player_count);
        render_text(ctx->renderer, &app->font, 200, 44, gray, info);

        // Player avatars and names
        for (int i = 0; i < NET_MAX_PLAYERS; i++) {
            int ax = 32 + 150 * i;
            if (net_slot_active(net, i)) {
                // Draw win portrait preview
                if (app->avatar_win[i].texture) {
                    SDL_Rect dest = {ax, 70, 132, 218};
                    SDL_RenderCopy(ctx->renderer, app->avatar_win[i].texture, NULL, &dest);
                }
                const char* name = net->player_names[i][0] ? net->player_names[i] : "???";
                render_text(ctx->renderer, &app->font, ax + 4, 292, net->player_ready[i] ? green : white, name);
                render_text(ctx->renderer, &app->font, ax + 4, 308,
                            net->player_ready[i] ? green : gray,
                            net->player_ready[i] ? "READY" : "waiting");
            } else {
                // Empty slot - dim placeholder
                render_text(ctx->renderer, &app->font, ax + 30, 170, gray, "---");
            }
        }

        if (net->is_server)
            render_text(ctx->renderer, &app->font, 140, 340, gray, "ENTER=TOGGLE READY  ESC=QUIT");
        else
            render_text(ctx->renderer, &app->font, 140, 340, gray, "ENTER=TOGGLE READY  ESC=DISCONNECT");

        context_present(ctx);

        // Input
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_QUIT) { net_disconnect(net); net_shutdown(); return; }
            if (e.type == SDL_KEYDOWN) {
                if (e.key.keysym.scancode == SDL_SCANCODE_ESCAPE) {
                    net_disconnect(net);
                    net_shutdown();
                    return;
                }
                if (e.key.keysym.scancode == SDL_SCANCODE_RETURN || e.key.keysym.scancode == SDL_SCANCODE_KP_ENTER) {
                    net->player_ready[net->local_player] = !net->player_ready[net->local_player];
                    if (net->is_server) {
                        // Broadcast ready update
                        NetMessage msg = {0};
                        msg.type = NET_MSG_READY_UPDATE;
                        memcpy(msg.data.ready_update.ready, net->player_ready, sizeof(msg.data.ready_update.ready));
                        net_broadcast(net, &msg);

                        // Check if all ready and we have 2+ players
                        if (net->player_count >= 2 && net_all_players_check(net, net->player_ready)) {
                                // Generate level list now so it's included in GAME_START
                                app->options.players = (uint8_t)net->player_count;
                                if (app->selected_level_count == 0 && app->level_count > 0) {
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
                                // Send GAME_START with level selection
                                NetMessage gs = {0};
                                gs.type = NET_MSG_GAME_START;
                                pack_options_to_game_start(&app->options, &gs.data.game_start);
                                gs.data.game_start.level_count = app->selected_level_count;
                                gs.data.game_start.rng_seed = (uint32_t)SDL_GetTicks() ^ 0xDEADBEEF;
                                if (app->selected_level_count > 0)
                                    memcpy(gs.data.game_start.selected_levels, app->selected_levels,
                                           app->selected_level_count * sizeof(int));
                                net_broadcast(net, &gs);
                                game_seed_rng(gs.data.game_start.rng_seed);
                                game_started = true;
                                in_lobby = false;
                        }
                    } else {
                        // Send ready to server
                        NetMessage msg = {0};
                        msg.type = NET_MSG_PLAYER_READY;
                        msg.data.player_ready.player_index = net->local_player;
                        msg.data.player_ready.is_ready = net->player_ready[net->local_player];
                        net_send_to(net->server_peer, &msg);
                    }
                }
            }
        }

        // Poll network
        NetMessage net_msg;
        ENetPeer* from;
        while (net_poll(net, &net_msg, &from) > 0) {
            switch (net_msg.type) {
                case NET_MSG_SERVER_INFO: {
                    // Client received server info
                    net->local_player = net_msg.data.server_info.assigned_index;
                    net->player_count = net_msg.data.server_info.player_count;
                    memcpy(net->player_names, net_msg.data.server_info.player_names, sizeof(net->player_names));
                    // Set our name in the slot
                    snprintf(net->player_names[net->local_player], NET_PLAYER_NAME_LEN, "%s", net_name);
                    // Tell server our name
                    NetMessage join = {0};
                    join.type = NET_MSG_PLAYER_JOIN;
                    join.data.player_join.player_index = net->local_player;
                    snprintf(join.data.player_join.player_name, NET_PLAYER_NAME_LEN, "%s", net_name);
                    net_send_to(net->server_peer, &join);
                    break;
                }
                case NET_MSG_PLAYER_JOIN: {
                    int pi = net_msg.data.player_join.player_index;
                    if (pi >= 0 && pi < NET_MAX_PLAYERS) {
                        snprintf(net->player_names[pi], NET_PLAYER_NAME_LEN, "%s", net_msg.data.player_join.player_name);
                        if (net->is_server) {
                            // Re-broadcast to all clients so everyone knows the name
                            net_broadcast(net, &net_msg);
                        } else {
                            // Recount players from names
                            int cnt = 0;
                            for (int s = 0; s < NET_MAX_PLAYERS; s++)
                                if (net->player_names[s][0]) cnt++;
                            net->player_count = cnt;
                        }
                    }
                    break;
                }
                case NET_MSG_PLAYER_LEAVE: {
                    int pi = net_msg.data.player_leave.player_index;
                    if (pi >= 0 && pi < NET_MAX_PLAYERS) {
                        net->player_names[pi][0] = '\0';
                        net->player_ready[pi] = false;
                        if (!net->is_server) {
                            int cnt = 0;
                            for (int s = 0; s < NET_MAX_PLAYERS; s++)
                                if (net->player_names[s][0]) cnt++;
                            net->player_count = cnt;
                        }
                    }
                    if (!net->is_server && pi == -1) {
                        // Server disconnected
                        net_disconnect(net);
                        net_shutdown();
                        return;
                    }
                    break;
                }
                case NET_MSG_PLAYER_READY:
                    if (net->is_server) {
                        int pi = net_msg.data.player_ready.player_index;
                        if (pi >= 0 && pi < NET_MAX_PLAYERS) {
                            net->player_ready[pi] = net_msg.data.player_ready.is_ready;
                            // Broadcast ready update
                            NetMessage ru = {0};
                            ru.type = NET_MSG_READY_UPDATE;
                            memcpy(ru.data.ready_update.ready, net->player_ready, sizeof(ru.data.ready_update.ready));
                            net_broadcast(net, &ru);
                            // Check if all ready
                            if (net->player_count >= 2 && net_all_players_check(net, net->player_ready)) {
                                app->options.players = (uint8_t)net->player_count;
                                if (app->selected_level_count == 0 && app->level_count > 0) {
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
                                NetMessage gs = {0};
                                gs.type = NET_MSG_GAME_START;
                                pack_options_to_game_start(&app->options, &gs.data.game_start);
                                gs.data.game_start.level_count = app->selected_level_count;
                                gs.data.game_start.rng_seed = (uint32_t)SDL_GetTicks() ^ 0xDEADBEEF;
                                if (app->selected_level_count > 0)
                                    memcpy(gs.data.game_start.selected_levels, app->selected_levels,
                                           app->selected_level_count * sizeof(int));
                                net_broadcast(net, &gs);
                                game_seed_rng(gs.data.game_start.rng_seed);
                                game_started = true;
                                in_lobby = false;
                            }
                        }
                    }
                    break;
                case NET_MSG_READY_UPDATE:
                    memcpy(net->player_ready, net_msg.data.ready_update.ready, sizeof(net->player_ready));
                    break;
                case NET_MSG_GAME_START: {
                    unpack_game_start_to_options(&net_msg.data.game_start, &app->options);
                    app->selected_level_count = net_msg.data.game_start.level_count;
                    if (app->selected_level_count > 0)
                        memcpy(app->selected_levels, net_msg.data.game_start.selected_levels,
                               app->selected_level_count * sizeof(int));
                    game_seed_rng(net_msg.data.game_start.rng_seed);
                    game_started = true;
                    in_lobby = false;
                    break;
                }
                default:
                    break;
            }
        }

        SDL_Delay(16);
    }

    if (!game_started) {
        net_disconnect(net);
        net_shutdown();
        return;
    }

    // Initialize game state for net play — force multiplayer mode
    app->options.players = (uint8_t)net->player_count;
    app->current_round = 0;
    for (int p = 0; p < MAX_PLAYERS; ++p) {
        app->player_cash[p] = app->options.cash;
        app->player_rounds_won[p] = 0;
        memset(app->player_inventory[p], 0, sizeof(app->player_inventory[p]));
    }
    // Copy net player names into app player slots
    for (int p = 0; p < NET_MAX_PLAYERS; p++) {
        if (net->player_names[p][0])
            snprintf(app->player_name[p], sizeof(app->player_name[p]), "%s", net->player_names[p]);
    }

    // Run net shop + gameplay rounds
    bool disconnected = false;
    bool show_victory = true;
    for (int round = 0; round < app->options.rounds && !disconnected; round++) {
        app->current_round = round;
        int shop_result = app_run_net_shop(app, ctx);
        if (shop_result == 0) { disconnected = true; break; }   // disconnect
        if (shop_result == -1) break;                            // F10 quit — show victory
        if (!net->connected) { disconnected = true; break; }

        // Determine level
        if (app->level_count > 0) {
            int lvl_idx;
            if (app->selected_level_count > 0 && round < app->selected_level_count) {
                int sel = app->selected_levels[round];
                if (sel == 0) lvl_idx = round % app->level_count;
                else lvl_idx = sel - 1;
            } else {
                lvl_idx = round % app->level_count;
            }

            context_animate(ctx, ANIMATION_FADE_DOWN, 7);
            RoundResult result = game_run(app, ctx, app->level_data[lvl_idx], net);

            app_process_round_result(app, &result, app->options.players, false);

            context_linger_music_start(ctx);
            context_animate(ctx, ANIMATION_FADE_DOWN, 7);
            context_linger_music_end(ctx);

            if (result.end_type == ROUND_END_FINAL) break;  // F10 during game — show victory
            if (!net->connected) { disconnected = true; break; }
        }
    }

    // Victory screen (unless disconnected)
    if (!disconnected && show_victory) {
        context_stop_music(ctx);
        app_run_victory_screen(app, ctx);
    }

    app->selected_level_count = saved_level_count;
    net_disconnect(net);
    net_shutdown();
}
#endif /* MB_NET */

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
                for (int p = 0; p < MAX_PLAYERS; ++p) {
                    app->player_cash[p] = app->options.cash;
                    app->player_rounds_won[p] = 0;
                    memset(app->player_inventory[p], 0, sizeof(app->player_inventory[p]));
                }
                // Default: random shuffled levels when none manually selected
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
                if (auto_levels) app->selected_level_count = 0;
            }
        }
        else if (selected == MENU_OPTIONS) app_run_options(app, ctx);
        else if (selected == MENU_INFO) app_run_info_menu(app, ctx);
    }
    context_stop_music(ctx);
}
