#include "shop.h"
#include "cpu.h"
#include "persist.h"
#include "fonts.h"
#include "glyphs.h"
#include "input.h"
#include "context.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

const uint32_t EQUIPMENT_PRICES[] = {
    1, 3, 10, 650, 15, 65, 300, 25, 500, 80, 90, 35, 145, 15, 80, 120, 50, 400, 1100, 1600, 70, 400, 50, 80, 800, 95, 575
};

// Single source of truth for end-of-round money distribution.
// Handles campaign (1 player, never lose money) and multiplayer (any player count).
void app_process_round_result(App* app, const RoundResult* result, int nplayers, bool campaign) {
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

    // Accumulate per-round stats (skip quit rounds — no meaningful outcome)
    if (result->end_type != ROUND_END_FINAL && result->end_type != ROUND_END_QUIT) {
        for (int p = 0; p < nplayers; p++) {
            app->game_stats[p].rounds++;
            if (result->player_survived[p]) app->game_stats[p].rounds_wins++;
            app->game_stats[p].treasures_collected += result->treasures_collected[p];
            app->game_stats[p].total_money += result->player_cash_earned[p];
            app->game_stats[p].bombs_dropped += result->bombs_dropped[p];
            app->game_stats[p].deaths += result->deaths[p];
            app->game_stats[p].meters_ran += result->meters_ran[p];
        }
    }
}

static uint32_t shop_adjusted_price(App* app, int item) {
    uint32_t base = EQUIPMENT_PRICES[item];
    if (app->options.free_market) {
        uint32_t pct = 130 - (uint32_t)(rand() % 60); // 70-130%
        return ((base - 1) * pct + 50) / 100 + 1;
    }
    return base;
}

bool shop_try_buy(App* app, int player, int item) {
    if (item < 0 || item >= EQUIP_TOTAL) return false;
    uint32_t price = shop_adjusted_price(app, item);
    if (app->player_cash[player] >= price) {
        app->player_cash[player] -= price;
        app->player_inventory[player][item]++;
        app->game_stats[player].bombs_bought++;
        return true;
    }
    return false;
}

bool shop_try_sell(App* app, int player, int item) {
    if (item < 0 || item >= EQUIP_TOTAL) return false;
    if (app->player_inventory[player][item] > 0) {
        app->player_inventory[player][item]--;
        app->player_cash[player] += (7 * EQUIPMENT_PRICES[item] + 5) / 10;
        return true;
    }
    return false;
}

void render_shop_ui(App* app, ApplicationContext* ctx, int selected_item[], int shop_players[], int num_panels) {
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

PlayerResult compute_score(App* app, int player_idx, int nplayers) {
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

void app_run_victory_screen(App* app, ApplicationContext* ctx) {
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

void app_run_shop(App* app, ApplicationContext* ctx) {
    int nplayers = app->options.players;
    bool running = true;
    while (running) {
        context_play_music_at(ctx, "OEKU.S3M", 83);

        // Shop in pairs: pair 1 shops while pair 2 watches, then swap.
        // All players always see the active pair's shop.
        int selected[MAX_PLAYERS] = {0};
        bool quit = false;

        for (int batch_start = 0; batch_start < nplayers && !quit; batch_start += 2) {
            int batch[2]; int num_panels = 0;
            for (int i = batch_start; i < nplayers && i < batch_start + 2; i++)
                batch[num_panels++] = i;
            // Swap so lower index is on right (matching original)
            if (num_panels == 2) { int tmp = batch[0]; batch[0] = batch[1]; batch[1] = tmp; }

            bool ready[2] = {false, false};
            if (num_panels < 2) ready[1] = true;
            bool batch_done = false;

            while (!batch_done) {
                int disp_sel[2] = {0, 0};
                for (int i = 0; i < num_panels; i++) disp_sel[i] = selected[batch[i]];
                render_shop_ui(app, ctx, disp_sel, batch, num_panels);

                context_present(ctx);

                SDL_Event e;
                while (SDL_PollEvent(&e)) {
                    app_handle_hotplug(app, ctx, &e);
                    if (e.type == SDL_QUIT) { quit = true; batch_done = true; break; }
                    if (is_pause_event(&e, &app->input_config)) {
                        PauseChoice pc = pause_menu(app, ctx, PAUSE_CTX_SHOP);
                        if (pc == PAUSE_END_GAME) { quit = true; batch_done = true; }
                        continue;
                    }
                    // Only active batch players can control the shop (skip CPU)
                    for (int panel = 0; panel < num_panels; panel++) {
                        int pi = batch[panel];
                        if (is_cpu_player(app, pi)) continue;
                        ActionType act = input_map_event(&e, pi, &app->input_config);
                        if (ready[panel]) continue;
                        if (act != ACT_MAX_PLAYER) {
                            switch (act) {
                                case ACT_UP:    if (selected[pi] >= 4) selected[pi] -= 4; break;
                                case ACT_DOWN:  if (selected[pi] + 4 <= EQUIP_TOTAL) selected[pi] += 4; else selected[pi] = EQUIP_TOTAL; break;
                                case ACT_LEFT:  selected[pi] = (selected[pi] + EQUIP_TOTAL) % (EQUIP_TOTAL + 1); break;
                                case ACT_RIGHT: selected[pi] = (selected[pi] + 1) % (EQUIP_TOTAL + 1); break;
                                case ACT_ACTION:
                                    if (selected[pi] == EQUIP_TOTAL) ready[panel] = true;
                                    else shop_try_buy(app, pi, selected[pi]);
                                    break;
                                case ACT_STOP:
                                    if (selected[pi] != EQUIP_TOTAL) shop_try_sell(app, pi, selected[pi]);
                                    break;
                                default: break;
                            }
                        }
                    }
                }
                // CPU players shop autonomously (one action per frame for visual)
                for (int panel = 0; panel < num_panels; panel++) {
                    int pi = batch[panel];
                    if (is_cpu_player(app, pi) && !ready[panel])
                        cpu_shop_tick(app, pi, &selected[pi], &ready[panel]);
                }
                if (ready[0] && ready[1]) batch_done = true;
                SDL_Delay(16);
            }
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
