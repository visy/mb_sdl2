#include "playersel.h"
#include "editor.h"
#include "persist.h"
#include "fonts.h"
#include "glyphs.h"
#include "input.h"
#include "context.h"
#include <stdio.h>
#include <string.h>

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

// Fully redraw the player select screen background (texture + erase unused panels + right pane + names)
static void ps_full_redraw(App* app, ApplicationContext* ctx, int num_players) {
    SDL_SetRenderTarget(ctx->renderer, ctx->buffer);
    SDL_RenderCopy(ctx->renderer, app->select_players.texture, NULL, NULL);
    SDL_SetRenderDrawColor(ctx->renderer, 0, 0, 0, 255);
    for (int p = num_players; p < 4; p++) {
        SDL_Rect r = {39, p * 53 + 18, 293, 53};
        SDL_RenderFillRect(ctx->renderer, &r);
    }
    SDL_SetRenderTarget(ctx->renderer, NULL);
    ps_render_right_pane(app, ctx);
    ps_render_left_names(app, ctx, num_players);
}

// Edit/create a player name at roster index. Returns true if name was entered.
static bool ps_edit_name(App* app, ApplicationContext* ctx, int roster_idx, int num_players) {
    char name[ROSTER_NAME_MAX] = "";
    if (!text_entry_dialog(app, ctx, name, ROSTER_NAME_MAX, "ENTER PLAYER NAME:", NULL, 0))
        return false;
    if (name[0] == '\0') return false;
    RosterInfo* entry = &app->roster.entries[roster_idx];
    memset(entry, 0, sizeof(RosterInfo));
    entry->active = true;
    snprintf(entry->name, ROSTER_NAME_MAX, "%s", name);
    ps_full_redraw(app, ctx, num_players);
    return true;
}

// Name select sub-menu: browse 32 roster slots, select or create
// A / Enter on existing = select it. A / Enter on empty = create new name.
// B / Esc = cancel. X / Delete = delete entry.
// Returns roster index selected, or -1 if cancelled
static int ps_name_select(App* app, ApplicationContext* ctx, int num_players) {
    int arrow = 0;
    ps_render_arrow(app, ctx, arrow);
    ps_render_stats(app, ctx, app->roster.entries[arrow].active ? &app->roster.entries[arrow] : NULL);
    context_present(ctx);

    int result = -1;
    bool running = true;

    #define NS_MOVE_UP()   arrow = (arrow + ROSTER_MAX - 1) % ROSTER_MAX
    #define NS_MOVE_DOWN() arrow = (arrow + 1) % ROSTER_MAX
    #define NS_DELETE() do { \
        app->roster.entries[arrow].active = false; \
        for (int i = 0; i < MAX_PLAYERS; i++) \
            if (app->roster.identities[i] == arrow) app->roster.identities[i] = -1; \
        ps_render_right_pane(app, ctx); \
        ps_render_stats(app, ctx, NULL); \
        context_present(ctx); } while(0)
    #define NS_SELECT_OR_EDIT() do { \
        if (app->roster.entries[arrow].active) { \
            result = arrow; running = false; \
        } else { \
            ps_edit_name(app, ctx, arrow, num_players); \
            if (app->roster.entries[arrow].active) { result = arrow; running = false; } \
            ps_render_arrow(app, ctx, arrow); \
            ps_render_stats(app, ctx, app->roster.entries[arrow].active ? &app->roster.entries[arrow] : NULL); \
            context_present(ctx); \
        } } while(0)

    while (running) {
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_QUIT) { running = false; break; }

            int prev = arrow;
            bool nav = false;

            // Use input_map_event for player 0 so pad bindings are respected
            ActionType act = input_map_event(&e, 0, &app->input_config);
            if (act == ACT_UP) { NS_MOVE_UP(); nav = true; }
            else if (act == ACT_DOWN) { NS_MOVE_DOWN(); nav = true; }
            else if (act == ACT_ACTION) { NS_SELECT_OR_EDIT(); }
            else if (act == ACT_STOP || act == ACT_PAUSE) { running = false; }

            // Keyboard extras (LEFT to select existing, DELETE to remove)
            if (e.type == SDL_KEYDOWN && !e.key.repeat) {
                if (e.key.keysym.scancode == SDL_SCANCODE_LEFT) {
                    if (app->roster.entries[arrow].active) result = arrow;
                    running = false;
                }
                else if (e.key.keysym.scancode == SDL_SCANCODE_BACKSPACE || e.key.keysym.scancode == SDL_SCANCODE_DELETE)
                    NS_DELETE();
                else if (e.key.keysym.scancode == SDL_SCANCODE_RETURN || e.key.keysym.scancode == SDL_SCANCODE_KP_ENTER)
                    NS_SELECT_OR_EDIT();
            }
            // Gamepad extras: X = delete
            if (e.type == SDL_CONTROLLERBUTTONDOWN && e.cbutton.button == SDL_CONTROLLER_BUTTON_X)
                NS_DELETE();

            if (e.type == SDL_TEXTINPUT) {
                ps_edit_name(app, ctx, arrow, num_players);
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
    #undef NS_MOVE_UP
    #undef NS_MOVE_DOWN
    #undef NS_DELETE
    #undef NS_SELECT_OR_EDIT
    ps_clear_arrow(app, ctx, arrow);
    context_present(ctx);
    return result;
}

// Main player selection screen. Returns true if all players selected, false if cancelled.
bool app_run_player_select(App* app, ApplicationContext* ctx) {
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

    while (running) {
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_QUIT) { running = false; break; }

            int prev = active;

            // Use input_map_event for player 0 so gamepad bindings work
            ActionType act = input_map_event(&e, 0, &app->input_config);
            if (act == ACT_DOWN) {
                active++;
                if (active > 4) active = 0;
                else if (active != 4 && active >= num_players) active = 4;
            }
            else if (act == ACT_UP) {
                if (active == 0) active = 4;
                else { active--; if (active >= num_players) active = num_players - 1; }
            }
            else if (act == ACT_ACTION || act == ACT_RIGHT ||
                     (e.type == SDL_KEYDOWN && (e.key.keysym.scancode == SDL_SCANCODE_RETURN ||
                      e.key.keysym.scancode == SDL_SCANCODE_KP_ENTER))) {
                if (active == 4) {
                    bool all_ok = true;
                    for (int i = 0; i < num_players; i++)
                        if (app->roster.identities[i] < 0) { all_ok = false; break; }
                    if (all_ok) { result = true; running = false; }
                } else {
                    int sel = ps_name_select(app, ctx, num_players);
                    if (sel >= 0) app->roster.identities[active] = (int8_t)sel;
                    // Full redraw after returning from sub-menu
                    ps_full_redraw(app, ctx, num_players);
                    ps_render_shovel(app, ctx, active, active);
                    const RosterInfo* st = NULL;
                    if (app->roster.identities[active] >= 0)
                        st = &app->roster.entries[app->roster.identities[active]];
                    ps_render_stats(app, ctx, st);
                }
            }
            else if (act == ACT_STOP || act == ACT_PAUSE) { running = false; }

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
                ps_edit_name(app, ctx, empty, num_players);
                if (app->roster.entries[empty].active)
                    app->roster.identities[active] = (int8_t)empty;
                ps_full_redraw(app, ctx, num_players);
                ps_render_shovel(app, ctx, active, active);
                context_present(ctx);
            }
        }
        SDL_Delay(16);
    }

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
