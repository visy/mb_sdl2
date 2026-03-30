#include "campaign.h"
#include "game.h"
#include "shop.h"
#include "persist.h"
#include "fonts.h"
#include "input.h"
#include "context.h"
#include <stdio.h>
#include <string.h>

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

    // Update and save high scores
    highscores_insert(app->highscores, app->player_name[0], app->current_round, app->player_cash[0]);
    highscores_save(app->highscores, ctx->game_dir);

    // Hall of fame — display all high scores
    if (app->halloffa.texture) {
        SDL_SetRenderTarget(ctx->renderer, ctx->buffer);
        SDL_RenderCopy(ctx->renderer, app->halloffa.texture, NULL, NULL);
        SDL_Color color = app->halloffa.palette[1];
        for (int i = 0; i < HIGHSCORE_MAX; i++) {
            if (app->highscores[i].name[0] == '\0') break;
            char score_str[64];
            snprintf(score_str, sizeof(score_str), "%-2d   %-20s Level %-2d Money %u",
                     i + 1, app->highscores[i].name, app->highscores[i].level, app->highscores[i].money);
            render_text(ctx->renderer, &app->font, 127, 179 + i * 14, color, score_str);
        }
        SDL_SetRenderTarget(ctx->renderer, NULL);
        context_animate(ctx, ANIMATION_FADE_UP, 7);
        context_wait_key_pressed(ctx);
        context_animate(ctx, ANIMATION_FADE_DOWN, 7);
    }
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

void app_run_campaign(App* app, ApplicationContext* ctx) {
    if (app->campaign_level_count == 0) return;
    validate_campaign_levels(app);

    app->current_round = 0;
    app->player_lives = 3;
    app->player_cash[0] = 250;
    app->player_rounds_won[0] = 0;
    memset(app->player_inventory[0], 0, sizeof(app->player_inventory[0]));
    memset(&app->game_stats[0], 0, sizeof(app->game_stats[0]));

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
    // Update roster stats for campaign player
    int8_t ri = app->roster.identities[0];
    if (ri >= 0 && app->roster.entries[ri].active)
        roster_update_stats(&app->roster.entries[ri], &app->game_stats[0], win);
    roster_save(&app->roster, ctx->game_dir);

    app_run_campaign_end(app, ctx, win);
}
