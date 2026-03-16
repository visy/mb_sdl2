#include "game.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

static const int SMALL_BOMB_PATTERN[][2] = {
  {-1, 0}, {1, 0}, {0, -1}, {0, 1}
};

static const int BIG_BOMB_PATTERN[][2] = {
  {-1, 0}, {1, 0}, {0, -1}, {0, 1},
  {-2, 0}, {-1, 1}, {0, 2}, {1, 1},
  {2, 0}, {1, -1}, {0, -2}, {-1, -1}
};

static const int DYNAMITE_PATTERN[][2] = {
  {-1, 0}, {1, 0}, {0, -1}, {0, 1},
  {-2, 0}, {-1, 1}, {0, 2}, {1, 1},
  {2, 0}, {1, -1}, {0, -2}, {-1, -1},
  {-3, 0}, {-3, 1}, {-2, 1}, {-2, 2},
  {-1, 2}, {-1, 3}, {0, 3}, {1, 3},
  {1, 2}, {2, 2}, {2, 1}, {3, 1},
  {3, 0}, {3, -1}, {2, -1}, {2, -2},
  {1, -2}, {1, -3}, {0, -3}, {-1, -3},
  {-1, -2}, {-2, -2}, {-2, -1}, {-3, -1}
};

bool is_passable(uint8_t val) {
    if (val == TILE_PASSAGE || val == TILE_TELEPORT || val == TILE_BLOOD || val == TILE_EXPLOSION || val == TILE_SMOKE1 || val == TILE_SMOKE2 || (val >= 0x3A && val <= 0x40) || val == 0xAF) return true;
    return false;
}

static bool is_treasure(uint8_t val) {
    if (val == TILE_DIAMOND) return true; 
    if (val >= TILE_GOLD_SHIELD && val <= TILE_GOLD_CROWN) return true; 
    if (val == TILE_MEDIKIT || val == TILE_WEAPONS_CRATE || val == 0xB3) return true;
    return false;
}

static int get_treasure_value(uint8_t val) {
    if (val == TILE_GOLD_SHIELD) return 15;
    if (val == TILE_GOLD_EGG) return 25;
    if (val == TILE_GOLD_PILE) return 15;
    if (val == TILE_GOLD_BRACELET) return 10;
    if (val == TILE_GOLD_BAR) return 30;
    if (val == TILE_GOLD_CROSS) return 35;
    if (val == TILE_GOLD_SCEPTER) return 50;
    if (val == TILE_GOLD_RUBIN) return 65;
    if (val == TILE_GOLD_CROWN) return 100;
    if (val == TILE_DIAMOND) return 1000;
    return 0;
}

static bool is_sand(uint8_t val) {
    return (val >= TILE_SAND1 && val <= TILE_SAND3);
}

bool is_stone(uint8_t val) {
    return (val >= TILE_STONE1 && val <= TILE_STONE4) || (val >= TILE_STONE_TOP_LEFT && val <= TILE_STONE_BOTTOM_RIGHT) || val == TILE_STONE_BOTTOM_LEFT;
}

static const uint8_t SEE_THROUGH_BITMAP[32] = {
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00,
    0x00, 0x00, 0x80, 0x03, 0xF8, 0x0F, 0x88, 0xF1,
    0x0F, 0xFC, 0xFF, 0xF7, 0xEF, 0x8F, 0x30, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
};

static bool see_through(uint8_t val) {
    return (SEE_THROUGH_BITMAP[val / 8] & (1 << (val & 7))) != 0;
}

static void reveal_view(World* world, int px, int py, Direction facing) {
    for (int offset = -20; offset <= 20; offset++) {
        int abs_offset = offset < 0 ? -offset : offset;
        int slope_error = 2 * abs_offset - 20;
        int cx = px, cy = py;

        for (int step = 0; step <= 20; step++) {
            if (cx < 0 || cx >= MAP_WIDTH || cy < 0 || cy >= MAP_HEIGHT) break;
            world->fog[cy][cx] = false;
            if (!see_through(world->tiles[cy][cx])) break;

            if (slope_error > 0) {
                switch (facing) {
                    case DIR_RIGHT: case DIR_LEFT: cy += (offset < 0) ? -1 : 1; break;
                    case DIR_UP: case DIR_DOWN:    cx += (offset < 0) ? -1 : 1; break;
                }
                slope_error -= 2 * 20;
            }
            slope_error += 2 * abs_offset;

            switch (facing) {
                case DIR_RIGHT: cx++; break;
                case DIR_LEFT:  cx--; break;
                case DIR_UP:    cy--; break;
                case DIR_DOWN:  cy++; break;
            }
        }
    }

    // Also reveal along facing direction while passable (corridor reveal)
    int cx = px, cy = py;
    while (cx >= 0 && cx < MAP_WIDTH && cy >= 0 && cy < MAP_HEIGHT && is_passable(world->tiles[cy][cx])) {
        for (int d = 0; d < 4; d++) {
            int nx = cx + (d == 0 ? 1 : d == 1 ? -1 : 0);
            int ny = cy + (d == 2 ? -1 : d == 3 ? 1 : 0);
            if (nx >= 0 && nx < MAP_WIDTH && ny >= 0 && ny < MAP_HEIGHT)
                world->fog[ny][nx] = false;
        }
        switch (facing) {
            case DIR_RIGHT: cx++; break;
            case DIR_LEFT:  cx--; break;
            case DIR_UP:    cy--; break;
            case DIR_DOWN:  cy++; break;
        }
    }
}

static void apply_explosion_damage(World* world, int cx, int cy, int dmg) {
    for (int p = 0; p < world->num_players; ++p) {
        Actor* actor = &world->actors[p];
        if (p == 0 && world->god_mode) continue;
        if (actor->is_dead) continue;

        int px = actor->pos.x / 10;
        int py = (actor->pos.y - 30) / 10;

        if (px == cx && py == cy) {
            int effective_dmg = dmg * world->bomb_damage / 100;
            actor->health -= effective_dmg;
            if (actor->health <= 0) {
                actor->health = 0;
                actor->is_dead = true;
            }
        }
    }
}

static void explode_cell_ex(World* world, int cx, int cy, int dmg, bool heavy) {
    if (cx < 0 || cx >= MAP_WIDTH || cy < 0 || cy >= MAP_HEIGHT) return;

    apply_explosion_damage(world, cx, cy, dmg);

    uint8_t val = world->tiles[cy][cx];
    
    if (cx > 0 && !is_passable(world->tiles[cy][cx-1])) world->burned[cy][cx] |= BURNED_L;
    if (cx < MAP_WIDTH - 1 && !is_passable(world->tiles[cy][cx+1])) world->burned[cy][cx] |= BURNED_R;
    if (cy > 0 && !is_passable(world->tiles[cy-1][cx])) world->burned[cy][cx] |= BURNED_U;
    if (cy < MAP_HEIGHT - 1 && !is_passable(world->tiles[cy+1][cx])) world->burned[cy][cx] |= BURNED_D;

    if (val == TILE_SMALL_BOMB1 || val == TILE_SMALL_BOMB2 || val == TILE_SMALL_BOMB3 ||
        val == TILE_BIG_BOMB1 || val == TILE_BIG_BOMB2 || val == TILE_BIG_BOMB3 ||
        val == TILE_DYNAMITE1 || val == TILE_DYNAMITE2 || val == TILE_DYNAMITE3 ||
        val == TILE_ATOMIC1 || val == TILE_ATOMIC2 || val == TILE_ATOMIC3) {
        if (world->timer[cy][cx] > 0) {
            world->timer[cy][cx] = 1;
            return;
        } else {
            world->tiles[cy][cx] = TILE_EXPLOSION;
            world->timer[cy][cx] = 3;
        }
    } else if (is_stone(val) || val == TILE_BOULDER) {
        if (heavy) {
            world->tiles[cy][cx] = TILE_EXPLOSION;
            world->timer[cy][cx] = 3;
        } else if (rand() % 2 == 0) {
            world->tiles[cy][cx] = TILE_STONE_CRACKED_HEAVY;
            world->hits[cy][cx] = 500;
        } else {
            world->tiles[cy][cx] = TILE_STONE_CRACKED_LIGHT;
            world->hits[cy][cx] = 1000;
        }
    } else if (val != TILE_WALL) {
        world->tiles[cy][cx] = TILE_EXPLOSION;
        world->timer[cy][cx] = 3;
    }
}

static void explode_cell(World* world, int cx, int cy, int dmg) {
    explode_cell_ex(world, cx, cy, dmg, false);
}

static void explode_nuke(World* world, int cx, int cy) {
    explode_cell_ex(world, cx, cy, 255, true);
    for (int dc = -12; dc <= 12; ++dc) {
        int cathet = (int)ceil(sqrt(144.0 - (double)(dc * dc)));
        for (int dr = -cathet; dr <= cathet; ++dr) {
            explode_cell_ex(world, cx + dc, cy + dr, 255, true);
        }
    }
}

static void explode_pattern(World* world, int cx, int cy, int dmg, const int pattern[][2], int pattern_size) {
    explode_cell(world, cx, cy, dmg);
    for (int i = 0; i < pattern_size; ++i) {
        explode_cell(world, cx + pattern[i][1], cy + pattern[i][0], dmg);
    }
}

static int get_initial_hits(uint8_t val) {
    if (val == TILE_WALL) return 30000;
    if (is_sand(val)) return 24;
    if (val == TILE_GRAVEL_LIGHT) return 108;
    if (val == TILE_GRAVEL_HEAVY) return 347;
    if (val >= TILE_STONE_TOP_LEFT && val <= TILE_STONE_BOTTOM_RIGHT) return 1227;
    if (val == TILE_STONE_BOTTOM_LEFT) return 1227;
    if (is_stone(val)) return 2000;
    if (val == TILE_STONE_CRACKED_LIGHT) return 1000;
    if (val == TILE_STONE_CRACKED_HEAVY) return 500;
    if (val == TILE_BOULDER) return 24;
    return 0;
}

void game_init_world(World* world, uint8_t* level_data, int num_players) {
    memset(world, 0, sizeof(World));
    world->num_players = num_players;
    for (int y = 0; y < MAP_HEIGHT; ++y) {
        uint8_t* row_ptr = &level_data[y * 66];
        for (int x = 0; x < MAP_WIDTH; ++x) {
            uint8_t val = row_ptr[x];
            if (is_stone(val) && y > 0 && y < MAP_HEIGHT - 1 && x > 0 && x < MAP_WIDTH - 1) {
                if (level_data[y * 66 + x + 1] == TILE_PASSAGE && level_data[y * 66 + x - 1] == TILE_PASSAGE &&
                    level_data[(y - 1) * 66 + x] == TILE_PASSAGE && level_data[(y + 1) * 66 + x] == TILE_PASSAGE) {
                    val = TILE_BOULDER;
                }
            }
            world->tiles[y][x] = val;
            world->hits[y][x] = get_initial_hits(val);
        }
    }

    int start_x[2] = {15, 625};
    int start_y[2] = {45, 45};

    for (int p = 0; p < num_players; ++p) {
        world->actors[p].pos.x = start_x[p];
        world->actors[p].pos.y = start_y[p];
        world->actors[p].health = 100;
        world->actors[p].max_health = 100;
        world->actors[p].drilling = 10;
        world->actors[p].facing = (p == 0) ? DIR_RIGHT : DIR_LEFT;
        world->actors[p].selected_weapon = EQUIP_SMALL_BOMB;
    }
    world->god_mode = true;
}

static void render_world(App* app, ApplicationContext* ctx, World* world) {
    SDL_SetRenderTarget(ctx->renderer, ctx->buffer);
    SDL_RenderCopy(ctx->renderer, app->players.texture, NULL, NULL);

    SDL_Color yellow = {255, 255, 0, 255}, white = {255, 255, 255, 255}, cyan = {0, 255, 255, 255};

    static const int PLAYER_X[] = {12, 174, 337, 500};
    static const int HEALTH_BAR_LEFT[] = {142, 304, 467, 630};

    for (int p = 0; p < world->num_players; ++p) {
        int pos_x = PLAYER_X[p];
        int wep = world->actors[p].selected_weapon;
        glyphs_render(&app->glyphs, ctx->renderer, pos_x, 0, (GlyphType)(GLYPH_EQUIPMENT_START + wep));
        
        char count_str[16];
        snprintf(count_str, sizeof(count_str), "%d", app->player_inventory[p][wep]);
        render_text(ctx->renderer, &app->font, pos_x + 18, 18, cyan, count_str);

        render_text(ctx->renderer, &app->font, pos_x + 36, 1, cyan, app->player_name[p]);

        char drill_str[16];
        snprintf(drill_str, sizeof(drill_str), "%d", world->actors[p].drilling);
        SDL_SetRenderDrawColor(ctx->renderer, 0, 0, 0, 255);
        SDL_Rect r_drill = {pos_x + 50, 11, 40, 8};
        SDL_RenderFillRect(ctx->renderer, &r_drill);
        render_text(ctx->renderer, &app->font, pos_x + 50, 11, cyan, drill_str);

        char cash_str[16];
        snprintf(cash_str, sizeof(cash_str), "%d", app->player_cash[p]);
        SDL_Rect r_cash = {pos_x + 50, 21, 40, 8};
        SDL_RenderFillRect(ctx->renderer, &r_cash);
        render_text(ctx->renderer, &app->font, pos_x + 50, 21, yellow, cash_str);

        int health_bars = world->actors[p].health <= 0 ? 0 : (world->actors[p].health * 50 + 1) / (2 * world->actors[p].max_health) + 1;
        if (health_bars > 26) health_bars = 26;
        int left = HEALTH_BAR_LEFT[p];
        SDL_SetRenderDrawColor(ctx->renderer, 0, 0, 0, 255);
        if (health_bars < 26) {
            SDL_Rect r_empty = {left, 2, 8, 26 - health_bars};
            SDL_RenderFillRect(ctx->renderer, &r_empty);
        }
        if (health_bars > 0) {
            SDL_Color hc = app->players.palette[2 + p];
            SDL_SetRenderDrawColor(ctx->renderer, hc.r, hc.g, hc.b, 255);
            SDL_Rect r_full = {left, 28 - health_bars, 8, health_bars};
            SDL_RenderFillRect(ctx->renderer, &r_full);
        }
    }

    if (world->god_mode) {
        render_text(ctx->renderer, &app->font, 12 + 70, 2, white, "GOD MODE ON");
    }

    if (world->darkness) {
        // In darkness mode: first render borders (wall tiles on edges), then black overlay, then revealed cells
        for (int x = 0; x < MAP_WIDTH; ++x) {
            glyphs_render(&app->glyphs, ctx->renderer, x * 10, 30, (GlyphType)(GLYPH_MAP_START + world->tiles[0][x]));
            glyphs_render(&app->glyphs, ctx->renderer, x * 10, (MAP_HEIGHT - 1) * 10 + 30, (GlyphType)(GLYPH_MAP_START + world->tiles[MAP_HEIGHT - 1][x]));
        }
        for (int y = 0; y < MAP_HEIGHT; ++y) {
            glyphs_render(&app->glyphs, ctx->renderer, 0, y * 10 + 30, (GlyphType)(GLYPH_MAP_START + world->tiles[y][0]));
            glyphs_render(&app->glyphs, ctx->renderer, (MAP_WIDTH - 1) * 10, y * 10 + 30, (GlyphType)(GLYPH_MAP_START + world->tiles[y][MAP_WIDTH - 1]));
        }

        SDL_SetRenderDrawColor(ctx->renderer, 0, 0, 0, 255);
        SDL_Rect dark_rect = {10, 40, 620, 430};
        SDL_RenderFillRect(ctx->renderer, &dark_rect);

        for (int y = 1; y < MAP_HEIGHT - 1; ++y) {
            for (int x = 1; x < MAP_WIDTH - 1; ++x) {
                if (!world->fog[y][x]) {
                    uint8_t val = world->tiles[y][x];
                    glyphs_render(&app->glyphs, ctx->renderer, x * 10, y * 10 + 30, (GlyphType)(GLYPH_MAP_START + val));
                }
            }
        }

        // Burned borders for revealed cells only
        for (int y = 1; y < MAP_HEIGHT - 1; ++y) {
            for (int x = 1; x < MAP_WIDTH - 1; ++x) {
                if (world->fog[y][x]) continue;
                uint8_t val = world->tiles[y][x];
                int px = x * 10;
                int py = y * 10 + 30;
                if (val == TILE_EXPLOSION || val == TILE_SMOKE1 || val == TILE_SMOKE2 || is_passable(val)) {
                    uint8_t b = (val == TILE_EXPLOSION) ? 0xF : world->burned[y][x];
                    if (b & BURNED_L && x > 0) { uint8_t n = world->tiles[y][x-1]; if (is_sand(n)||n==TILE_GRAVEL_LIGHT||n==TILE_GRAVEL_HEAVY) glyphs_render(&app->glyphs,ctx->renderer,px-4,py,GLYPH_BURNED_SAND_R); else if(is_stone(n)||n==TILE_STONE_CRACKED_LIGHT||n==TILE_STONE_CRACKED_HEAVY) glyphs_render(&app->glyphs,ctx->renderer,px-4,py,GLYPH_BURNED_STONE_R); }
                    if (b & BURNED_R && x < MAP_WIDTH-1) { uint8_t n = world->tiles[y][x+1]; if (is_sand(n)||n==TILE_GRAVEL_LIGHT||n==TILE_GRAVEL_HEAVY) glyphs_render(&app->glyphs,ctx->renderer,px+10,py,GLYPH_BURNED_SAND_L); else if(is_stone(n)||n==TILE_STONE_CRACKED_LIGHT||n==TILE_STONE_CRACKED_HEAVY) glyphs_render(&app->glyphs,ctx->renderer,px+10,py,GLYPH_BURNED_STONE_L); }
                    if (b & BURNED_U && y > 0) { uint8_t n = world->tiles[y-1][x]; if (is_sand(n)||n==TILE_GRAVEL_LIGHT||n==TILE_GRAVEL_HEAVY) glyphs_render(&app->glyphs,ctx->renderer,px,py-3,GLYPH_BURNED_SAND_D); else if(is_stone(n)||n==TILE_STONE_CRACKED_LIGHT||n==TILE_STONE_CRACKED_HEAVY) glyphs_render(&app->glyphs,ctx->renderer,px,py-3,GLYPH_BURNED_STONE_D); }
                    if (b & BURNED_D && y < MAP_HEIGHT-1) { uint8_t n = world->tiles[y+1][x]; if (is_sand(n)||n==TILE_GRAVEL_LIGHT||n==TILE_GRAVEL_HEAVY) glyphs_render(&app->glyphs,ctx->renderer,px,py+10,GLYPH_BURNED_SAND_U); else if(is_stone(n)||n==TILE_STONE_CRACKED_LIGHT||n==TILE_STONE_CRACKED_HEAVY) glyphs_render(&app->glyphs,ctx->renderer,px,py+10,GLYPH_BURNED_STONE_U); }
                }
            }
        }

        // Only render players in revealed cells
        for (int p = 0; p < world->num_players; ++p) {
            Actor* actor = &world->actors[p];
            if (!actor->is_dead) {
                int acx = actor->pos.x / 10;
                int acy = (actor->pos.y - 30) / 10;
                if (acx >= 0 && acx < MAP_WIDTH && acy >= 0 && acy < MAP_HEIGHT && !world->fog[acy][acx]) {
                    int base = actor->is_digging ? GLYPH_PLAYER_DIG_START : GLYPH_PLAYER_START;
                    if (p == 1) base += 4000;
                    int anim_frame = 0;
                    if (actor->is_digging) { static const int pp[] = {0,1,2,3,2,1}; anim_frame = pp[actor->animation % 6]; }
                    else { anim_frame = actor->animation % 4; }
                    int p_glyph = base + (int)actor->facing + (anim_frame * 4);
                    glyphs_render(&app->glyphs, ctx->renderer, actor->pos.x - 5, actor->pos.y - 5, (GlyphType)p_glyph);
                }
            }
        }
    } else {
        // Normal (non-darkness) rendering
        for (int y = 0; y < MAP_HEIGHT; ++y) {
            for (int x = 0; x < MAP_WIDTH; ++x) {
                uint8_t val = world->tiles[y][x];
                glyphs_render(&app->glyphs, ctx->renderer, x * 10, y * 10 + 30, (GlyphType)(GLYPH_MAP_START + val));
            }
        }

        for (int y = 0; y < MAP_HEIGHT; ++y) {
            for (int x = 0; x < MAP_WIDTH; ++x) {
                uint8_t val = world->tiles[y][x];
                int px = x * 10;
                int py = y * 10 + 30;

                if (val == TILE_EXPLOSION || val == TILE_SMOKE1 || val == TILE_SMOKE2 || is_passable(val)) {
                    uint8_t b = (val == TILE_EXPLOSION) ? 0xF : world->burned[y][x];

                    if (b & BURNED_L && x > 0) {
                        uint8_t n = world->tiles[y][x-1];
                        if (is_sand(n) || n == TILE_GRAVEL_LIGHT || n == TILE_GRAVEL_HEAVY) glyphs_render(&app->glyphs, ctx->renderer, px - 4, py, GLYPH_BURNED_SAND_R);
                        else if (is_stone(n) || n == TILE_STONE_CRACKED_LIGHT || n == TILE_STONE_CRACKED_HEAVY) glyphs_render(&app->glyphs, ctx->renderer, px - 4, py, GLYPH_BURNED_STONE_R);
                    }
                    if (b & BURNED_R && x < MAP_WIDTH - 1) {
                        uint8_t n = world->tiles[y][x+1];
                        if (is_sand(n) || n == TILE_GRAVEL_LIGHT || n == TILE_GRAVEL_HEAVY) glyphs_render(&app->glyphs, ctx->renderer, px + 10, py, GLYPH_BURNED_SAND_L);
                        else if (is_stone(n) || n == TILE_STONE_CRACKED_LIGHT || n == TILE_STONE_CRACKED_HEAVY) glyphs_render(&app->glyphs, ctx->renderer, px + 10, py, GLYPH_BURNED_STONE_L);
                    }
                    if (b & BURNED_U && y > 0) {
                        uint8_t n = world->tiles[y-1][x];
                        if (is_sand(n) || n == TILE_GRAVEL_LIGHT || n == TILE_GRAVEL_HEAVY) glyphs_render(&app->glyphs, ctx->renderer, px, py - 3, GLYPH_BURNED_SAND_D);
                        else if (is_stone(n) || n == TILE_STONE_CRACKED_LIGHT || n == TILE_STONE_CRACKED_HEAVY) glyphs_render(&app->glyphs, ctx->renderer, px, py - 3, GLYPH_BURNED_STONE_D);
                    }
                    if (b & BURNED_D && y < MAP_HEIGHT - 1) {
                        uint8_t n = world->tiles[y+1][x];
                        if (is_sand(n) || n == TILE_GRAVEL_LIGHT || n == TILE_GRAVEL_HEAVY) glyphs_render(&app->glyphs, ctx->renderer, px, py + 10, GLYPH_BURNED_SAND_U);
                        else if (is_stone(n) || n == TILE_STONE_CRACKED_LIGHT || n == TILE_STONE_CRACKED_HEAVY) glyphs_render(&app->glyphs, ctx->renderer, px, py + 10, GLYPH_BURNED_STONE_U);
                    }
                }
            }
        }

        for (int p = 0; p < world->num_players; ++p) {
            Actor* actor = &world->actors[p];
            if (!actor->is_dead) {
                int base = actor->is_digging ? GLYPH_PLAYER_DIG_START : GLYPH_PLAYER_START;
                if (p == 1) base += 4000;
                int anim_frame = 0;
                if (actor->is_digging) {
                    static const int pp[] = {0, 1, 2, 3, 2, 1};
                    anim_frame = pp[actor->animation % 6];
                } else {
                    anim_frame = actor->animation % 4;
                }
                int p_glyph = base + (int)actor->facing + (anim_frame * 4);
                glyphs_render(&app->glyphs, ctx->renderer, actor->pos.x - 5, actor->pos.y - 5, (GlyphType)p_glyph);
            }
        }
    }

    SDL_SetRenderTarget(ctx->renderer, NULL);
    context_present(ctx);
}

RoundResult game_run(App* app, ApplicationContext* ctx, uint8_t* level_data) {
    int tracks[] = {0, 39, 55};
    context_play_music_at(ctx, "OEKU.S3M", tracks[rand() % 3]);
    
    World world;
    game_init_world(&world, level_data, 2);
    world.bomb_damage = app->options.bomb_damage;
    world.darkness = app->options.darkness;
    if (world.darkness) {
        memset(world.fog, true, sizeof(world.fog));
    }

    bool running = true;
    while (running) {
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_QUIT) { running = false; break; }
            
            for (int p = 0; p < world.num_players; ++p) {
                ActionType act = input_map_event(&e, p, &app->input_config);
                
                if (act == ACT_GOD && (e.type == SDL_KEYDOWN || e.type == SDL_CONTROLLERBUTTONDOWN)) {
                    world.god_mode = !world.god_mode;
                }

                if (act != ACT_MAX_PLAYER && act != ACT_GOD && !world.actors[p].is_dead) {
                    Actor* actor = &world.actors[p];
                    switch (act) {
                        case ACT_UP:    actor->facing = DIR_UP;    actor->moving = true; break;
                        case ACT_DOWN:  actor->facing = DIR_DOWN;  actor->moving = true; break;
                        case ACT_LEFT:  actor->facing = DIR_LEFT;  actor->moving = true; break;
                        case ACT_RIGHT: actor->facing = DIR_RIGHT; actor->moving = true; break;
                        case ACT_STOP:  actor->moving = false; break;
                        case ACT_ACTION: {
                            int cx = actor->pos.x / 10;
                            int cy = (actor->pos.y - 35) / 10;
                            if (cx >= 0 && cx < MAP_WIDTH && cy >= 0 && cy < MAP_HEIGHT) {
                                int w = actor->selected_weapon;
                                if (app->player_inventory[p][w] > 0 && (world.tiles[cy][cx] == TILE_PASSAGE || is_treasure(world.tiles[cy][cx]))) {
                                    if (w == EQUIP_SMALL_BOMB) { world.tiles[cy][cx] = TILE_SMALL_BOMB1; world.timer[cy][cx] = 90; app->player_inventory[p][w]--; }
                                    else if (w == EQUIP_BIG_BOMB) { world.tiles[cy][cx] = TILE_BIG_BOMB1; world.timer[cy][cx] = 90; app->player_inventory[p][w]--; }
                                    else if (w == EQUIP_DYNAMITE) { world.tiles[cy][cx] = TILE_DYNAMITE1; world.timer[cy][cx] = 60; app->player_inventory[p][w]--; }
                                    else if (w == EQUIP_ATOMIC_BOMB) { world.tiles[cy][cx] = TILE_ATOMIC1; world.timer[cy][cx] = 280; app->player_inventory[p][w]--; }
                                }
                            }
                        } break;
                        case ACT_CYCLE: {
                            int total_inv = 0;
                            for (int i=0; i<EQUIP_TOTAL; i++) total_inv += app->player_inventory[p][i];
                            if (total_inv > 0) {
                                for (int i = 1; i < EQUIP_TOTAL; ++i) {
                                    int w = (actor->selected_weapon + i) % EQUIP_TOTAL;
                                    if (app->player_inventory[p][w] > 0) {
                                        actor->selected_weapon = w;
                                        break;
                                    }
                                }
                            }
                        } break;
                        default: break;
                    }
                }
                
                if (e.type == SDL_CONTROLLERBUTTONDOWN && e.cbutton.button == SDL_CONTROLLER_BUTTON_BACK) running = false;
                if (e.type == SDL_KEYDOWN && e.key.keysym.scancode == SDL_SCANCODE_ESCAPE) running = false;
            }
        }

        int alive_count = 0;
        for (int p = 0; p < world.num_players; p++) {
            Actor* actor = &world.actors[p];
            if (actor->is_dead && actor->health == 0) {
                actor->health = -1;
                int cx = actor->pos.x / 10;
                int cy = (actor->pos.y - 30) / 10;
                if (cx >= 0 && cx < MAP_WIDTH && cy >= 0 && cy < MAP_HEIGHT) world.tiles[cy][cx] = TILE_BLOOD;
                context_play_sample(app->sound_aargh);
            }
            if (!actor->is_dead) alive_count++;
        }

        if (alive_count <= 1 && world.round_end_timer == 0) {
            world.round_end_timer = 120;
        }
        if (world.round_end_timer > 0) {
            world.round_end_timer--;
            if (world.round_end_timer == 0) running = false;
        }

        for (int p = 0; p < world.num_players; p++) {
            Actor* actor = &world.actors[p];
            actor->is_digging = false;
            if (actor->moving && !actor->is_dead) {
                int dx = actor->pos.x % 10;
                int dy = (actor->pos.y - 30) % 10;
                int cx = actor->pos.x / 10;
                int cy = (actor->pos.y - 30) / 10;

                int d_dir = 0, d_ortho = 0;
                bool finishing = false;

                switch(actor->facing) {
                    case DIR_LEFT:  d_dir = dx; d_ortho = dy; finishing = dx > 5; break;
                    case DIR_RIGHT: d_dir = dx; d_ortho = dy; finishing = dx < 5; break;
                    case DIR_UP:    d_dir = dy; d_ortho = dx; finishing = dy > 5; break;
                    case DIR_DOWN:  d_dir = dy; d_ortho = dx; finishing = dy < 5; break;
                    default: break;
                }

                if (d_ortho == 5 && (finishing || is_passable(world.tiles[cy + (actor->facing == DIR_DOWN ? 1 : (actor->facing == DIR_UP ? -1 : 0))][cx + (actor->facing == DIR_RIGHT ? 1 : (actor->facing == DIR_LEFT ? -1 : 0))]) || is_treasure(world.tiles[cy + (actor->facing == DIR_DOWN ? 1 : (actor->facing == DIR_UP ? -1 : 0))][cx + (actor->facing == DIR_RIGHT ? 1 : (actor->facing == DIR_LEFT ? -1 : 0))]) )) {
                    if (actor->facing == DIR_LEFT) actor->pos.x--;
                    else if (actor->facing == DIR_RIGHT) actor->pos.x++;
                    else if (actor->facing == DIR_UP) actor->pos.y--;
                    else if (actor->facing == DIR_DOWN) actor->pos.y++;
                    
                    int ncx = actor->pos.x / 10;
                    int ncy = (actor->pos.y - 30) / 10;
                    int c_dx = actor->pos.x % 10;
                    int c_dy = (actor->pos.y - 30) % 10;
                    if (c_dx != 5 || c_dy != 5) {
                        int tcx = ncx, tcy = ncy;
                        if (actor->facing == DIR_LEFT && c_dx < 5) tcx--;
                        else if (actor->facing == DIR_RIGHT && c_dx > 5) tcx++;
                        else if (actor->facing == DIR_UP && c_dy < 5) tcy--;
                        else if (actor->facing == DIR_DOWN && c_dy > 5) tcy++;
                        if (tcx >= 0 && tcx < MAP_WIDTH && tcy >= 0 && tcy < MAP_HEIGHT && is_treasure(world.tiles[tcy][tcx])) {
                            app->player_cash[p] += get_treasure_value(world.tiles[tcy][tcx]);
                            world.tiles[tcy][tcx] = TILE_PASSAGE;
                            context_play_sample_freq(app->sound_kili, 10000 + (rand()%5000));
                        }
                    }
                    if (is_treasure(world.tiles[ncy][ncx])) {
                        app->player_cash[p] += get_treasure_value(world.tiles[ncy][ncx]);
                        world.tiles[ncy][ncx] = TILE_PASSAGE;
                        context_play_sample_freq(app->sound_kili, 10000 + (rand()%5000));
                    }
                    // Teleporter: activate when player reaches center of a teleporter tile
                    if (c_dx == 5 && c_dy == 5 && world.tiles[ncy][ncx] == TILE_TELEPORT) {
                        int tele[256][2], nt = 0;
                        for (int ty = 0; ty < MAP_HEIGHT; ty++) for (int tx = 0; tx < MAP_WIDTH; tx++)
                            if (world.tiles[ty][tx] == TILE_TELEPORT && (tx != ncx || ty != ncy)) {
                                tele[nt][0] = tx; tele[nt][1] = ty; nt++;
                            }
                        if (nt > 0) {
                            int r = rand() % nt;
                            actor->pos.x = tele[r][0] * 10 + 5;
                            actor->pos.y = tele[r][1] * 10 + 35;
                            actor->moving = false;
                            context_play_sample(app->sound_kili);
                        }
                    }
                    actor->animation_timer++;
                    if (actor->animation_timer >= 4) {
                        actor->animation = (actor->animation + 1) % 4;
                        actor->animation_timer = 0;
                    }
                } else if (d_ortho == 5 && d_dir == 5) {
                    int ncx = cx, ncy = cy;
                    if (actor->facing == DIR_LEFT) ncx--;
                    else if (actor->facing == DIR_RIGHT) ncx++;
                    else if (actor->facing == DIR_UP) ncy--;
                    else if (actor->facing == DIR_DOWN) ncy++;

                    if (ncx >= 0 && ncx < MAP_WIDTH && ncy >= 0 && ncy < MAP_HEIGHT) {
                        uint8_t target = world.tiles[ncy][ncx];
                        if (is_sand(target) || is_stone(target) || target == TILE_STONE_CRACKED_LIGHT || target == TILE_STONE_CRACKED_HEAVY || target == TILE_GRAVEL_LIGHT || target == TILE_GRAVEL_HEAVY) {
                            actor->is_digging = true;
                            world.hits[ncy][ncx] -= actor->drilling;
                            if (is_stone(target)) {
                                if (world.hits[ncy][ncx] < 500) world.tiles[ncy][ncx] = TILE_STONE_CRACKED_HEAVY; 
                                else if (world.hits[ncy][ncx] < 1000) world.tiles[ncy][ncx] = TILE_STONE_CRACKED_LIGHT;
                            } else if (target == TILE_STONE_CRACKED_LIGHT) {
                                if (world.hits[ncy][ncx] < 500) world.tiles[ncy][ncx] = TILE_STONE_CRACKED_HEAVY; 
                            }
                            if (world.hits[ncy][ncx] <= 0) {
                                world.tiles[ncy][ncx] = TILE_PASSAGE;
                                actor->is_digging = false;
                            }
                            actor->animation_timer++;
                            if (actor->animation_timer >= 8) {
                                actor->animation = (actor->animation + 1) % 6;
                                actor->animation_timer = 0;
                                if ((actor->animation % 6) == 3) context_play_sample_freq(app->sound_picaxe, 10500 + (rand()%1000));
                            }
                        } else if (target == TILE_BOULDER) {
                            world.hits[ncy][ncx] -= actor->drilling;
                            if (world.hits[ncy][ncx] <= 0) {
                                int pcx = ncx + (actor->facing == DIR_RIGHT ? 1 : (actor->facing == DIR_LEFT ? -1 : 0));
                                int pcy = ncy + (actor->facing == DIR_DOWN ? 1 : (actor->facing == DIR_UP ? -1 : 0));
                                if (pcx >= 0 && pcx < MAP_WIDTH && pcy >= 0 && pcy < MAP_HEIGHT && is_passable(world.tiles[pcy][pcx])) {
                                    world.tiles[pcy][pcx] = TILE_BOULDER; world.hits[pcy][pcx] = 24;
                                    world.tiles[ncy][ncx] = TILE_PASSAGE; world.hits[ncy][ncx] = 0;
                                } else world.hits[ncy][ncx] = 24;
                            }
                            actor->animation_timer++;
                            if (actor->animation_timer >= 8) { actor->animation = (actor->animation + 1) % 4; actor->animation_timer = 0; }
                        } else if (target == TILE_WALL) actor->moving = false;
                    }
                } else if (d_ortho != 5) {
                    if (actor->facing == DIR_UP || actor->facing == DIR_DOWN) actor->pos.x = (actor->pos.x / 10) * 10 + 5;
                    else actor->pos.y = ((actor->pos.y - 30) / 10) * 10 + 35;
                }
            } else { actor->animation = 0; }
        }

        for (int y = 0; y < MAP_HEIGHT; ++y) {
            for (int x = 0; x < MAP_WIDTH; ++x) {
                if (world.timer[y][x] > 0) {
                    world.timer[y][x]--;
                    if (world.timer[y][x] == 0) {
                        uint8_t t = world.tiles[y][x];
                        if (t == TILE_SMALL_BOMB3) {
                            if (app->sound_pikkupom) context_play_sample_freq(app->sound_pikkupom, 11000);
                            explode_pattern(&world, x, y, 60, SMALL_BOMB_PATTERN, sizeof(SMALL_BOMB_PATTERN)/sizeof(SMALL_BOMB_PATTERN[0]));
                        } else if (t == TILE_BIG_BOMB3) {
                            if (app->sound_explos1) context_play_sample_freq(app->sound_explos1, 11000);
                            explode_pattern(&world, x, y, 84, BIG_BOMB_PATTERN, sizeof(BIG_BOMB_PATTERN)/sizeof(BIG_BOMB_PATTERN[0]));
                        } else if (t == TILE_DYNAMITE3) {
                            if (app->sound_explos2) context_play_sample_freq(app->sound_explos2, 11000);
                            explode_pattern(&world, x, y, 100, DYNAMITE_PATTERN, sizeof(DYNAMITE_PATTERN)/sizeof(DYNAMITE_PATTERN[0]));
                        } else if (t == TILE_ATOMIC1 || t == TILE_ATOMIC2 || t == TILE_ATOMIC3) {
                            world.tiles[y][x] = TILE_PASSAGE;
                            explode_nuke(&world, x, y);
                            if (app->sound_explos3) {
                                context_play_sample_freq(app->sound_explos3, 5000);
                                context_play_sample_freq(app->sound_explos3, 9900);
                                context_play_sample_freq(app->sound_explos3, 10000);
                            }
                        } else if (t == TILE_EXPLOSION) {
                            world.tiles[y][x] = TILE_SMOKE1;
                            world.timer[y][x] = 3;
                        } else if (t == TILE_SMOKE1) {
                            world.tiles[y][x] = TILE_SMOKE2;
                            world.timer[y][x] = 3;
                        } else if (t == TILE_SMOKE2) {
                            world.tiles[y][x] = TILE_PASSAGE;
                            world.timer[y][x] = 0;
                        } else {
                            world.tiles[y][x] = TILE_PASSAGE;
                            for (int p = 0; p < world.num_players; p++) {
                                if (world.actors[p].is_dead && (world.actors[p].pos.x / 10) == x && ((world.actors[p].pos.y - 30) / 10) == y) {
                                    world.tiles[y][x] = TILE_BLOOD;
                                }
                            }
                        }
                    } else {
                        int clock = world.timer[y][x] + 1;
                        uint8_t t = world.tiles[y][x];
                        if (t == TILE_SMALL_BOMB1 && clock <= 60) world.tiles[y][x] = TILE_SMALL_BOMB2;
                        else if (t == TILE_SMALL_BOMB2 && clock <= 30) world.tiles[y][x] = TILE_SMALL_BOMB3;
                        else if (t == TILE_BIG_BOMB1 && clock <= 60) world.tiles[y][x] = TILE_BIG_BOMB2;
                        else if (t == TILE_BIG_BOMB2 && clock <= 30) world.tiles[y][x] = TILE_BIG_BOMB3;
                        else if (t == TILE_DYNAMITE1 && clock <= 40) world.tiles[y][x] = TILE_DYNAMITE2;
                        else if (t == TILE_DYNAMITE2 && clock <= 20) world.tiles[y][x] = TILE_DYNAMITE3;
                        else if (t == TILE_ATOMIC1) world.tiles[y][x] = TILE_ATOMIC2;
                        else if (t == TILE_ATOMIC2) world.tiles[y][x] = TILE_ATOMIC3;
                        else if (t == TILE_ATOMIC3) world.tiles[y][x] = TILE_ATOMIC1;
                    }
                }
            }
        }

        if (world.darkness) {
            for (int p = 0; p < world.num_players; p++) {
                if (!world.actors[p].is_dead) {
                    int px = world.actors[p].pos.x / 10;
                    int py = (world.actors[p].pos.y - 30) / 10;
                    reveal_view(&world, px, py, world.actors[p].facing);
                }
            }
        }

        render_world(app, ctx, &world);
        SDL_Delay(16);
    }

    RoundResult result;
    memset(&result, 0, sizeof(result));
    for (int p = 0; p < world.num_players; p++) {
        result.player_survived[p] = !world.actors[p].is_dead;
    }
    return result;
}
