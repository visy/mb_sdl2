#include "game.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static bool is_passable(uint8_t val) {
    if (val == 0x30 || val == 0x66 || val == 0xAF) return true;
    return false;
}

static bool is_treasure(uint8_t val) {
    if (val == 0x73) return true; 
    if (val >= 0x8F && val <= 0x9A) return true; 
    if (val == 0x6D || val == 0x79 || val == 0xB3) return true;
    return false;
}

static bool is_sand(uint8_t val) {
    return (val >= 0x32 && val <= 0x34);
}

static bool is_stone(uint8_t val) {
    return (val >= 0x43 && val <= 0x46) || (val >= 0x37 && val <= 0x39) || val == 0x41 || val == 0x70 || val == 0x71;
}

static int get_initial_hits(uint8_t val) {
    if (val == 0x31) return 30000;
    if (is_stone(val)) return 1500;
    if (is_sand(val)) return 1;
    return 0;
}

void game_init_world(World* world, uint8_t* level_data) {
    memset(world, 0, sizeof(World));
    for (int y = 0; y < MAP_HEIGHT; ++y) {
        uint8_t* row_ptr = &level_data[y * 66];
        for (int x = 0; x < MAP_WIDTH; ++x) {
            world->tiles[y][x] = row_ptr[x];
            world->hits[y][x] = get_initial_hits(row_ptr[x]);
        }
    }

    world->player.pos.x = 15;
    world->player.pos.y = 45; // row 1 center (1 * 10 + 35)
    world->player.health = 100;
    world->player.max_health = 100;
    world->player.drilling = 10;
    world->player.facing = DIR_RIGHT;
    world->player.moving = false;
    world->player.is_digging = false;
}

static void render_world(App* app, ApplicationContext* ctx, World* world) {
    SDL_SetRenderTarget(ctx->renderer, ctx->buffer);
    SDL_RenderCopy(ctx->renderer, app->players.texture, NULL, NULL);

    for (int y = 0; y < MAP_HEIGHT; ++y) {
        for (int x = 0; x < MAP_WIDTH; ++x) {
            uint8_t val = world->tiles[y][x];
            glyphs_render(&app->glyphs, ctx->renderer, x * 10, y * 10 + 30, (GlyphType)(GLYPH_MAP_START + val));
        }
    }

    int base = world->player.is_digging ? GLYPH_PLAYER_DIG_START : GLYPH_PLAYER_START;
    int anim_frame = 0;
    if (world->player.is_digging) {
        static const int pp[] = {0, 1, 2, 3, 2, 1};
        anim_frame = pp[world->player.animation % 6];
    } else {
        anim_frame = world->player.animation % 4;
    }

    int p_glyph = base + (int)world->player.facing + (anim_frame * 4);
    glyphs_render(&app->glyphs, ctx->renderer, world->player.pos.x - 5, world->player.pos.y - 5, (GlyphType)p_glyph);

    SDL_SetRenderTarget(ctx->renderer, NULL);
    context_present(ctx);
}

void game_run(App* app, ApplicationContext* ctx, uint8_t* level_data) {
    int tracks[] = {0, 39, 55};
    context_play_music_at(ctx, "OEKU.S3M", tracks[rand() % 3]);
    
    World world;
    game_init_world(&world, level_data);

    bool running = true;
    while (running) {
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_QUIT) { running = false; break; }
            if (e.type == SDL_KEYDOWN) {
                switch (e.key.keysym.scancode) {
                    case SDL_SCANCODE_ESCAPE: running = false; break;
                    case SDL_SCANCODE_UP:    world.player.facing = DIR_UP;    world.player.moving = true; break;
                    case SDL_SCANCODE_DOWN:  world.player.facing = DIR_DOWN;  world.player.moving = true; break;
                    case SDL_SCANCODE_LEFT:  world.player.facing = DIR_LEFT;  world.player.moving = true; break;
                    case SDL_SCANCODE_RIGHT: world.player.facing = DIR_RIGHT; world.player.moving = true; break;
                    case SDL_SCANCODE_SPACE: {
                        int cx = world.player.pos.x / 10;
                        int cy = (world.player.pos.y - 35) / 10;
                        if (cx >= 0 && cx < MAP_WIDTH && cy >= 0 && cy < MAP_HEIGHT) {
                            world.tiles[cy][cx] = 0x57; 
                            world.timer[cy][cx] = 60;
                        }
                    } break;
                    default: break;
                }
            }
        }

        world.player.is_digging = false;
        if (world.player.moving) {
            int dx = world.player.pos.x % 10;
            int dy = (world.player.pos.y - 30) % 10;
            int cx = world.player.pos.x / 10;
            int cy = (world.player.pos.y - 30) / 10;

            int d_dir = 0, d_ortho = 0;
            bool finishing = false;

            switch(world.player.facing) {
                case DIR_LEFT:  d_dir = dx; d_ortho = dy; finishing = dx > 5; break;
                case DIR_RIGHT: d_dir = dx; d_ortho = dy; finishing = dx < 5; break;
                case DIR_UP:    d_dir = dy; d_ortho = dx; finishing = dy > 5; break;
                case DIR_DOWN:  d_dir = dy; d_ortho = dx; finishing = dy < 5; break;
                default: break;
            }

            if (d_ortho == 5 && (finishing || is_passable(world.tiles[cy + (world.player.facing == DIR_DOWN ? 1 : (world.player.facing == DIR_UP ? -1 : 0))][cx + (world.player.facing == DIR_RIGHT ? 1 : (world.player.facing == DIR_LEFT ? -1 : 0))]) || is_treasure(world.tiles[cy + (world.player.facing == DIR_DOWN ? 1 : (world.player.facing == DIR_UP ? -1 : 0))][cx + (world.player.facing == DIR_RIGHT ? 1 : (world.player.facing == DIR_LEFT ? -1 : 0))]) )) {
                if (world.player.facing == DIR_LEFT) world.player.pos.x--;
                else if (world.player.facing == DIR_RIGHT) world.player.pos.x++;
                else if (world.player.facing == DIR_UP) world.player.pos.y--;
                else if (world.player.facing == DIR_DOWN) world.player.pos.y++;
                
                int ncx = world.player.pos.x / 10;
                int ncy = (world.player.pos.y - 30) / 10;
                if (is_treasure(world.tiles[ncy][ncx]) && (world.player.pos.x % 10 == 5) && ((world.player.pos.y - 30) % 10 == 5)) {
                    world.tiles[ncy][ncx] = 0x30;
                    int f[] = {10000, 12599, 14983};
                    context_play_sample_freq(app->sound_kili, f[rand()%3]);
                }

                world.player.animation_timer++;
                if (world.player.animation_timer >= 4) {
                    world.player.animation = (world.player.animation + 1) % 4;
                    world.player.animation_timer = 0;
                }
            } else if (d_ortho == 5 && d_dir == 5) {
                int ncx = cx, ncy = cy;
                if (world.player.facing == DIR_LEFT) ncx--;
                else if (world.player.facing == DIR_RIGHT) ncx++;
                else if (world.player.facing == DIR_UP) ncy--;
                else if (world.player.facing == DIR_DOWN) ncy++;

                if (ncx >= 0 && ncx < MAP_WIDTH && ncy >= 0 && ncy < MAP_HEIGHT) {
                    uint8_t target = world.tiles[ncy][ncx];
                    if (is_sand(target) || is_stone(target)) {
                        world.player.is_digging = true;
                        world.hits[ncy][ncx] -= world.player.drilling;
                        if (is_stone(target)) {
                            if (world.hits[ncy][ncx] < 500) world.tiles[ncy][ncx] = 0x71; 
                            else if (world.hits[ncy][ncx] < 1000) world.tiles[ncy][ncx] = 0x70;
                        }
                        if (world.hits[ncy][ncx] <= 0) {
                            world.tiles[ncy][ncx] = 0x30;
                            world.player.is_digging = false;
                        }
                        world.player.animation_timer++;
                        if (world.player.animation_timer >= 8) {
                            world.player.animation = (world.player.animation + 1) % 6;
                            world.player.animation_timer = 0;
                            if ((world.player.animation % 6) == 3) {
                                int f[] = {10500, 11000, 11500};
                                context_play_sample_freq(app->sound_picaxe, f[rand()%3]);
                            }
                        }
                    } else if (target == 0x31) {
                        world.player.moving = false;
                    }
                }
            } else if (d_ortho != 5) {
                if (world.player.facing == DIR_UP || world.player.facing == DIR_DOWN)
                    world.player.pos.x = (world.player.pos.x / 10) * 10 + 5;
                else
                    world.player.pos.y = ((world.player.pos.y - 30) / 10) * 10 + 35;
            }
        } else {
            world.player.animation = 0;
        }

        for (int y = 0; y < MAP_HEIGHT; ++y) {
            for (int x = 0; x < MAP_WIDTH; ++x) {
                if (world.timer[y][x] > 0) {
                    world.timer[y][x]--;
                    if (world.timer[y][x] == 0) world.tiles[y][x] = 0x30;
                }
            }
        }

        render_world(app, ctx, &world);
        SDL_Delay(16);
    }
}
