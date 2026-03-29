#include "game.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

// Deterministic PRNG for multiplayer sync (xorshift32)
static uint32_t game_rng_state = 1;

void game_seed_rng(uint32_t seed) {
    game_rng_state = seed ? seed : 1;
}

int game_rand(void) {
    game_rng_state ^= game_rng_state << 13;
    game_rng_state ^= game_rng_state >> 17;
    game_rng_state ^= game_rng_state << 5;
    return (int)(game_rng_state & 0x7FFFFFFF);
}

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

// ==================== Tile helpers ====================

bool is_passable(uint8_t val) {
    return val == TILE_PASSAGE || val == TILE_BLOOD || val == TILE_SLIME_CORPSE;
}


static bool is_treasure(uint8_t val) {
    if (val == TILE_DIAMOND) return true;
    if (val >= TILE_GOLD_SHIELD && val <= TILE_GOLD_CROWN) return true;
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

static bool is_brick(uint8_t val) {
    return val == TILE_BRICK || val == TILE_BRICK_CRACKED_LIGHT || val == TILE_BRICK_CRACKED_HEAVY;
}

static const uint8_t PUSHABLE_BITMAP[32] = {
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x04, 0x00, 0x80, 0x03, 0x98, 0x07, 0x80, 0xF1,
    0x0F, 0x7C, 0x00, 0xE0, 0x1E, 0x0C, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
};

static bool is_pushable(uint8_t val) {
    return (PUSHABLE_BITMAP[val / 8] & (1 << (val & 7))) != 0;
}

static bool is_bomb(uint8_t val) {
    return val == TILE_SMALL_BOMB1 || val == TILE_SMALL_BOMB2 || val == TILE_SMALL_BOMB3
        || val == TILE_BIG_BOMB1 || val == TILE_BIG_BOMB2 || val == TILE_BIG_BOMB3
        || val == TILE_DYNAMITE1 || val == TILE_DYNAMITE2 || val == TILE_DYNAMITE3
        || val == TILE_ATOMIC1 || val == TILE_ATOMIC2 || val == TILE_ATOMIC3
        || val == TILE_BARREL || val == TILE_MINE
        || val == TILE_SMALL_RADIO_BLUE || val == TILE_SMALL_RADIO_RED
        || val == TILE_SMALL_RADIO_GREEN || val == TILE_SMALL_RADIO_YELLOW
        || val == TILE_BIG_RADIO_BLUE || val == TILE_BIG_RADIO_RED
        || val == TILE_BIG_RADIO_GREEN || val == TILE_BIG_RADIO_YELLOW
        || val == TILE_SMALL_CRUCIFIX_BOMB || val == TILE_LARGE_CRUCIFIX_BOMB
        || val == TILE_PLASTIC_BOMB || val == TILE_EXPLOSIVE_PLASTIC_BOMB
        || val == TILE_DIGGER_BOMB || val == TILE_NAPALM1 || val == TILE_NAPALM2
        || val == TILE_JUMPING_BOMB
        || val == TILE_SMALL_BOMB_EXTINGUISHED || val == TILE_BIG_BOMB_EXTINGUISHED
        || val == TILE_DYNAMITE_EXTINGUISHED || val == TILE_NAPALM_EXTINGUISHED;
}

static bool is_small_radio(uint8_t val) {
    return val == TILE_SMALL_RADIO_BLUE || val == TILE_SMALL_RADIO_RED
        || val == TILE_SMALL_RADIO_GREEN || val == TILE_SMALL_RADIO_YELLOW;
}

static bool is_big_radio(uint8_t val) {
    return val == TILE_BIG_RADIO_BLUE || val == TILE_BIG_RADIO_RED
        || val == TILE_BIG_RADIO_GREEN || val == TILE_BIG_RADIO_YELLOW;
}

static bool is_radio_for(uint8_t val, int player) {
    switch (player) {
        case 0: return val == TILE_SMALL_RADIO_BLUE || val == TILE_BIG_RADIO_BLUE;
        case 1: return val == TILE_SMALL_RADIO_RED || val == TILE_BIG_RADIO_RED;
        case 2: return val == TILE_SMALL_RADIO_GREEN || val == TILE_BIG_RADIO_GREEN;
        case 3: return val == TILE_SMALL_RADIO_YELLOW || val == TILE_BIG_RADIO_YELLOW;
        default: return false;
    }
}

static bool blocks_crucifix(uint8_t val) {
    return val == TILE_WALL || val == TILE_EXIT || val == TILE_DOOR
        || val == TILE_BUTTON_OFF || val == (TILE_BUTTON_OFF + 1);
}

static bool is_diggable(uint8_t val) {
    return is_sand(val) || is_stone(val) || is_brick(val)
        || val == TILE_STONE_CRACKED_LIGHT || val == TILE_STONE_CRACKED_HEAVY
        || val == TILE_GRAVEL_LIGHT || val == TILE_GRAVEL_HEAVY
        || val == TILE_BIOMASS || val == TILE_PLASTIC || val == TILE_EXPLOSIVE_PLASTIC;
}

// ==================== Darkness ====================

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
        // Determine orthogonal step direction matching Rust's ortho()/reverse():
        //   RIGHT: ortho=Down,  so offset>=0 → +y, offset<0 → -y
        //   LEFT:  ortho=Up,    so offset>=0 → -y, offset<0 → +y
        //   UP:    ortho=Right, so offset>=0 → +x, offset<0 → -x
        //   DOWN:  ortho=Left,  so offset>=0 → -x, offset<0 → +x
        int odx = 0, ody = 0;
        switch (facing) {
            case DIR_RIGHT: ody = (offset >= 0) ?  1 : -1; break;
            case DIR_LEFT:  ody = (offset >= 0) ? -1 :  1; break;
            case DIR_UP:    odx = (offset >= 0) ?  1 : -1; break;
            case DIR_DOWN:  odx = (offset >= 0) ? -1 :  1; break;
        }
        for (int step = 0; step <= 20; step++) {
            if (cx < 0 || cx >= MAP_WIDTH || cy < 0 || cy >= MAP_HEIGHT) break;
            world->fog[cy][cx] = false;
            if (!see_through(world->tiles[cy][cx])) break;
            if (slope_error > 0) {
                cx += odx;
                cy += ody;
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
    // Room reveal: walk forward from player, revealing adjacent cells
    int cx = px, cy = py;
    while (cx > 0 && cx < MAP_WIDTH - 1 && cy > 0 && cy < MAP_HEIGHT - 1 && is_passable(world->tiles[cy][cx])) {
        for (int d = 0; d < 4; d++) {
            int nx = cx + (d == 0 ? 1 : d == 1 ? -1 : 0);
            int ny = cy + (d == 2 ? -1 : d == 3 ? 1 : 0);
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

// ==================== Explosions ====================

static int explosion_chain_count = 0;
static App* explosion_app = NULL; // set before chain explosion starts
static bool explosion_nuke_sound_played = false;

static void explode_bomb(World* world, int cx, int cy);

static void apply_explosion_damage(World* world, int cx, int cy, int dmg) {
    for (int i = 0; i < world->num_actors; ++i) {
        Actor* actor = &world->actors[i];
        if (actor->is_dead) continue;
        int px = actor->pos.x / 10;
        int py = (actor->pos.y - 30) / 10;
        if (px == cx && py == cy) {
            int effective_dmg = (i < world->num_players) ? dmg * world->bomb_damage / 100 : dmg;
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

    if (is_bomb(val) || val == TILE_EXPLOSIVE_PLASTIC) {
        // Immediately trigger the bomb (matching Rust's immediate dispatch)
        explode_bomb(world, cx, cy);
        return;
    } else if (is_stone(val) || val == TILE_BOULDER) {
        if (heavy) {
            world->tiles[cy][cx] = TILE_EXPLOSION;
            world->timer[cy][cx] = 3;
        } else if (game_rand() % 2 == 0) {
            world->tiles[cy][cx] = TILE_STONE_CRACKED_HEAVY;
            world->hits[cy][cx] = 500;
        } else {
            world->tiles[cy][cx] = TILE_STONE_CRACKED_LIGHT;
            world->hits[cy][cx] = 1000;
        }
    } else if (is_brick(val)) {
        if (heavy) {
            world->tiles[cy][cx] = TILE_EXPLOSION;
            world->timer[cy][cx] = 3;
        } else if (world->hits[cy][cx] > 4000) {
            world->tiles[cy][cx] = TILE_BRICK_CRACKED_LIGHT;
            world->hits[cy][cx] = 4000;
        } else if (world->hits[cy][cx] > 2000) {
            world->tiles[cy][cx] = TILE_BRICK_CRACKED_HEAVY;
            world->hits[cy][cx] = 2000;
        } else {
            world->tiles[cy][cx] = TILE_EXPLOSION;
            world->timer[cy][cx] = 3;
        }
    } else if (val != TILE_WALL && val != TILE_DOOR && val != TILE_EXIT && val != TILE_BUTTON_OFF && val != (TILE_BUTTON_OFF + 1)) {
        world->tiles[cy][cx] = TILE_EXPLOSION;
        world->timer[cy][cx] = 3;
    }
    if (world->tiles[cy][cx] == TILE_EXPLOSION) world->burned[cy][cx] = 1;
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

static void explode_barrel(World* world, int cx, int cy, App* app) {
    world->tiles[cy][cx] = TILE_EXPLOSION;
    world->timer[cy][cx] = 3;
    world->burned[cy][cx] = 1;
    if (app->sound_explos1) context_play_sample_freq(app->sound_explos1, 11000);
    int from = game_rand() % 5;
    for (int i = from; i < 15; i++) {
        int dx = (game_rand() % 20) - 10;
        int dy = (game_rand() % 20) - 10;
        int tx = cx + dx, ty = cy + dy;
        if (tx >= 0 && tx < MAP_WIDTH && ty >= 0 && ty < MAP_HEIGHT) {
            explode_pattern(world, tx, ty, 84, BIG_BOMB_PATTERN, sizeof(BIG_BOMB_PATTERN)/sizeof(BIG_BOMB_PATTERN[0]));
        }
    }
}

static void explode_crucifix(World* world, int cx, int cy, bool small, App* app) {
    int dmg = small ? 100 : 200;
    world->tiles[cy][cx] = TILE_PASSAGE;
    explode_cell(world, cx, cy, dmg);
    int dx[] = {1, -1, 0, 0};
    int dy[] = {0, 0, -1, 1};
    for (int d = 0; d < 4; d++) {
        for (int dist = 1; dist <= (small ? 15 : 999); dist++) {
            int nx = cx + dx[d] * dist;
            int ny = cy + dy[d] * dist;
            if (nx < 0 || nx >= MAP_WIDTH || ny < 0 || ny >= MAP_HEIGHT) break;
            if (blocks_crucifix(world->tiles[ny][nx])) break;
            explode_cell(world, nx, ny, dmg);
        }
    }
    if (small) {
        if (app->sound_explos1) context_play_sample_freq(app->sound_explos1, 11000);
    } else {
        if (app->sound_explos3) context_play_sample_freq(app->sound_explos3, 11000);
    }
}

static void expand_fill(World* world, int cx, int cy, int max_tiles, bool (*can_expand)(uint8_t),
                         void (*finalize)(World*, int, int, void*), void* userdata) {
    // BFS expansion algorithm
    typedef struct { int x, y; } Pt;
    static Pt queue[5000];
    static bool visited[MAP_HEIGHT][MAP_WIDTH];
    memset(visited, 0, sizeof(visited));
    int head = 0, tail = 0, count = 0;
    queue[tail++] = (Pt){cx, cy};
    visited[cy][cx] = true;
    while (head < tail && count < max_tiles) {
        Pt p = queue[head++];
        finalize(world, p.x, p.y, userdata);
        count++;
        int dx[] = {1, -1, 0, 0};
        int dy[] = {0, 0, -1, 1};
        for (int d = 0; d < 4; d++) {
            int nx = p.x + dx[d], ny = p.y + dy[d];
            if (nx < 0 || nx >= MAP_WIDTH || ny < 0 || ny >= MAP_HEIGHT) continue;
            if (visited[ny][nx]) continue;
            if (!can_expand(world->tiles[ny][nx])) continue;
            visited[ny][nx] = true;
            if (tail < 5000) queue[tail++] = (Pt){nx, ny};
        }
    }
}

static bool expand_passable(uint8_t val) {
    return is_passable(val);
}

static bool is_flame_passable(uint8_t val) {
    return is_passable(val)
        || val == TILE_SMOKE1 || val == TILE_SMOKE2
        || val == TILE_BIOMASS
        || (val >= TILE_EXPLOSION && val <= TILE_MONSTER_SMOKE2)
        || val == TILE_PLASTIC;
}

static void activate_flamethrower(World* world, int cx, int cy, Direction facing, int player_cx, int player_cy) {
    // Directional cone expansion matching Rust FlamethrowerExpansion
    // Uses marker-based wave expansion: MARKER1 = already processed, MARKER2 = newly expanded
    // Direction: forward always, backward never, perpendicular only within cone (delta*2 <= main_dist)
    int dx = (facing == DIR_RIGHT) ? 1 : (facing == DIR_LEFT) ? -1 : 0;
    int dy = (facing == DIR_DOWN) ? 1 : (facing == DIR_UP) ? -1 : 0;

    // Start one cell ahead if passable
    int start_x = cx, start_y = cy;
    int nx = cx + dx, ny = cy + dy;
    if (nx >= 0 && nx < MAP_WIDTH && ny >= 0 && ny < MAP_HEIGHT && is_flame_passable(world->tiles[ny][nx])) {
        start_x = nx; start_y = ny;
    }

    // Use hits array temporarily as markers (save/restore)
    // Simpler: use a static array
    static uint8_t marker[MAP_HEIGHT][MAP_WIDTH];
    memset(marker, 0, sizeof(marker));
    #define FLAME_MARK1 1
    #define FLAME_MARK2 2

    marker[start_y][start_x] = FLAME_MARK1;
    int expanded = 0;
    int dirs_dx[] = {1, -1, 0, 0};
    int dirs_dy[] = {0, 0, -1, 1};
    Direction dirs[] = {DIR_RIGHT, DIR_LEFT, DIR_UP, DIR_DOWN};

    while (expanded < 30) {
        bool spread = false;
        for (int y = 1; y < MAP_HEIGHT - 1; y++) {
            for (int x = 1; x < MAP_WIDTH - 1; x++) {
                if (marker[y][x] != FLAME_MARK1) continue;
                for (int d = 0; d < 4; d++) {
                    int nnx = x + dirs_dx[d], nny = y + dirs_dy[d];
                    if (nnx < 1 || nnx >= MAP_WIDTH - 1 || nny < 1 || nny >= MAP_HEIGHT - 1) continue;
                    if (marker[nny][nnx] != 0) continue;
                    uint8_t val = world->tiles[nny][nnx];
                    if (!is_flame_passable(val)) continue;

                    // Direction check: forward=ok, backward=no, perpendicular=cone
                    Direction expand_dir = dirs[d];
                    if (expand_dir == facing) {
                        // forward: always ok
                    } else if ((facing == DIR_LEFT && expand_dir == DIR_RIGHT) ||
                               (facing == DIR_RIGHT && expand_dir == DIR_LEFT) ||
                               (facing == DIR_UP && expand_dir == DIR_DOWN) ||
                               (facing == DIR_DOWN && expand_dir == DIR_UP)) {
                        continue; // backward: never
                    } else {
                        // perpendicular: cone check
                        int delta_row = abs(nny - start_y);
                        int delta_col = abs(nnx - start_x);
                        if (expand_dir == DIR_UP || expand_dir == DIR_DOWN) {
                            if (delta_row * 2 > delta_col) continue;
                        } else {
                            if (delta_col * 2 > delta_row) continue;
                        }
                    }

                    marker[nny][nnx] = FLAME_MARK2;
                    expanded++;
                    spread = true;
                }
            }
        }
        if (!spread) break;
        // Convert MARK2 to MARK1 for next wave
        for (int y = 0; y < MAP_HEIGHT; y++)
            for (int x = 0; x < MAP_WIDTH; x++)
                if (marker[y][x] == FLAME_MARK2) marker[y][x] = FLAME_MARK1;
    }

    // Finalize: explode all marked cells (skip player's own cell)
    for (int y = 0; y < MAP_HEIGHT; y++)
        for (int x = 0; x < MAP_WIDTH; x++)
            if (marker[y][x] == FLAME_MARK1) {
                world->tiles[y][x] = TILE_PASSAGE;
                if (x == player_cx && y == player_cy) continue;
                explode_cell_ex(world, x, y, 34, true);
            }
    #undef FLAME_MARK1
    #undef FLAME_MARK2
}

static bool expand_napalm(uint8_t val) {
    return val == TILE_PASSAGE || val == TILE_SMOKE1 || val == TILE_SMOKE2
        || val == TILE_BLOOD || val == TILE_BIOMASS || val == TILE_EXPLOSION
        || val == TILE_MONSTER_DYING || val == TILE_MONSTER_SMOKE1 || val == TILE_MONSTER_SMOKE2
        || val == TILE_PLASTIC || val == TILE_SLIME_CORPSE;
}

static bool expand_digger(uint8_t val) {
    return is_stone(val) || val == TILE_BOULDER
        || (val >= TILE_STONE_TOP_LEFT && val <= TILE_STONE_BOTTOM_RIGHT)
        || val == TILE_STONE_BOTTOM_LEFT;
}

static void finalize_plastic(World* world, int x, int y, void* userdata) {
    (void)userdata;
    // Don't place plastic on players
    for (int p = 0; p < world->num_players; p++) {
        if (!world->actors[p].is_dead && world->actors[p].pos.x / 10 == x &&
            (world->actors[p].pos.y - 30) / 10 == y) {
            world->tiles[y][x] = TILE_PASSAGE;
            world->timer[y][x] = 0;
            return;
        }
    }
    world->tiles[y][x] = TILE_PLASTIC;
    world->hits[y][x] = 400;
    world->timer[y][x] = 0;
}

static void finalize_explosive_plastic(World* world, int x, int y, void* userdata) {
    (void)userdata;
    for (int p = 0; p < world->num_players; p++) {
        if (!world->actors[p].is_dead && world->actors[p].pos.x / 10 == x &&
            (world->actors[p].pos.y - 30) / 10 == y) {
            world->tiles[y][x] = TILE_PASSAGE;
            world->timer[y][x] = 0;
            return;
        }
    }
    world->tiles[y][x] = TILE_EXPLOSIVE_PLASTIC;
    world->hits[y][x] = 400;
    world->timer[y][x] = 0;
}

static void finalize_digger(World* world, int x, int y, void* userdata) {
    (void)userdata;
    explode_cell_ex(world, x, y, 10, true);
}

static void finalize_napalm(World* world, int x, int y, void* userdata) {
    (void)userdata;
    world->tiles[y][x] = TILE_PASSAGE;
    explode_cell_ex(world, x, y, 220, true);
}

static void explode_jumping_bomb(World* world, int cx, int cy, App* app) {
    // Pick random bomb type and explode it
    int r = game_rand() % 3;
    int jumps = world->hits[cy][cx];
    if (r == 0) {
        world->tiles[cy][cx] = TILE_SMALL_BOMB1;
        if (app->sound_pikkupom) context_play_sample_freq(app->sound_pikkupom, 11000);
        explode_pattern(world, cx, cy, 60, SMALL_BOMB_PATTERN, sizeof(SMALL_BOMB_PATTERN)/sizeof(SMALL_BOMB_PATTERN[0]));
    } else if (r == 1) {
        world->tiles[cy][cx] = TILE_BIG_BOMB1;
        if (app->sound_explos1) context_play_sample_freq(app->sound_explos1, 11000);
        explode_pattern(world, cx, cy, 84, BIG_BOMB_PATTERN, sizeof(BIG_BOMB_PATTERN)/sizeof(BIG_BOMB_PATTERN[0]));
    } else {
        world->tiles[cy][cx] = TILE_DYNAMITE1;
        if (app->sound_explos2) context_play_sample_freq(app->sound_explos2, 11000);
        explode_pattern(world, cx, cy, 100, DYNAMITE_PATTERN, sizeof(DYNAMITE_PATTERN)/sizeof(DYNAMITE_PATTERN[0]));
    }

    // Jump to new location if jumps remaining
    if (jumps > 1) {
        int nx = -1, ny = -1;
        for (int attempt = 0; attempt < 6; attempt++) {
            int dx = (game_rand() % 8) - 4;
            int dy = (game_rand() % 8) - 4;
            int tx = cx + dx, ty = cy + dy;
            if (tx >= 0 && tx < MAP_WIDTH && ty >= 0 && ty < MAP_HEIGHT) {
                uint8_t v = world->tiles[ty][tx];
                if (v == TILE_PASSAGE || is_sand(v) || is_stone(v) || v == TILE_BOULDER || v == TILE_EXPLOSION
                    || (v >= TILE_STONE_TOP_LEFT && v <= TILE_STONE_BOTTOM_RIGHT) || v == TILE_STONE_BOTTOM_LEFT) {
                    nx = tx; ny = ty;
                }
            }
        }
        if (nx < 0) { nx = cx; ny = cy; }
        world->tiles[ny][nx] = TILE_JUMPING_BOMB;
        world->hits[ny][nx] = jumps - 1;
        world->timer[ny][nx] = 1 + game_rand() % 180;
    }
}

static void explode_bomb(World* world, int cx, int cy) {
    if (explosion_chain_count > 200) return;
    explosion_chain_count++;
    uint8_t t = world->tiles[cy][cx];
    App* app = explosion_app;

    // Clear tile first so it doesn't get re-triggered
    world->tiles[cy][cx] = TILE_PASSAGE;
    world->timer[cy][cx] = 0;

    if (t == TILE_SMALL_BOMB1 || t == TILE_SMALL_BOMB2 || t == TILE_SMALL_BOMB3
        || t == TILE_MINE || t == TILE_SMALL_BOMB_EXTINGUISHED) {
        if (app && app->sound_pikkupom) context_play_sample_freq(app->sound_pikkupom, 11000);
        explode_pattern(world, cx, cy, 60, SMALL_BOMB_PATTERN, sizeof(SMALL_BOMB_PATTERN)/sizeof(SMALL_BOMB_PATTERN[0]));
    } else if (t == TILE_BIG_BOMB1 || t == TILE_BIG_BOMB2 || t == TILE_BIG_BOMB3
               || is_small_radio(t) || t == TILE_BIG_BOMB_EXTINGUISHED) {
        if (app && app->sound_explos1) context_play_sample_freq(app->sound_explos1, 11000);
        explode_pattern(world, cx, cy, 84, BIG_BOMB_PATTERN, sizeof(BIG_BOMB_PATTERN)/sizeof(BIG_BOMB_PATTERN[0]));
    } else if (t == TILE_EXPLOSIVE_PLASTIC) {
        if (app && app->sound_explos1) context_play_sample_freq(app->sound_explos1, 11000);
        explode_pattern(world, cx, cy, 84, BIG_BOMB_PATTERN, sizeof(BIG_BOMB_PATTERN)/sizeof(BIG_BOMB_PATTERN[0]));
    } else if (t == TILE_DYNAMITE1 || t == TILE_DYNAMITE2 || t == TILE_DYNAMITE3
               || is_big_radio(t) || t == TILE_DYNAMITE_EXTINGUISHED) {
        if (app && app->sound_explos2) context_play_sample_freq(app->sound_explos2, 11000);
        explode_pattern(world, cx, cy, 100, DYNAMITE_PATTERN, sizeof(DYNAMITE_PATTERN)/sizeof(DYNAMITE_PATTERN[0]));
    } else if (t == TILE_ATOMIC1 || t == TILE_ATOMIC2 || t == TILE_ATOMIC3) {
        explode_nuke(world, cx, cy);
        if (app && app->sound_explos3 && !explosion_nuke_sound_played) {
            context_play_sample_freq(app->sound_explos3, 5000);
            context_play_sample_freq(app->sound_explos3, 9900);
            context_play_sample_freq(app->sound_explos3, 10000);
            explosion_nuke_sound_played = true;
        }
    } else if (t == TILE_BARREL) {
        world->tiles[cy][cx] = t; // restore for explode_barrel
        explode_barrel(world, cx, cy, app);
    } else if (t == TILE_SMALL_CRUCIFIX_BOMB) {
        explode_crucifix(world, cx, cy, true, app);
    } else if (t == TILE_LARGE_CRUCIFIX_BOMB) {
        explode_crucifix(world, cx, cy, false, app);
    } else if (t == TILE_PLASTIC_BOMB) {
        expand_fill(world, cx, cy, 45, expand_passable, finalize_plastic, NULL);
        if (app && app->sound_urethan) context_play_sample_freq(app->sound_urethan, 11000);
    } else if (t == TILE_EXPLOSIVE_PLASTIC_BOMB) {
        expand_fill(world, cx, cy, 50, expand_passable, finalize_explosive_plastic, NULL);
        if (app && app->sound_urethan) context_play_sample_freq(app->sound_urethan, 11000);
    } else if (t == TILE_DIGGER_BOMB) {
        expand_fill(world, cx, cy, 75, expand_digger, finalize_digger, NULL);
        if (app && app->sound_explos2) context_play_sample_freq(app->sound_explos2, 11000);
    } else if (t == TILE_NAPALM1 || t == TILE_NAPALM2 || t == TILE_NAPALM_EXTINGUISHED) {
        expand_fill(world, cx, cy, 75, expand_napalm, finalize_napalm, NULL);
        if (app && app->sound_explos5) context_play_sample_freq(app->sound_explos5, 11000);
    } else if (t == TILE_JUMPING_BOMB) {
        world->tiles[cy][cx] = t; // restore for explode_jumping_bomb
        explode_jumping_bomb(world, cx, cy, app);
    } else if (t == TILE_GRENADE_FLY_R || t == TILE_GRENADE_FLY_L
               || t == TILE_GRENADE_FLY_D || t == TILE_GRENADE_FLY_U) {
        // Grenade hit by explosion: explode as small bomb
        if (app && app->sound_pikkupom) context_play_sample_freq(app->sound_pikkupom, 11000);
        explode_pattern(world, cx, cy, 60, SMALL_BOMB_PATTERN, sizeof(SMALL_BOMB_PATTERN)/sizeof(SMALL_BOMB_PATTERN[0]));
    } else {
        // Unknown bomb type: generic explosion
        world->tiles[cy][cx] = TILE_EXPLOSION;
        world->timer[cy][cx] = 3;
        world->burned[cy][cx] = 1;
    }
}

// ==================== Tile initialization ====================

static int get_initial_hits(uint8_t val) {
    if (val == TILE_WALL) return 30000;
    if (is_sand(val)) return 24;
    if (val == TILE_GRAVEL_LIGHT) return 108;
    if (val == TILE_GRAVEL_HEAVY) return 347;
    if (val >= TILE_STONE_TOP_LEFT && val <= TILE_STONE_BOTTOM_RIGHT) return 1227;
    if (val == TILE_STONE_BOTTOM_LEFT) return 1227;
    if (val == TILE_STONE1) return 2000;
    if (val == TILE_STONE2) return 2150;
    if (val == TILE_STONE3) return 2200;
    if (val == TILE_STONE4) return 2100;
    if (val == TILE_STONE_CRACKED_LIGHT) return 1000;
    if (val == TILE_STONE_CRACKED_HEAVY) return 500;
    if (val == TILE_BOULDER) return 24;
    if (val == TILE_BRICK) return 8000;
    if (val == TILE_BRICK_CRACKED_LIGHT) return 4000;
    if (val == TILE_BRICK_CRACKED_HEAVY) return 2000;
    if (val == TILE_BIOMASS || val == TILE_PLASTIC) return 400;
    if (val == TILE_DOOR) return 30000;
    if (val == TILE_BUTTON_OFF) return 30000;
    if (val == TILE_EXPLOSIVE_PLASTIC) return 400;
    return 0;
}

// ==================== Monster helpers ====================

static int monster_speed(ActorKind kind) {
    switch (kind) {
        case ACTOR_FURRY: return 6;
        case ACTOR_GRENADIER: return 3;
        case ACTOR_SLIME: return 2;
        case ACTOR_ALIEN: return 100;
        case ACTOR_CLONE: return 100;
        default: return 1;
    }
}

static int monster_damage(ActorKind kind) {
    switch (kind) {
        case ACTOR_FURRY: return 2;
        case ACTOR_GRENADIER: return 3;
        case ACTOR_SLIME: return 1;
        case ACTOR_ALIEN: return 5;
        case ACTOR_CLONE: return 1;
        default: return 0;
    }
}

static int monster_initial_health(ActorKind kind) {
    switch (kind) {
        case ACTOR_FURRY: return 29;
        case ACTOR_GRENADIER: return 29;
        case ACTOR_SLIME: return 10;
        case ACTOR_ALIEN: return 66;
        default: return 100;
    }
}

static int monster_drilling(ActorKind kind) {
    switch (kind) {
        case ACTOR_FURRY: return 5;
        case ACTOR_GRENADIER: return 12;
        case ACTOR_SLIME: return 12;
        case ACTOR_ALIEN: return 52;
        default: return 10;
    }
}

static GlyphType monster_glyph_base(ActorKind kind) {
    switch (kind) {
        case ACTOR_FURRY: return GLYPH_MONSTER_FURRY;
        case ACTOR_GRENADIER: return GLYPH_MONSTER_GRENADIER;
        case ACTOR_SLIME: return GLYPH_MONSTER_SLIME;
        case ACTOR_ALIEN: return GLYPH_MONSTER_ALIEN;
        default: return GLYPH_MONSTER_FURRY;
    }
}

// ==================== World initialization ====================

void game_init_world(World* world, uint8_t* level_data, int num_players) {
    memset(world, 0, sizeof(World));
    world->num_players = num_players;
    world->num_actors = num_players;

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

            // Spawn enemies from map tiles
            ActorKind mkind = ACTOR_PLAYER;
            Direction mdir = DIR_RIGHT;
            bool spawn = false;
            if (val >= TILE_FURRY_RIGHT && val <= TILE_FURRY_DOWN) {
                mkind = ACTOR_FURRY; mdir = (Direction)(val - TILE_FURRY_RIGHT); spawn = true;
            } else if (val >= TILE_GRENADIER_RIGHT && val <= TILE_GRENADIER_DOWN) {
                mkind = ACTOR_GRENADIER; mdir = (Direction)(val - TILE_GRENADIER_RIGHT); spawn = true;
            } else if (val >= TILE_SLIME_RIGHT && val <= TILE_SLIME_DOWN) {
                mkind = ACTOR_SLIME; mdir = (Direction)(val - TILE_SLIME_RIGHT); spawn = true;
            } else if (val >= TILE_ALIEN_RIGHT && val <= TILE_ALIEN_DOWN) {
                mkind = ACTOR_ALIEN; mdir = (Direction)(val - TILE_ALIEN_RIGHT); spawn = true;
            }

            if (spawn && world->num_actors < MAX_ACTORS) {
                Actor* m = &world->actors[world->num_actors];
                memset(m, 0, sizeof(Actor));
                m->kind = mkind;
                m->facing = mdir;
                m->pos.x = x * 10 + 5;
                m->pos.y = y * 10 + 35;
                m->health = monster_initial_health(mkind);
                m->max_health = m->health;
                m->drilling = monster_drilling(mkind);
                m->moving = false;
                m->is_active = false;
                world->num_actors++;
                val = TILE_PASSAGE;
            }

            // Biomass gets random initial timer
            if (val == TILE_BIOMASS) {
                world->tiles[y][x] = val;
                world->hits[y][x] = 400;
                world->timer[y][x] = game_rand() % 30;
            } else {
                world->tiles[y][x] = val;
                world->hits[y][x] = get_initial_hits(val);
            }
        }
    }

    for (int p = 0; p < num_players; ++p) {
        world->actors[p].pos.x = 15;
        world->actors[p].pos.y = 45;
        world->actors[p].health = 100;
        world->actors[p].max_health = 100;
        world->actors[p].drilling = 0;
        world->actors[p].facing = DIR_RIGHT;
        world->actors[p].selected_weapon = EQUIP_SMALL_BOMB;
        world->actors[p].kind = ACTOR_PLAYER;
        world->actors[p].is_active = true;
    }
    if (num_players >= 2) {
        // Randomize P1/P2 spawn corners
        if (game_rand() % 2) {
            world->actors[0].pos.x = 15;  world->actors[0].pos.y = 45;  world->actors[0].facing = DIR_RIGHT;
            world->actors[1].pos.x = 625; world->actors[1].pos.y = 465; world->actors[1].facing = DIR_LEFT;
        } else {
            world->actors[0].pos.x = 625; world->actors[0].pos.y = 465; world->actors[0].facing = DIR_LEFT;
            world->actors[1].pos.x = 15;  world->actors[1].pos.y = 45;  world->actors[1].facing = DIR_RIGHT;
        }
    }
    if (num_players == 3) {
        if (game_rand() % 2) {
            world->actors[2].pos.x = 15;  world->actors[2].pos.y = 465; world->actors[2].facing = DIR_RIGHT;
        } else {
            world->actors[2].pos.x = 625; world->actors[2].pos.y = 45;  world->actors[2].facing = DIR_LEFT;
        }
    } else if (num_players == 4) {
        if (game_rand() % 2) {
            world->actors[2].pos.x = 15;  world->actors[2].pos.y = 465; world->actors[2].facing = DIR_RIGHT;
            world->actors[3].pos.x = 625; world->actors[3].pos.y = 45;  world->actors[3].facing = DIR_LEFT;
        } else {
            world->actors[2].pos.x = 625; world->actors[2].pos.y = 45;  world->actors[2].facing = DIR_LEFT;
            world->actors[3].pos.x = 15;  world->actors[3].pos.y = 465; world->actors[3].facing = DIR_RIGHT;
        }
    }
}

static void randomize_exits(World* world) {
    // Count exits, keep only one random one
    int exit_count = 0;
    for (int y = 0; y < MAP_HEIGHT; y++)
        for (int x = 0; x < MAP_WIDTH; x++)
            if (world->tiles[y][x] == TILE_EXIT) exit_count++;
    if (exit_count <= 1) return;
    int keep = game_rand() % exit_count;
    int idx = 0;
    for (int y = 0; y < MAP_HEIGHT; y++)
        for (int x = 0; x < MAP_WIDTH; x++)
            if (world->tiles[y][x] == TILE_EXIT) {
                if (idx != keep) world->tiles[y][x] = TILE_PASSAGE;
                idx++;
            }
}

// ==================== Door/button system ====================

static void open_doors(World* world) {
    for (int y = 0; y < MAP_HEIGHT; y++) {
        for (int x = 0; x < MAP_WIDTH; x++) {
            if (world->tiles[y][x] == TILE_BUTTON_OFF) {
                world->timer[y][x] = 40;
                // ButtonOn = TILE_BUTTON_OFF + 1 = 0xB5
                world->tiles[y][x] = TILE_BUTTON_OFF + 1;
            } else if (world->tiles[y][x] == TILE_DOOR) {
                world->tiles[y][x] = TILE_PASSAGE;
                world->open_door[y][x] = true;
            }
        }
    }
}

static bool door_explodes_entity(uint8_t val) {
    return is_bomb(val) || val == TILE_BIOMASS || val == TILE_PLASTIC
        || val == TILE_EXPLOSIVE_PLASTIC || val == TILE_WEAPONS_CRATE
        || val == TILE_MEDIKIT || val == TILE_BARREL;
}

static void close_doors(World* world) {
    for (int y = 0; y < MAP_HEIGHT; y++) {
        for (int x = 0; x < MAP_WIDTH; x++) {
            if (world->tiles[y][x] == TILE_BUTTON_OFF + 1) {
                world->timer[y][x] = 40;
                world->tiles[y][x] = TILE_BUTTON_OFF;
            } else if (world->open_door[y][x]) {
                uint8_t val = world->tiles[y][x];
                if (door_explodes_entity(val) && world->timer[y][x] > 0) {
                    world->timer[y][x] = 1; // trigger explosion next tick
                }
                world->tiles[y][x] = TILE_DOOR;
                world->open_door[y][x] = false;
                world->hits[y][x] = 30000;
            }
        }
    }
}

// ==================== Monster AI ====================

static bool monster_can_move(Actor* actor, World* world) {
    int cx = actor->pos.x / 10;
    int cy = (actor->pos.y - 30) / 10;
    int ncx = cx, ncy = cy;
    switch (actor->facing) {
        case DIR_RIGHT: ncx++; break;
        case DIR_LEFT:  ncx--; break;
        case DIR_UP:    ncy--; break;
        case DIR_DOWN:  ncy++; break;
    }
    if (ncx < 0 || ncx >= MAP_WIDTH || ncy < 0 || ncy >= MAP_HEIGHT) return false;
    uint8_t val = world->tiles[ncy][ncx];
    return is_passable(val) || is_sand(val) || is_treasure(val);
}

static void monster_head_to_target(Actor* actor, int tx, int ty, World* world) {
    int cx = actor->pos.x / 10;
    int cy = (actor->pos.y - 30) / 10;
    int dx = abs(cx - tx);
    int dy = abs(cy - ty);
    actor->moving = true;

    // Try longer dimension first
    if (dx > dy) {
        actor->facing = (cx > tx) ? DIR_LEFT : DIR_RIGHT;
    } else {
        actor->facing = (cy > ty) ? DIR_UP : DIR_DOWN;
    }
    if (monster_can_move(actor, world)) return;

    // Try shorter dimension
    if (dx <= dy) {
        actor->facing = (cx > tx) ? DIR_LEFT : DIR_RIGHT;
    } else {
        actor->facing = (cy > ty) ? DIR_UP : DIR_DOWN;
    }
    if (monster_can_move(actor, world)) return;

    // Random direction
    Direction dirs[] = {DIR_LEFT, DIR_RIGHT, DIR_UP, DIR_DOWN};
    int r = game_rand() % 5;
    if (r < 4) {
        actor->facing = dirs[r];
    } else {
        actor->moving = false;
    }
}

static void monster_avoid_bomb(Actor* actor, int bx, int by, World* world) {
    int cx = actor->pos.x / 10;
    int cy = (actor->pos.y - 30) / 10;
    int dx = abs(cx - bx);
    int dy = abs(cy - by);
    actor->moving = true;

    if (dx > dy || (game_rand() % 100) < 3) {
        actor->facing = (cx > bx) ? DIR_RIGHT : DIR_LEFT;
        if (!monster_can_move(actor, world)) actor->facing = DIR_DOWN;
        if (!monster_can_move(actor, world)) actor->facing = DIR_UP;
    } else {
        actor->facing = (cy > by) ? DIR_DOWN : DIR_UP;
        if (!monster_can_move(actor, world)) actor->facing = DIR_LEFT;
        if (!monster_can_move(actor, world)) actor->facing = DIR_RIGHT;
    }
}

static void monster_damage_players(World* world, int actor_idx) {
    Actor* monster = &world->actors[actor_idx];
    int mcx = monster->pos.x / 10;
    int mcy = (monster->pos.y - 30) / 10;
    int dmg = monster_damage(monster->kind);
    for (int p = 0; p < world->num_players; p++) {
        Actor* player = &world->actors[p];
        if (player->is_dead) continue;
        // Clone doesn't hurt its owner (or anyone in campaign mode)
        if (monster->kind == ACTOR_CLONE && (monster->clone_owner == p || world->campaign_mode)) continue;
        int pcx = player->pos.x / 10;
        int pcy = (player->pos.y - 30) / 10;
        if (pcx == mcx && pcy == mcy) {
            player->health -= dmg;
            if (player->health <= 0) {
                player->health = 0;
                player->is_dead = true;
            }
        }
    }
}

static bool in_direct_sight(World* world, int r1, int c1, int r2, int c2) {
    if (r1 == r2) {
        int lo = c1 < c2 ? c1 : c2, hi = c1 < c2 ? c2 : c1;
        for (int c = lo; c <= hi; c++)
            if (!is_passable(world->tiles[r1][c])) return false;
        return true;
    } else if (c1 == c2) {
        int lo = r1 < r2 ? r1 : r2, hi = r1 < r2 ? r2 : r1;
        for (int r = lo; r <= hi; r++)
            if (!is_passable(world->tiles[r][c1])) return false;
        return true;
    }
    return false;
}

static bool in_fov_sight(int mr, int mc, int pr, int pc, Direction facing) {
    int high, low, ortho1, ortho2;
    switch (facing) {
        case DIR_LEFT:  high = mc; low = pc; ortho1 = pr; ortho2 = mr; break;
        case DIR_RIGHT: high = pc; low = mc; ortho1 = pr; ortho2 = mr; break;
        case DIR_UP:    high = mr; low = pr; ortho1 = pc; ortho2 = mc; break;
        case DIR_DOWN:  high = pr; low = mr; ortho1 = pc; ortho2 = mc; break;
        default: return false;
    }
    return high >= low && high <= low + 7 && ortho2 + low < ortho1 + high && ortho1 + low < ortho2 + high;
}

static void monster_detect_players(World* world) {
    for (int i = world->num_players; i < world->num_actors; i++) {
        Actor* m = &world->actors[i];
        if (m->is_active || m->is_dead) continue;
        int mcx = m->pos.x / 10;
        int mcy = (m->pos.y - 30) / 10;
        for (int p = 0; p < world->num_players; p++) {
            Actor* player = &world->actors[p];
            if (player->is_dead) continue;
            int pcx = player->pos.x / 10;
            int pcy = (player->pos.y - 30) / 10;
            bool proximity = (abs(m->pos.x - player->pos.x) < 20 && abs(m->pos.y - player->pos.y) < 20);
            bool detected = proximity
                || in_direct_sight(world, mcy, mcx, pcy, pcx)
                || in_fov_sight(mcy, mcx, pcy, pcx, m->facing);
            if (detected) {
                m->is_active = true;
                m->moving = true;
                break;
            }
        }
    }
}

static void grenadier_maybe_throw(World* world, int actor_idx) {
    Actor* m = &world->actors[actor_idx];
    int mcx = m->pos.x / 10;
    int mcy = (m->pos.y - 30) / 10;

    // Check obstacle distance in facing direction
    int dist = 0;
    int sx = mcx, sy = mcy;
    for (int d = 0; d < 10; d++) {
        switch (m->facing) {
            case DIR_RIGHT: sx++; break;
            case DIR_LEFT:  sx--; break;
            case DIR_UP:    sy--; break;
            case DIR_DOWN:  sy++; break;
        }
        if (sx < 0 || sx >= MAP_WIDTH || sy < 0 || sy >= MAP_HEIGHT) break;
        if (!is_passable(world->tiles[sy][sx])) break;
        dist++;
    }
    if (dist <= 4) return;

    // Check if any player is on same row or column
    for (int p = 0; p < world->num_players; p++) {
        if (world->actors[p].is_dead) continue;
        int pcx = world->actors[p].pos.x / 10;
        int pcy = (world->actors[p].pos.y - 30) / 10;
        if ((pcx == mcx) != (pcy == mcy)) {
            // Throw grenade
            uint8_t gval = TILE_GRENADE_FLY_R;
            switch (m->facing) {
                case DIR_RIGHT: gval = TILE_GRENADE_FLY_R; break;
                case DIR_LEFT:  gval = TILE_GRENADE_FLY_L; break;
                case DIR_DOWN:  gval = TILE_GRENADE_FLY_D; break;
                case DIR_UP:    gval = TILE_GRENADE_FLY_U; break;
            }
            world->tiles[mcy][mcx] = gval;
            world->timer[mcy][mcx] = 1;
            break;
        }
    }
}

static void animate_monster(World* world, int idx) {
    Actor* actor = &world->actors[idx];
    if (!actor->moving || actor->is_dead) return;

    int dx = actor->pos.x % 10;
    int dy = (actor->pos.y - 30) % 10;
    int cx = actor->pos.x / 10;
    int cy = (actor->pos.y - 30) / 10;

    int d_dir = 0, d_ortho = 0;
    bool finishing = false;

    bool boundary_ok = false;
    switch (actor->facing) {
        case DIR_LEFT:  d_dir = dx; d_ortho = dy; finishing = dx > 5; boundary_ok = actor->pos.x > 5; break;
        case DIR_RIGHT: d_dir = dx; d_ortho = dy; finishing = dx < 5; boundary_ok = actor->pos.x < 635; break;
        case DIR_UP:    d_dir = dy; d_ortho = dx; finishing = dy > 5; boundary_ok = actor->pos.y > 35; break;
        case DIR_DOWN:  d_dir = dy; d_ortho = dx; finishing = dy < 5; boundary_ok = actor->pos.y < 475; break;
    }

    int ncx = cx + (actor->facing == DIR_RIGHT ? 1 : (actor->facing == DIR_LEFT ? -1 : 0));
    int ncy = cy + (actor->facing == DIR_DOWN ? 1 : (actor->facing == DIR_UP ? -1 : 0));

    bool can_move = boundary_ok && d_ortho > 3 && d_ortho < 6;
    if (can_move && ncx >= 0 && ncx < MAP_WIDTH && ncy >= 0 && ncy < MAP_HEIGHT) {
        uint8_t next_val = world->tiles[ncy][ncx];
        if (finishing || is_passable(next_val)) {
            switch (actor->facing) {
                case DIR_LEFT:  actor->pos.x--; break;
                case DIR_RIGHT: actor->pos.x++; break;
                case DIR_UP:    actor->pos.y--; break;
                case DIR_DOWN:  actor->pos.y++; break;
            }
        }
    }

    if (d_ortho != 5) {
        if (actor->facing == DIR_UP || actor->facing == DIR_DOWN) actor->pos.x = (actor->pos.x / 10) * 10 + 5;
        else actor->pos.y = ((actor->pos.y - 30) / 10) * 10 + 35;
    }

    // Interact with map when centered (matching Rust interact_map for monsters)
    if (d_dir == 5 && ncx >= 0 && ncx < MAP_WIDTH && ncy >= 0 && ncy < MAP_HEIGHT) {
        uint8_t val = world->tiles[ncy][ncx];
        if (is_diggable(val) && world->hits[ncy][ncx] < 30000) {
            world->hits[ncy][ncx] -= actor->drilling;
            if (is_stone(val) || val == TILE_STONE_CRACKED_LIGHT) {
                if (world->hits[ncy][ncx] < 500) world->tiles[ncy][ncx] = TILE_STONE_CRACKED_HEAVY;
                else if (world->hits[ncy][ncx] < 1000) world->tiles[ncy][ncx] = TILE_STONE_CRACKED_LIGHT;
            } else if (is_brick(val)) {
                if (world->hits[ncy][ncx] <= 2000) world->tiles[ncy][ncx] = TILE_BRICK_CRACKED_HEAVY;
                else if (world->hits[ncy][ncx] <= 4000) world->tiles[ncy][ncx] = TILE_BRICK_CRACKED_LIGHT;
            }
            if (world->hits[ncy][ncx] <= 0) {
                world->tiles[ncy][ncx] = TILE_PASSAGE;
                world->hits[ncy][ncx] = 0;
            }
        } else if (is_treasure(val) || val == TILE_DIAMOND
                   || (val >= TILE_SMALL_PICKAXE && val <= TILE_DRILL)) {
            // Monsters collect treasure and pickaxes (increases their drilling)
            int drill_value = 0;
            if (val == TILE_SMALL_PICKAXE) drill_value = 1;
            else if (val == TILE_LARGE_PICKAXE) drill_value = 3;
            else if (val == TILE_DRILL) drill_value = 5;
            actor->drilling += drill_value;
            // Clones share drilling with their owner
            if (actor->kind == ACTOR_CLONE && actor->clone_owner >= 0 && actor->clone_owner < world->num_players) {
                world->actors[actor->clone_owner].drilling += drill_value;
            }
            world->tiles[ncy][ncx] = TILE_PASSAGE;
            world->hits[ncy][ncx] = 0;
        } else if (val == TILE_MINE) {
            world->timer[ncy][ncx] = 1;
        } else if (is_pushable(val)) {
            // Monsters can push objects
            int pcx = ncx + (actor->facing == DIR_RIGHT ? 1 : (actor->facing == DIR_LEFT ? -1 : 0));
            int pcy = ncy + (actor->facing == DIR_DOWN ? 1 : (actor->facing == DIR_UP ? -1 : 0));
            if (world->hits[ncy][ncx] == 30000) {
                // Metal wall, can't push
            } else if (world->hits[ncy][ncx] > 1) {
                world->hits[ncy][ncx] -= actor->drilling;
            } else if (pcx >= 0 && pcx < MAP_WIDTH && pcy >= 0 && pcy < MAP_HEIGHT && is_passable(world->tiles[pcy][pcx])) {
                bool blocked = false;
                for (int ai = 0; ai < world->num_actors; ai++) {
                    if (world->actors[ai].is_dead) continue;
                    if (world->actors[ai].pos.x / 10 == pcx && (world->actors[ai].pos.y - 30) / 10 == pcy) { blocked = true; break; }
                }
                if (!blocked) {
                    world->tiles[pcy][pcx] = world->tiles[ncy][ncx];
                    world->timer[pcy][pcx] = world->timer[ncy][ncx];
                    world->hits[pcy][pcx] = 24;
                    world->tiles[ncy][ncx] = TILE_PASSAGE;
                    world->timer[ncy][ncx] = 0;
                    world->hits[ncy][ncx] = 0;
                }
            }
        } else if (val == TILE_BUTTON_OFF) {
            if (world->timer[ncy][ncx] <= 1) open_doors(world);
        } else if (val == TILE_BUTTON_OFF + 1) {
            if (world->timer[ncy][ncx] <= 1) close_doors(world);
        } else if (val == TILE_TELEPORT) {
            // Monsters can use teleporters
            int tele[256][2], nt = 0;
            for (int ty = 0; ty < MAP_HEIGHT; ty++)
                for (int tx = 0; tx < MAP_WIDTH; tx++)
                    if (world->tiles[ty][tx] == TILE_TELEPORT && (tx != ncx || ty != ncy)) {
                        tele[nt][0] = tx; tele[nt][1] = ty; nt++;
                    }
            if (nt > 0) {
                int r = game_rand() % nt;
                actor->pos.x = tele[r][0] * 10 + 5;
                actor->pos.y = tele[r][1] * 10 + 35;
            }
        } else if (val == TILE_MEDIKIT) {
            // Monsters destroy medikits but don't heal (player-only in Rust)
            world->tiles[ncy][ncx] = TILE_PASSAGE;
        }
    }

    actor->animation = (actor->animation + 1) % 30;
}

static void animate_monsters(World* world, App* app) {
    (void)app;
    if (world->round_counter % 5 == 0) {
        monster_detect_players(world);
    }

    for (int i = world->num_players; i < world->num_actors; i++) {
        Actor* m = &world->actors[i];
        if (!m->is_active || m->is_dead) continue;

        monster_damage_players(world, i);

        if (world->round_counter % monster_speed(m->kind) != 0) {
            animate_monster(world, i);
        }

        if (world->round_counter % 26 == 0) {
            int mcx = m->pos.x / 10;
            int mcy = (m->pos.y - 30) / 10;

            // Look for bombs within 5 cells
            bool found_bomb = false;
            for (int by = mcy - 5; by <= mcy + 5 && !found_bomb; by++) {
                for (int bx = mcx - 5; bx <= mcx + 5 && !found_bomb; bx++) {
                    if (bx >= 0 && bx < MAP_WIDTH && by >= 0 && by < MAP_HEIGHT && is_bomb(world->tiles[by][bx])) {
                        monster_avoid_bomb(m, bx, by, world);
                        found_bomb = true;
                    }
                }
            }

            if (!found_bomb) {
                // Look for closest player within 10 cells
                int best_dist = 999;
                int best_px = -1, best_py = -1;
                for (int p = 0; p < world->num_players; p++) {
                    if (world->actors[p].is_dead) continue;
                    // Clone doesn't chase its owner (or anyone in campaign)
                    if (m->kind == ACTOR_CLONE && (m->clone_owner == p || world->campaign_mode)) continue;
                    int pcx = world->actors[p].pos.x / 10;
                    int pcy = (world->actors[p].pos.y - 30) / 10;
                    int d = abs(mcx - pcx) + abs(mcy - pcy);
                    if (d < best_dist && d <= 10) {
                        best_dist = d;
                        best_px = pcx;
                        best_py = pcy;
                    }
                }
                if (best_px >= 0) {
                    monster_head_to_target(m, best_px, best_py, world);
                    // Clones throw grenades when locked on a target
                    if (m->kind == ACTOR_CLONE) {
                        grenadier_maybe_throw(world, i);
                    }
                } else if (m->kind == ACTOR_CLONE) {
                    // Clones look for gold when no player target
                    int gold_dist = 999;
                    int gold_x = -1, gold_y = -1;
                    for (int gy = mcy - 7; gy <= mcy + 7; gy++) {
                        for (int gx = mcx - 7; gx <= mcx + 7; gx++) {
                            if (gx >= 0 && gx < MAP_WIDTH && gy >= 0 && gy < MAP_HEIGHT && is_treasure(world->tiles[gy][gx])) {
                                int gd = abs(mcx - gx) + abs(mcy - gy);
                                if (gd < gold_dist) {
                                    gold_dist = gd;
                                    gold_x = gx;
                                    gold_y = gy;
                                }
                            }
                        }
                    }
                    if (gold_x >= 0) {
                        monster_head_to_target(m, gold_x, gold_y, world);
                    }
                }

                if (m->kind == ACTOR_GRENADIER) {
                    grenadier_maybe_throw(world, i);
                }
            }
        }

        if ((world->round_counter % 33 == 0 && !monster_can_move(m, world)) || world->round_counter % 121 == 0) {
            Direction dirs[] = {DIR_LEFT, DIR_RIGHT, DIR_UP, DIR_DOWN};
            m->moving = true;
            m->facing = dirs[game_rand() % 4];
        }
    }
}

// ==================== Rendering ====================

static void render_actor(App* app, SDL_Renderer* renderer, Actor* actor, int actor_idx, int num_players) {
    if (actor->is_dead) return;
    int glyph;
    if (actor_idx < num_players) {
        int base = GLYPH_PLAYER_START + actor_idx * 1000 + (actor->is_digging ? 500 : 0);
        static const int phase_map[] = {0, 1, 2, 3, 2, 1};
        int anim_frame = actor->moving ? phase_map[actor->animation / 5] : 0;
        glyph = base + (int)actor->facing + (anim_frame * 4);
    } else if (actor->kind == ACTOR_CLONE) {
        int pi = actor->clone_owner;
        int base = GLYPH_PLAYER_START + pi * 1000 + (actor->is_digging ? 500 : 0);
        static const int phase_map[] = {0, 1, 2, 3, 2, 1};
        int anim_frame = actor->moving ? phase_map[actor->animation / 5] : 0;
        glyph = base + (int)actor->facing + (anim_frame * 4);
    } else {
        int base = (int)monster_glyph_base(actor->kind);
        static const int phase_map[] = {0, 1, 2, 3, 2, 1};
        int anim_frame = actor->moving ? phase_map[actor->animation / 5] : 0;
        glyph = base + (int)actor->facing + (anim_frame * 4);
    }
    glyphs_render(&app->glyphs, renderer, actor->pos.x - 5, actor->pos.y - 5, (GlyphType)glyph);
}

static bool is_stone_like(uint8_t val) {
    return is_stone(val) || (val >= TILE_STONE_TOP_LEFT && val <= TILE_STONE_BOTTOM_RIGHT)
        || val == TILE_STONE_BOTTOM_LEFT || val == TILE_STONE_CRACKED_LIGHT || val == TILE_STONE_CRACKED_HEAVY;
}

// Paint burned border overlays for a cell onto the current render target
static void paint_burned_borders(App* app, SDL_Renderer* renderer, World* world, int x, int y) {
    if (!world->burned[y][x]) return;
    uint8_t val = world->tiles[y][x];
    if (val != TILE_PASSAGE && val != TILE_BLOOD && val != TILE_SLIME_CORPSE
        && val != TILE_EXPLOSION && val != TILE_MONSTER_DYING) return;

    int px = x * 10;
    int py = y * 10 + 30;

    if (x > 0) {
        uint8_t n = world->tiles[y][x-1];
        if (is_sand(n)||n==TILE_GRAVEL_LIGHT||n==TILE_GRAVEL_HEAVY) glyphs_render(&app->glyphs, renderer, px-4, py, GLYPH_BURNED_SAND_R);
        else if (is_stone_like(n)) glyphs_render(&app->glyphs, renderer, px-4, py, GLYPH_BURNED_STONE_R);
    }
    if (x < MAP_WIDTH-1) {
        uint8_t n = world->tiles[y][x+1];
        if (is_sand(n)||n==TILE_GRAVEL_LIGHT||n==TILE_GRAVEL_HEAVY) glyphs_render(&app->glyphs, renderer, px+10, py, GLYPH_BURNED_SAND_L);
        else if (is_stone_like(n)) glyphs_render(&app->glyphs, renderer, px+10, py, GLYPH_BURNED_STONE_L);
    }
    if (y > 0) {
        uint8_t n = world->tiles[y-1][x];
        if (is_sand(n)||n==TILE_GRAVEL_LIGHT||n==TILE_GRAVEL_HEAVY) glyphs_render(&app->glyphs, renderer, px, py-3, GLYPH_BURNED_SAND_D);
        else if (is_stone_like(n)) glyphs_render(&app->glyphs, renderer, px, py-3, GLYPH_BURNED_STONE_D);
    }
    if (y < MAP_HEIGHT-1) {
        uint8_t n = world->tiles[y+1][x];
        if (is_sand(n)||n==TILE_GRAVEL_LIGHT||n==TILE_GRAVEL_HEAVY) glyphs_render(&app->glyphs, renderer, px, py+10, GLYPH_BURNED_SAND_U);
        else if (is_stone_like(n)) glyphs_render(&app->glyphs, renderer, px, py+10, GLYPH_BURNED_STONE_U);
    }
}

// Paint a single map cell (tile + burned overlay) onto the canvas
static void canvas_paint_cell(App* app, SDL_Renderer* renderer, World* world, int x, int y) {
    glyphs_render(&app->glyphs, renderer, x * 10, y * 10 + 30, (GlyphType)(GLYPH_MAP_START + world->tiles[y][x]));
    paint_burned_borders(app, renderer, world, x, y);
    // Also repaint burned borders of neighbors that overlap into this cell
    if (x > 0) paint_burned_borders(app, renderer, world, x - 1, y);
    if (x < MAP_WIDTH - 1) paint_burned_borders(app, renderer, world, x + 1, y);
    if (y > 0) paint_burned_borders(app, renderer, world, x, y - 1);
    if (y < MAP_HEIGHT - 1) paint_burned_borders(app, renderer, world, x, y + 1);
}

// Create canvas and paint the full initial map
static void canvas_init(App* app, ApplicationContext* ctx, World* world) {
    world->canvas = SDL_CreateTexture(ctx->renderer, SDL_PIXELFORMAT_RGBA8888,
        SDL_TEXTUREACCESS_TARGET, SCREEN_WIDTH, SCREEN_HEIGHT);

    SDL_SetRenderTarget(ctx->renderer, world->canvas);
    // Start with black background
    SDL_SetRenderDrawColor(ctx->renderer, 0, 0, 0, 255);
    SDL_RenderClear(ctx->renderer);

    if (world->darkness) {
        // In darkness mode, only paint borders initially
        for (int x = 0; x < MAP_WIDTH; ++x) {
            glyphs_render(&app->glyphs, ctx->renderer, x * 10, 30, (GlyphType)(GLYPH_MAP_START + world->tiles[0][x]));
            glyphs_render(&app->glyphs, ctx->renderer, x * 10, (MAP_HEIGHT - 1) * 10 + 30, (GlyphType)(GLYPH_MAP_START + world->tiles[MAP_HEIGHT - 1][x]));
        }
        for (int y = 0; y < MAP_HEIGHT; ++y) {
            glyphs_render(&app->glyphs, ctx->renderer, 0, y * 10 + 30, (GlyphType)(GLYPH_MAP_START + world->tiles[y][0]));
            glyphs_render(&app->glyphs, ctx->renderer, (MAP_WIDTH - 1) * 10, y * 10 + 30, (GlyphType)(GLYPH_MAP_START + world->tiles[y][MAP_WIDTH - 1]));
        }
    } else {
        // Paint all tiles
        for (int y = 0; y < MAP_HEIGHT; ++y)
            for (int x = 0; x < MAP_WIDTH; ++x)
                glyphs_render(&app->glyphs, ctx->renderer, x * 10, y * 10 + 30, (GlyphType)(GLYPH_MAP_START + world->tiles[y][x]));
    }

    SDL_SetRenderTarget(ctx->renderer, NULL);

    // Snapshot current state for diffing
    memcpy(world->canvas_tiles, world->tiles, sizeof(world->tiles));
    memcpy(world->canvas_fog, world->fog, sizeof(world->fog));
}

// Flush changes: diff current tiles/fog against snapshot, repaint changed cells
static void canvas_flush(App* app, ApplicationContext* ctx, World* world) {
    SDL_SetRenderTarget(ctx->renderer, world->canvas);

    if (world->darkness) {
        // Check for newly revealed fog cells
        for (int y = 1; y < MAP_HEIGHT - 1; ++y) {
            for (int x = 1; x < MAP_WIDTH - 1; ++x) {
                if (world->canvas_fog[y][x] && !world->fog[y][x]) {
                    // Newly revealed: paint the cell
                    canvas_paint_cell(app, ctx->renderer, world, x, y);
                    world->canvas_fog[y][x] = false;
                    world->canvas_tiles[y][x] = world->tiles[y][x];
                } else if (!world->fog[y][x] && world->canvas_tiles[y][x] != world->tiles[y][x]) {
                    // Already revealed but tile changed
                    canvas_paint_cell(app, ctx->renderer, world, x, y);
                    world->canvas_tiles[y][x] = world->tiles[y][x];
                }
            }
        }
        // Border tiles can also change
        for (int x = 0; x < MAP_WIDTH; ++x) {
            if (world->canvas_tiles[0][x] != world->tiles[0][x]) {
                glyphs_render(&app->glyphs, ctx->renderer, x * 10, 30, (GlyphType)(GLYPH_MAP_START + world->tiles[0][x]));
                world->canvas_tiles[0][x] = world->tiles[0][x];
            }
            if (world->canvas_tiles[MAP_HEIGHT-1][x] != world->tiles[MAP_HEIGHT-1][x]) {
                glyphs_render(&app->glyphs, ctx->renderer, x * 10, (MAP_HEIGHT-1)*10+30, (GlyphType)(GLYPH_MAP_START + world->tiles[MAP_HEIGHT-1][x]));
                world->canvas_tiles[MAP_HEIGHT-1][x] = world->tiles[MAP_HEIGHT-1][x];
            }
        }
        for (int y = 0; y < MAP_HEIGHT; ++y) {
            if (world->canvas_tiles[y][0] != world->tiles[y][0]) {
                glyphs_render(&app->glyphs, ctx->renderer, 0, y*10+30, (GlyphType)(GLYPH_MAP_START + world->tiles[y][0]));
                world->canvas_tiles[y][0] = world->tiles[y][0];
            }
            if (world->canvas_tiles[y][MAP_WIDTH-1] != world->tiles[y][MAP_WIDTH-1]) {
                glyphs_render(&app->glyphs, ctx->renderer, (MAP_WIDTH-1)*10, y*10+30, (GlyphType)(GLYPH_MAP_START + world->tiles[y][MAP_WIDTH-1]));
                world->canvas_tiles[y][MAP_WIDTH-1] = world->tiles[y][MAP_WIDTH-1];
            }
        }
    } else {
        // Non-darkness: just diff tiles
        for (int y = 0; y < MAP_HEIGHT; ++y) {
            for (int x = 0; x < MAP_WIDTH; ++x) {
                if (world->canvas_tiles[y][x] != world->tiles[y][x]) {
                    canvas_paint_cell(app, ctx->renderer, world, x, y);
                    world->canvas_tiles[y][x] = world->tiles[y][x];
                }
            }
        }
    }

    SDL_SetRenderTarget(ctx->renderer, NULL);
}

static void render_world(App* app, ApplicationContext* ctx, World* world) {
    // Flush tile/fog changes to the off-screen canvas
    canvas_flush(app, ctx, world);

    // Compose final frame onto the presentation buffer
    SDL_SetRenderTarget(ctx->renderer, ctx->buffer);

    // HUD background
    SDL_RenderCopy(ctx->renderer, app->players.texture, NULL, NULL);

    // Black out unused player slots
    SDL_Color yellow = {255, 255, 0, 255}, cyan = {0, 255, 255, 255};
    static const int PLAYER_X[] = {12, 174, 337, 500};
    static const int HEALTH_BAR_LEFT[] = {142, 304, 467, 630};
    if (world->num_players < 4) {
        SDL_SetRenderDrawColor(ctx->renderer, 0, 0, 0, 255);
        for (int p = world->num_players; p < 4; p++) {
            SDL_Rect r = {PLAYER_X[p] - 12, 0, 162, 30};
            SDL_RenderFillRect(ctx->renderer, &r);
        }
    }

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
        snprintf(cash_str, sizeof(cash_str), "%d", app->player_cash[p] + world->cash_earned[p]);
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

    if (world->campaign_mode) {
        int lives = app->player_lives + world->lives_gained;
        int show = lives < 3 ? 3 : lives;
        int lx = 160 * world->num_players;
        SDL_SetRenderDrawColor(ctx->renderer, 0, 0, 0, 255);
        SDL_Rect lives_bg = {lx, 0, 640 - lx, 30};
        SDL_RenderFillRect(ctx->renderer, &lives_bg);
        for (int i = 0; i < show; i++) {
            GlyphType g = (i < lives) ? GLYPH_LIFE : GLYPH_LIFE_LOST;
            glyphs_render(&app->glyphs, ctx->renderer, i * 16 + lx, 2, g);
        }
    }

    // Copy the map canvas (map area only: y=30..30+MAP_HEIGHT*10)
    SDL_Rect map_area = {0, 30, SCREEN_WIDTH, MAP_HEIGHT * 10};
    SDL_RenderCopy(ctx->renderer, world->canvas, &map_area, &map_area);

    // Render actors on top of the canvas
    if (world->darkness) {
        // Monsters only in revealed cells, players always visible
        for (int i = world->num_players; i < world->num_actors; ++i) {
            Actor* a = &world->actors[i];
            if (!a->is_dead) {
                int acx = a->pos.x / 10;
                int acy = (a->pos.y - 30) / 10;
                if (acx >= 0 && acx < MAP_WIDTH && acy >= 0 && acy < MAP_HEIGHT && !world->fog[acy][acx])
                    render_actor(app, ctx->renderer, a, i, world->num_players);
            }
        }
        for (int i = 0; i < world->num_players; ++i) {
            Actor* a = &world->actors[i];
            if (!a->is_dead)
                render_actor(app, ctx->renderer, a, i, world->num_players);
        }
    } else {
        // Monsters first, then players on top
        for (int i = world->num_players; i < world->num_actors; ++i)
            render_actor(app, ctx->renderer, &world->actors[i], i, world->num_players);
        for (int i = 0; i < world->num_players; ++i)
            render_actor(app, ctx->renderer, &world->actors[i], i, world->num_players);
    }

    SDL_SetRenderTarget(ctx->renderer, NULL);
}

// ==================== Player interaction with map tiles ====================

static void player_interact_tile(App* app, World* world, int p, int ncx, int ncy) {
    uint8_t val = world->tiles[ncy][ncx];

    // Treasures — accumulate in cash_earned, committed to player_cash at end of round
    if (val >= TILE_GOLD_SHIELD && val <= TILE_GOLD_CROWN) {
        int v = get_treasure_value(val);
        if (p < MAX_PLAYERS) { world->cash_earned[p] += v; world->treasures_collected[p]++; }
        world->tiles[ncy][ncx] = TILE_PASSAGE;
        context_play_sample_freq(app->sound_kili, 10000 + (game_rand() % 5000));
        return;
    }
    if (val == TILE_DIAMOND) {
        if (p < MAX_PLAYERS) { world->cash_earned[p] += 1000; world->treasures_collected[p]++; }
        world->tiles[ncy][ncx] = TILE_PASSAGE;
        context_play_sample_freq(app->sound_kili, 10000 + (game_rand() % 5000));
        return;
    }

    // Pickaxe/drill items
    if (val == TILE_SMALL_PICKAXE) { world->actors[p].drilling += 1; world->tiles[ncy][ncx] = TILE_PASSAGE; context_play_sample(app->sound_picaxe); return; }
    if (val == TILE_LARGE_PICKAXE) { world->actors[p].drilling += 3; world->tiles[ncy][ncx] = TILE_PASSAGE; context_play_sample(app->sound_picaxe); return; }
    if (val == TILE_DRILL) { world->actors[p].drilling += 5; world->tiles[ncy][ncx] = TILE_PASSAGE; context_play_sample(app->sound_picaxe); return; }

    // Medikit
    if (val == TILE_MEDIKIT) {
        world->actors[p].health = world->actors[p].max_health;
        world->tiles[ncy][ncx] = TILE_PASSAGE;
        context_play_sample(app->sound_picaxe);
        return;
    }

    // Weapons crate - 3 categories: rare (1/5), medium (1/5), common (3/5)
    if (val == TILE_WEAPONS_CRATE) {
        int cat = game_rand() % 5;
        if (cat == 0) {
            // Rare: 1-2 of atomic/grenade/flamethrower/clone
            int cnt = 1 + game_rand() % 2;
            int weaps[] = {EQUIP_ATOMIC_BOMB, EQUIP_GRENADE, EQUIP_FLAMETHROWER, EQUIP_CLONE};
            app->player_inventory[p][weaps[game_rand() % 4]] += cnt;
        } else if (cat == 1) {
            // Medium: 1-5 of napalm/large_crucifix/teleport/biomass/extinguisher/jumping_bomb/super_drill
            int cnt = 1 + game_rand() % 5;
            int weaps[] = {EQUIP_NAPALM, EQUIP_LARGE_CRUCIFIX, EQUIP_TELEPORT, EQUIP_BIOMASS,
                           EQUIP_EXTINGUISHER, EQUIP_JUMPING_BOMB, EQUIP_SUPER_DRILL};
            app->player_inventory[p][weaps[game_rand() % 7]] += cnt;
        } else {
            // Common: 3-12 of basic weapons
            int cnt = 3 + game_rand() % 10;
            int weaps[] = {EQUIP_SMALL_BOMB, EQUIP_BIG_BOMB, EQUIP_DYNAMITE, EQUIP_SMALL_RADIO,
                           EQUIP_LARGE_RADIO, EQUIP_MINE, EQUIP_BARREL, EQUIP_SMALL_CRUCIFIX,
                           EQUIP_PLASTIC, EQUIP_EXPLOSIVE_PLASTIC, EQUIP_DIGGER, EQUIP_METAL_WALL};
            app->player_inventory[p][weaps[game_rand() % 12]] += cnt;
        }
        world->tiles[ncy][ncx] = TILE_PASSAGE;
        context_play_sample(app->sound_picaxe);
        return;
    }

    // Life item (campaign only)
    if (val == TILE_LIFE_ITEM) {
        if (world->campaign_mode) world->lives_gained++;
        world->tiles[ncy][ncx] = TILE_PASSAGE;
        context_play_sample(app->sound_picaxe);
        return;
    }
}

// ==================== Player action helpers (shared by local + net) ====================

static void apply_game_action(App* app, World* world, int p) {
    Actor* actor = &world->actors[p];
    if (actor->is_dead) return;
    int cx = actor->pos.x / 10;
    int cy = (actor->pos.y - 30) / 10;
    if (cx < 0 || cx >= MAP_WIDTH || cy < 0 || cy >= MAP_HEIGHT) return;
    int w = actor->selected_weapon;
    if (w == EQUIP_FLAMETHROWER && app->player_inventory[p][w] > 0) {
        app->player_inventory[p][w]--;
        activate_flamethrower(world, cx, cy, actor->facing, cx, cy);
        if (app->sound_explos4) context_play_sample_freq(app->sound_explos4, 11000);
    } else if (w == EQUIP_EXTINGUISHER && app->player_inventory[p][w] > 0) {
        app->player_inventory[p][w]--;
        int edx = (actor->facing == DIR_RIGHT) ? 1 : (actor->facing == DIR_LEFT) ? -1 : 0;
        int edy = (actor->facing == DIR_DOWN) ? 1 : (actor->facing == DIR_UP) ? -1 : 0;
        int fx = cx + edx, fy = cy + edy;
        for (int i = 0; i < 6; i++) {
            if (fx < 0 || fx >= MAP_WIDTH || fy < 0 || fy >= MAP_HEIGHT) break;
            uint8_t ev = world->tiles[fy][fx];
            if (is_bomb(ev) && ev != TILE_GRENADE_FLY_R && ev != TILE_GRENADE_FLY_L
                && ev != TILE_GRENADE_FLY_U && ev != TILE_GRENADE_FLY_D) {
                world->timer[fy][fx] = 0;
                world->hits[fy][fx] = 20;
                if (ev == TILE_DYNAMITE1 || ev == TILE_DYNAMITE2 || ev == TILE_DYNAMITE3)
                    world->tiles[fy][fx] = TILE_DYNAMITE_EXTINGUISHED;
                else if (ev == TILE_BIG_BOMB1 || ev == TILE_BIG_BOMB2 || ev == TILE_BIG_BOMB3)
                    world->tiles[fy][fx] = TILE_BIG_BOMB_EXTINGUISHED;
                else if (ev == TILE_SMALL_BOMB1 || ev == TILE_SMALL_BOMB2 || ev == TILE_SMALL_BOMB3)
                    world->tiles[fy][fx] = TILE_SMALL_BOMB_EXTINGUISHED;
                else if (ev == TILE_NAPALM1 || ev == TILE_NAPALM2)
                    world->tiles[fy][fx] = TILE_NAPALM_EXTINGUISHED;
            } else if (is_passable(ev)) {
                world->tiles[fy][fx] = TILE_SMOKE1;
                world->timer[fy][fx] = 3;
            } else {
                break;
            }
            fx += edx; fy += edy;
        }
    } else if (w == EQUIP_ARMOR) {
        // Armor is passive
    } else if (w == EQUIP_SUPER_DRILL && app->player_inventory[p][w] > 0 && actor->super_drill_count == 0) {
        app->player_inventory[p][w]--;
        actor->super_drill_count = 10;
        actor->drilling += 300;
    } else if (w == EQUIP_CLONE && app->player_inventory[p][w] > 0 && world->num_actors < MAX_ACTORS) {
        app->player_inventory[p][w]--;
        Actor* clone = &world->actors[world->num_actors];
        memset(clone, 0, sizeof(Actor));
        clone->kind = ACTOR_CLONE;
        clone->clone_owner = p;
        clone->facing = DIR_RIGHT;
        clone->moving = true;
        clone->max_health = 100;
        clone->health = 100;
        clone->pos.x = actor->pos.x / 10 * 10 + 5;
        clone->pos.y = (actor->pos.y - 30) / 10 * 10 + 35;
        clone->drilling = actor->drilling;
        if (actor->super_drill_count > 0) clone->drilling -= 300;
        clone->animation = 1;
        clone->is_active = true;
        world->num_actors++;
    } else if (app->player_inventory[p][w] > 0 && (world->tiles[cy][cx] == TILE_PASSAGE || is_treasure(world->tiles[cy][cx]))) {
        uint8_t tile = 0; int timer = 0; bool place = true;
        switch (w) {
            case EQUIP_SMALL_BOMB: tile = TILE_SMALL_BOMB1; timer = 100; break;
            case EQUIP_BIG_BOMB: tile = TILE_BIG_BOMB1; timer = 100; break;
            case EQUIP_DYNAMITE: tile = TILE_DYNAMITE1; timer = 80; break;
            case EQUIP_ATOMIC_BOMB: tile = TILE_ATOMIC1; timer = 280; break;
            case EQUIP_SMALL_RADIO:
                switch (p) {
                    case 0: tile = TILE_SMALL_RADIO_BLUE; break;
                    case 1: tile = TILE_SMALL_RADIO_RED; break;
                    case 2: tile = TILE_SMALL_RADIO_GREEN; break;
                    case 3: tile = TILE_SMALL_RADIO_YELLOW; break;
                }
                timer = 0; break;
            case EQUIP_LARGE_RADIO:
                switch (p) {
                    case 0: tile = TILE_BIG_RADIO_BLUE; break;
                    case 1: tile = TILE_BIG_RADIO_RED; break;
                    case 2: tile = TILE_BIG_RADIO_GREEN; break;
                    case 3: tile = TILE_BIG_RADIO_YELLOW; break;
                }
                timer = 0; break;
            case EQUIP_GRENADE:
                switch (actor->facing) {
                    case DIR_RIGHT: tile = TILE_GRENADE_FLY_R; break;
                    case DIR_LEFT:  tile = TILE_GRENADE_FLY_L; break;
                    case DIR_UP:    tile = TILE_GRENADE_FLY_U; break;
                    case DIR_DOWN:  tile = TILE_GRENADE_FLY_D; break;
                }
                timer = 1; break;
            case EQUIP_MINE: tile = TILE_MINE; timer = 0; break;
            case EQUIP_NAPALM: tile = TILE_NAPALM1; timer = 260; break;
            case EQUIP_BARREL: tile = TILE_BARREL; timer = 0; break;
            case EQUIP_SMALL_CRUCIFIX: tile = TILE_SMALL_CRUCIFIX_BOMB; timer = 100; break;
            case EQUIP_LARGE_CRUCIFIX: tile = TILE_LARGE_CRUCIFIX_BOMB; timer = 100; break;
            case EQUIP_PLASTIC:
                tile = TILE_PLASTIC_BOMB; timer = 100;
                if (app->sound_urethan) context_play_sample_freq(app->sound_urethan, 11000);
                break;
            case EQUIP_EXPLOSIVE_PLASTIC:
                tile = TILE_EXPLOSIVE_PLASTIC_BOMB; timer = 90;
                if (app->sound_urethan) context_play_sample_freq(app->sound_urethan, 11000);
                break;
            case EQUIP_DIGGER: tile = TILE_DIGGER_BOMB; timer = 100; break;
            case EQUIP_METAL_WALL: tile = TILE_METAL_WALL_PLACED; timer = 1; break;
            case EQUIP_TELEPORT: tile = TILE_TELEPORT; timer = 0; break;
            case EQUIP_BIOMASS:
                tile = TILE_BIOMASS; timer = game_rand() % 80;
                if (app->sound_urethan) context_play_sample_freq(app->sound_urethan, 11000);
                break;
            case EQUIP_JUMPING_BOMB:
                tile = TILE_JUMPING_BOMB; timer = 80 + game_rand() % 80;
                break;
            default: place = false; break;
        }
        if (place) {
            world->tiles[cy][cx] = tile;
            world->timer[cy][cx] = timer;
            if (w == EQUIP_JUMPING_BOMB) world->hits[cy][cx] = 5;
            if (w == EQUIP_BIOMASS) world->hits[cy][cx] = 400;
            app->player_inventory[p][w]--;
            if (p < MAX_PLAYERS) world->bombs_dropped[p]++;
        }
    }
}

static void apply_game_cycle(App* app, World* world, int p) {
    Actor* actor = &world->actors[p];
    if (actor->is_dead) return;
    for (int i = 1; i < EQUIP_TOTAL; ++i) {
        int w = (actor->selected_weapon + i) % EQUIP_TOTAL;
        if (w == EQUIP_SMALL_PICKAXE || w == EQUIP_LARGE_PICKAXE || w == EQUIP_DRILL || w == EQUIP_ARMOR) continue;
        if (app->player_inventory[p][w] > 0) {
            actor->selected_weapon = w;
            break;
        }
    }
}

static void apply_game_remote(World* world, int p) {
    for (int ry = 0; ry < MAP_HEIGHT; ry++)
        for (int rx = 0; rx < MAP_WIDTH; rx++)
            if (is_radio_for(world->tiles[ry][rx], p))
                world->timer[ry][rx] = 1;
}

// Shared input application: takes packed input flags and applies to actor/world.
// Input flag format matches NET_INPUT_* constants (direction in low 3 bits, actions in higher bits).
static void apply_player_input(App* app, World* world, int p, uint8_t input) {
    Actor* actor = &world->actors[p];
    if (!actor->is_dead) {
        int dir = input & NET_INPUT_DIR_MASK;
        if (dir == NET_INPUT_UP)         { actor->facing = DIR_UP;    actor->moving = true; }
        else if (dir == NET_INPUT_DOWN)  { actor->facing = DIR_DOWN;  actor->moving = true; }
        else if (dir == NET_INPUT_LEFT)  { actor->facing = DIR_LEFT;  actor->moving = true; }
        else if (dir == NET_INPUT_RIGHT) { actor->facing = DIR_RIGHT; actor->moving = true; }
        else if (dir == NET_INPUT_STOP)  { actor->moving = false; }
        if (input & NET_INPUT_ACTION) apply_game_action(app, world, p);
        if (input & NET_INPUT_CYCLE)  apply_game_cycle(app, world, p);
        if (input & NET_INPUT_REMOTE) apply_game_remote(world, p);
    }
}

// Convert an ActionType from the input system to the packed input flag format.
static uint8_t action_to_input_flag(ActionType act) {
    switch (act) {
        case ACT_UP:     return NET_INPUT_UP;
        case ACT_DOWN:   return NET_INPUT_DOWN;
        case ACT_LEFT:   return NET_INPUT_LEFT;
        case ACT_RIGHT:  return NET_INPUT_RIGHT;
        case ACT_STOP:   return NET_INPUT_STOP;
        case ACT_ACTION: return NET_INPUT_ACTION;
        case ACT_CYCLE:  return NET_INPUT_CYCLE;
        case ACT_REMOTE: return NET_INPUT_REMOTE;
        default:         return 0;
    }
}

#ifdef MB_NET
// ==================== Netgame pause overlay for clients ====================

static void net_pause_overlay(App* app, ApplicationContext* ctx) {
    SDL_SetRenderTarget(ctx->renderer, ctx->buffer);
    SDL_SetRenderDrawBlendMode(ctx->renderer, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(ctx->renderer, 0, 0, 0, 180);
    SDL_Rect bg = {170, 210, 300, 60};
    SDL_RenderFillRect(ctx->renderer, &bg);
    SDL_SetRenderDrawColor(ctx->renderer, 200, 200, 200, 255);
    SDL_RenderDrawRect(ctx->renderer, &bg);
    SDL_Color white = {255, 255, 255, 255};
    render_text(ctx->renderer, &app->font, 200, 230, white, "HOST HAS PAUSED THE GAME");
    SDL_SetRenderTarget(ctx->renderer, NULL);
    context_present(ctx);
}

// ==================== Netgame input exchange ====================

static bool net_exchange_inputs(App* app, ApplicationContext* ctx,
                                NetContext* net, uint8_t local_input,
                                uint8_t all_inputs[NET_MAX_PLAYERS], uint32_t frame,
                                Uint32 frame_start, int tick_ms) {
    if (net->is_server) {
        all_inputs[net->local_player] = local_input;
        bool received[NET_MAX_PLAYERS] = {false};
        received[net->local_player] = true;

        Uint32 start = SDL_GetTicks();
        for (;;) {
            bool have_all = true;
            for (int s = 0; s < NET_MAX_PLAYERS; s++) {
                if (net_slot_active(net, s) && !received[s]) { have_all = false; break; }
            }
            if (have_all) break;
            if (SDL_GetTicks() - start > 2000) return false;

            NetMessage msg;
            ENetPeer* from;
            while (net_poll(net, &msg, &from) > 0) {
                if (msg.type == NET_MSG_GAME_INPUT) {
                    int pi = msg.data.game_input.player_index;
                    if (pi >= 0 && pi < NET_MAX_PLAYERS) {
                        all_inputs[pi] = msg.data.game_input.input;
                        received[pi] = true;
                    }
                } else if (msg.type == NET_MSG_PLAYER_LEAVE) {
                    return false;
                }
            }
            SDL_Delay(1);
        }

        // Server paces the game: wait until tick_ms since frame_start
        Uint32 elapsed = SDL_GetTicks() - frame_start;
        if ((int)elapsed < tick_ms)
            SDL_Delay((Uint32)(tick_ms - (int)elapsed));

        // Broadcast combined tick to all clients
        NetMessage tick = {0};
        tick.type = NET_MSG_GAME_TICK;
        tick.data.game_tick.frame = frame;
        memcpy(tick.data.game_tick.inputs, all_inputs, NET_MAX_PLAYERS);
        net_broadcast(net, &tick);
        net_flush(net);
        return true;
    } else {
        // Client: send input to server
        NetMessage msg = {0};
        msg.type = NET_MSG_GAME_INPUT;
        msg.data.game_input.frame = frame;
        msg.data.game_input.player_index = net->local_player;
        msg.data.game_input.input = local_input;
        net_send_to(net->server_peer, &msg);
        net_flush(net);

        // Wait for tick from server (server paces the game)
        Uint32 start = SDL_GetTicks();
        while (SDL_GetTicks() - start < 2000) {
            NetMessage recv;
            ENetPeer* from;
            while (net_poll(net, &recv, &from) > 0) {
                if (recv.type == NET_MSG_GAME_TICK) {
                    memcpy(all_inputs, recv.data.game_tick.inputs, NET_MAX_PLAYERS);
                    return true;
                } else if (recv.type == NET_MSG_PLAYER_LEAVE) {
                    return false;
                } else if (recv.type == NET_MSG_PAUSE && recv.data.pause.paused) {
                    // Host paused: show overlay, wait for unpause
                    net_pause_overlay(app, ctx);
                    bool paused = true;
                    while (paused) {
                        SDL_Event pe;
                        while (SDL_PollEvent(&pe)) { /* drain events */ }
                        NetMessage pr; ENetPeer* pf;
                        while (net_poll(net, &pr, &pf) > 0) {
                            if (pr.type == NET_MSG_PAUSE && !pr.data.pause.paused) paused = false;
                            else if (pr.type == NET_MSG_PLAYER_LEAVE) return false;
                        }
                        SDL_Delay(16);
                    }
                    start = SDL_GetTicks(); // reset timeout after unpause
                }
            }
            SDL_Delay(1);
        }
        return false;
    }
}
#endif /* MB_NET */

// ==================== Main game loop ====================

RoundResult game_run(App* app, ApplicationContext* ctx, uint8_t* level_data, NetContext* net) {
    // For local play, seed PRNG with time. For netgame, caller seeds before the session.
#ifdef MB_NET
    if (!net) {
        game_seed_rng((uint32_t)SDL_GetTicks());
    }
#else
    game_seed_rng((uint32_t)SDL_GetTicks());
#endif

    int tracks[] = {0, 39, 55};
    context_play_music_at(ctx, "OEKU.S3M", tracks[game_rand() % 3]);

#ifdef MB_NET
    bool campaign = net ? false : (app->options.players == 1);
    int num_players = net ? app->options.players : (campaign ? 1 : app->options.players);
#else
    bool campaign = (app->options.players == 1);
    int num_players = campaign ? 1 : app->options.players;
#endif

    World world;
    game_init_world(&world, level_data, num_players);
    world.campaign_mode = campaign;
    world.bomb_damage = campaign ? 100 : app->options.bomb_damage;
    world.darkness = campaign ? true : app->options.darkness;
    if (world.darkness) {
        memset(world.fog, true, sizeof(world.fog));
    }
    if (campaign) {
        randomize_exits(&world);
    }

    // Apply equipment bonuses from inventory
    for (int p = 0; p < world.num_players; p++) {
        int armor = app->player_inventory[p][EQUIP_ARMOR];
        world.actors[p].max_health = 100 + 100 * armor;
        world.actors[p].health = world.actors[p].max_health;
        world.actors[p].drilling = 1 + app->player_inventory[p][EQUIP_SMALL_PICKAXE]
            + 3 * app->player_inventory[p][EQUIP_LARGE_PICKAXE]
            + 5 * app->player_inventory[p][EQUIP_DRILL];
    }

    // Clear player start tiles to passage and collect any items there
    for (int p = 0; p < world.num_players; p++) {
        int sx = world.actors[p].pos.x / 10;
        int sy = (world.actors[p].pos.y - 30) / 10;
        if (sx >= 0 && sx < MAP_WIDTH && sy >= 0 && sy < MAP_HEIGHT) {
            player_interact_tile(app, &world, p, sx, sy);
            world.tiles[sy][sx] = TILE_PASSAGE;
            world.hits[sy][sx] = 0;
        }
    }

    Uint32 round_start = SDL_GetTicks();
    Uint32 round_time_ms = (Uint32)app->options.round_time_secs * 1000;

    canvas_init(app, ctx, &world);

    // Frame timing: base tick = 20ms at 100% speed
    // speed 50% = 40ms ticks (half speed), 200% = 10ms ticks (double speed)
    int speed_pct = (int)app->options.speed;
    if (speed_pct < 50) speed_pct = 50;
    int tick_ms = 2000 / speed_pct;
    if (tick_ms < 10) tick_ms = 10;

    bool running = true, quit_requested = false;
    explosion_app = app;
    (void)net; // may be unused when MB_NET is not defined
    while (running) {
        Uint32 frame_start = SDL_GetTicks();
#ifdef MB_NET
        if (net) {
            // === NETGAME: lockstep input ===
            uint8_t local_input = 0;
            bool want_pause = false;
            SDL_Event e;
            while (SDL_PollEvent(&e)) {
                if (e.type == SDL_QUIT) { local_input |= NET_INPUT_QUIT; break; }
                if (is_pause_event(&e, &app->input_config)) {
                    if (net->is_server) want_pause = true;
                    continue;
                }
                ActionType act = input_map_event(&e, 0, &app->input_config);
                if (act != ACT_MAX_PLAYER && act != ACT_PAUSE) {
                    uint8_t flags = action_to_input_flag(act);
                    // Direction flags replace previous direction; action flags accumulate
                    if (flags && flags <= NET_INPUT_STOP)
                        local_input = (local_input & ~NET_INPUT_DIR_MASK) | flags;
                    else
                        local_input |= flags;
                }
            }

            // Host pause: broadcast pause, show menu, broadcast unpause
            if (want_pause && net->is_server) {
                NetMessage pmsg = {0}; pmsg.type = NET_MSG_PAUSE; pmsg.data.pause.paused = true;
                net_broadcast(net, &pmsg); net_flush(net);

                PauseChoice pc = pause_menu_net(app, ctx, PAUSE_CTX_GAMEPLAY, net);

                pmsg.data.pause.paused = false;
                net_broadcast(net, &pmsg); net_flush(net);

                if (pc == PAUSE_EXIT_LEVEL) { running = false; continue; }
                else if (pc == PAUSE_END_GAME) { running = false; quit_requested = true; continue; }
                // Adjust frame_start to exclude pause time
                frame_start = SDL_GetTicks();
            }

            uint8_t all_inputs[NET_MAX_PLAYERS] = {0};
            if (!net_exchange_inputs(app, ctx, net, local_input, all_inputs, (uint32_t)world.round_counter, frame_start, tick_ms)) {
                running = false;
                quit_requested = true;
            } else {
                for (int p = 0; p < world.num_players; p++) {
                    if (all_inputs[p] & NET_INPUT_QUIT) { running = false; quit_requested = true; }
                    apply_player_input(app, &world, p, all_inputs[p]);
                }
            }
        } else
#endif /* MB_NET */
        {
            // === LOCAL PLAY ===
            SDL_Event e;
            while (SDL_PollEvent(&e)) {
                if (e.type == SDL_QUIT) { running = false; quit_requested = true; break; }

                if (is_pause_event(&e, &app->input_config)) {
                    PauseChoice pc = pause_menu(app, ctx, PAUSE_CTX_GAMEPLAY);
                    if (pc == PAUSE_EXIT_LEVEL) { running = false; }
                    else if (pc == PAUSE_END_GAME) { running = false; quit_requested = true; }
                    continue;
                }

                for (int p = 0; p < world.num_players; ++p) {
                    ActionType act = input_map_event(&e, p, &app->input_config);
                    if (act != ACT_MAX_PLAYER && act != ACT_PAUSE) {
                        uint8_t flags = action_to_input_flag(act);
                        apply_player_input(app, &world, p, flags);
                    }
                }
            }
        }

        // Check deaths (players)
        int alive_count = 0;
        for (int p = 0; p < world.num_players; p++) {
            Actor* actor = &world.actors[p];
            if (actor->is_dead && actor->health == 0) {
                actor->health = -1;
                world.deaths[p]++;
                int cx = actor->pos.x / 10;
                int cy = (actor->pos.y - 30) / 10;
                if (cx >= 0 && cx < MAP_WIDTH && cy >= 0 && cy < MAP_HEIGHT) world.tiles[cy][cx] = TILE_BLOOD;
                context_play_sample(app->sound_aargh);
            }
            if (!actor->is_dead) alive_count++;
        }

        // Check deaths (monsters)
        for (int i = world.num_players; i < world.num_actors; i++) {
            Actor* m = &world.actors[i];
            if (m->is_dead && m->health == 0) {
                m->health = -1;
                int cx = m->pos.x / 10;
                int cy = (m->pos.y - 30) / 10;
                if (cx >= 0 && cx < MAP_WIDTH && cy >= 0 && cy < MAP_HEIGHT) {
                    if (m->kind == ACTOR_SLIME) {
                        world.tiles[cy][cx] = TILE_SLIME_DYING;
                    } else {
                        world.tiles[cy][cx] = TILE_MONSTER_DYING;
                        world.burned[cy][cx] = 1;
                    }
                    world.timer[cy][cx] = 3;
                }
                if (m->kind == ACTOR_SLIME) {
                    // Play slime death sound (reuse aargh for now)
                    context_play_sample(app->sound_aargh);
                } else {
                    context_play_sample(app->sound_aargh);
                }
            }
        }

        // Round end conditions (matching Rust: counter increments, ends at > 100)
        if (world.exited) {
            running = false;
        } else if (world.round_counter % 5 == 0) {
            if (world.campaign_mode) {
                if (alive_count == 0) world.round_end_timer += 2;
            } else if (alive_count < 2) {
                world.round_end_timer += 3;
            }
        }
        // All gold collected ends multiplayer round
        if (world.round_counter % 20 == 0 && !world.campaign_mode) {
            bool has_gold = false;
            for (int y = 0; y < MAP_HEIGHT && !has_gold; y++)
                for (int x = 0; x < MAP_WIDTH && !has_gold; x++)
                    if (is_treasure(world.tiles[y][x])) has_gold = true;
            if (!has_gold) world.round_end_timer += 20;
        }
        if (world.round_end_timer > 100) running = false;

        // Player movement (super drill = double speed via 2 iterations)
        for (int p = 0; p < world.num_players; p++) {
          int move_iters = (world.actors[p].super_drill_count > 0) ? 2 : 1;
          for (int mi = 0; mi < move_iters; mi++) {
            Actor* actor = &world.actors[p];
            actor->is_digging = false;
            if (actor->moving && !actor->is_dead) {
                int dx = actor->pos.x % 10;
                int dy = (actor->pos.y - 30) % 10;
                int cx = actor->pos.x / 10;
                int cy = (actor->pos.y - 30) / 10;

                int d_dir = 0, d_ortho = 0;
                bool finishing = false;

                bool can_move = true;
                switch (actor->facing) {
                    case DIR_LEFT:  d_dir = dx; d_ortho = dy; finishing = dx > 5; can_move = actor->pos.x > 5; break;
                    case DIR_RIGHT: d_dir = dx; d_ortho = dy; finishing = dx < 5; can_move = actor->pos.x < 635; break;
                    case DIR_UP:    d_dir = dy; d_ortho = dx; finishing = dy > 5; can_move = actor->pos.y > 35; break;
                    case DIR_DOWN:  d_dir = dy; d_ortho = dx; finishing = dy < 5; can_move = actor->pos.y < 475; break;
                    default: break;
                }

                int ncx = cx + (actor->facing == DIR_RIGHT ? 1 : (actor->facing == DIR_LEFT ? -1 : 0));
                int ncy = cy + (actor->facing == DIR_DOWN ? 1 : (actor->facing == DIR_UP ? -1 : 0));

                // Step forward if possible (matching Rust animate_actor)
                bool is_moving = can_move && d_ortho > 3 && d_ortho < 6;
                uint8_t map_value = (ncx >= 0 && ncx < MAP_WIDTH && ncy >= 0 && ncy < MAP_HEIGHT) ? world.tiles[ncy][ncx] : TILE_WALL;
                if (is_moving && (finishing || is_passable(map_value))) {
                    if (actor->facing == DIR_LEFT) actor->pos.x--;
                    else if (actor->facing == DIR_RIGHT) actor->pos.x++;
                    else if (actor->facing == DIR_UP) actor->pos.y--;
                    else if (actor->facing == DIR_DOWN) actor->pos.y++;
                    world.meters_ran[p]++;
                }

                // Center orthogonal position
                if (d_ortho != 5) {
                    if (actor->facing == DIR_UP || actor->facing == DIR_DOWN) actor->pos.x = (actor->pos.x / 10) * 10 + 5;
                    else actor->pos.y = ((actor->pos.y - 30) / 10) * 10 + 35;
                }

                // Interact with next cell when centered in direction of travel (Rust: interact_map)
                if (d_dir == 5 && ncx >= 0 && ncx < MAP_WIDTH && ncy >= 0 && ncy < MAP_HEIGHT) {
                    uint8_t target = world.tiles[ncy][ncx];

                    if (is_passable(target)) {
                        // Reveal fog in darkness mode
                        if (world.darkness) {
                            int pcx = actor->pos.x / 10;
                            int pcy = (actor->pos.y - 30) / 10;
                            reveal_view(&world, pcx, pcy, actor->facing);
                        }
                    } else if (is_treasure(target) || (target >= TILE_SMALL_PICKAXE && target <= TILE_DRILL)) {
                        // Gold, diamonds, pickaxes, drills — collected instantly
                        player_interact_tile(app, &world, p, ncx, ncy);
                    } else if (target == TILE_WEAPONS_CRATE || target == TILE_MEDIKIT || target == TILE_LIFE_ITEM) {
                        // Items collected instantly
                        player_interact_tile(app, &world, p, ncx, ncy);
                    } else if (target == TILE_MINE) {
                        // Activate mine
                        world.timer[ncy][ncx] = 1;
                    } else if (target == TILE_TELEPORT) {
                        // Teleporter — move to a random other teleporter
                        int tele[256][2], nt = 0;
                        for (int ty = 0; ty < MAP_HEIGHT; ty++)
                            for (int tx = 0; tx < MAP_WIDTH; tx++)
                                if (world.tiles[ty][tx] == TILE_TELEPORT && (tx != ncx || ty != ncy)) {
                                    tele[nt][0] = tx; tele[nt][1] = ty; nt++;
                                }
                        if (nt > 0) {
                            int r = game_rand() % nt;
                            actor->pos.x = tele[r][0] * 10 + 5;
                            actor->pos.y = tele[r][1] * 10 + 35;
                            actor->moving = false;
                            context_play_sample(app->sound_kili);
                        }
                    } else if (target == TILE_EXIT && world.campaign_mode) {
                        world.exited = true;
                    } else if (target == TILE_BUTTON_OFF) {
                        if (world.timer[ncy][ncx] <= 1) open_doors(&world);
                    } else if (target == TILE_BUTTON_OFF + 1) {
                        if (world.timer[ncy][ncx] <= 1) close_doors(&world);
                    } else if (is_diggable(target) && world.hits[ncy][ncx] < 30000) {
                        // Digging — pickaxe animation only for hard materials
                        bool is_hard = is_stone(target) || is_brick(target)
                            || (target >= TILE_STONE_TOP_LEFT && target <= TILE_STONE_BOTTOM_RIGHT)
                            || target == TILE_STONE_BOTTOM_LEFT
                            || target == TILE_STONE_CRACKED_LIGHT || target == TILE_STONE_CRACKED_HEAVY
                            || target == TILE_BRICK_CRACKED_LIGHT || target == TILE_BRICK_CRACKED_HEAVY;
                        actor->is_digging = is_hard;
                        world.hits[ncy][ncx] -= actor->drilling;
                        if (is_stone(target)) {
                            if (world.hits[ncy][ncx] < 500) world.tiles[ncy][ncx] = TILE_STONE_CRACKED_HEAVY;
                            else if (world.hits[ncy][ncx] < 1000) world.tiles[ncy][ncx] = TILE_STONE_CRACKED_LIGHT;
                        } else if (target == TILE_STONE_CRACKED_LIGHT) {
                            if (world.hits[ncy][ncx] < 500) world.tiles[ncy][ncx] = TILE_STONE_CRACKED_HEAVY;
                        } else if (is_brick(target)) {
                            if (world.hits[ncy][ncx] <= 2000) world.tiles[ncy][ncx] = TILE_BRICK_CRACKED_HEAVY;
                            else if (world.hits[ncy][ncx] <= 4000) world.tiles[ncy][ncx] = TILE_BRICK_CRACKED_LIGHT;
                        }
                        if (world.hits[ncy][ncx] <= 0) {
                            world.tiles[ncy][ncx] = TILE_PASSAGE;
                            actor->is_digging = false;
                        }
                    } else if (is_pushable(target)) {
                        if (world.hits[ncy][ncx] > 1) {
                            world.hits[ncy][ncx] -= actor->drilling;
                        } else {
                            int pcx = ncx + (actor->facing == DIR_RIGHT ? 1 : (actor->facing == DIR_LEFT ? -1 : 0));
                            int pcy = ncy + (actor->facing == DIR_DOWN ? 1 : (actor->facing == DIR_UP ? -1 : 0));
                            if (pcx >= 0 && pcx < MAP_WIDTH && pcy >= 0 && pcy < MAP_HEIGHT && is_passable(world.tiles[pcy][pcx])) {
                                bool blocked = false;
                                for (int ai = 0; ai < world.num_actors; ai++) {
                                    if (world.actors[ai].is_dead) continue;
                                    if (world.actors[ai].pos.x / 10 == pcx && (world.actors[ai].pos.y - 30) / 10 == pcy) { blocked = true; break; }
                                }
                                if (!blocked) {
                                    world.tiles[pcy][pcx] = world.tiles[ncy][ncx];
                                    world.timer[pcy][pcx] = world.timer[ncy][ncx];
                                    world.hits[pcy][pcx] = 24;
                                    world.tiles[ncy][ncx] = TILE_PASSAGE;
                                    world.timer[ncy][ncx] = 0;
                                    world.hits[ncy][ncx] = 0;
                                } else {
                                    world.hits[ncy][ncx] = 24;
                                }
                            } else world.hits[ncy][ncx] = 24;
                        }
                    } else if (target == TILE_WALL || target == TILE_DOOR) {
                        actor->moving = false;
                    }
                }

                // Animation update
                actor->animation %= 30;
                if (actor->is_digging && actor->animation == 16) context_play_sample_freq(app->sound_picaxe, 11000 + (game_rand() % 100));
                actor->animation++;
            } else { actor->animation = 0; }
          }
        }

        // Monster AI and movement
        animate_monsters(&world, app);

        // Timer processing
        for (int y = 0; y < MAP_HEIGHT; ++y) {
            for (int x = 0; x < MAP_WIDTH; ++x) {
                if (world.timer[y][x] > 0) {
                    world.timer[y][x]--;
                    if (world.timer[y][x] == 0) {
                        uint8_t t = world.tiles[y][x];
                        if (is_bomb(t)) {
                            explosion_chain_count = 0;
                            explosion_nuke_sound_played = false;
                            explode_bomb(&world, x, y);
                        } else if (t == TILE_GRENADE_FLY_R || t == TILE_GRENADE_FLY_L || t == TILE_GRENADE_FLY_D || t == TILE_GRENADE_FLY_U) {
                            // Grenade flies forward; if blocked, explode
                            int gx = x, gy = y;
                            if (t == TILE_GRENADE_FLY_R) gx++;
                            else if (t == TILE_GRENADE_FLY_L) gx--;
                            else if (t == TILE_GRENADE_FLY_D) gy++;
                            else if (t == TILE_GRENADE_FLY_U) gy--;
                            bool can_fly = false;
                            if (gx >= 0 && gx < MAP_WIDTH && gy >= 0 && gy < MAP_HEIGHT) {
                                uint8_t next_t = world.tiles[gy][gx];
                                if (is_passable(next_t) || next_t == t) {
                                    bool player_blocking = false;
                                    for (int p = 0; p < world.num_players; p++) {
                                        if (world.actors[p].is_dead) continue;
                                        int pcx = world.actors[p].pos.x / 10;
                                        int pcy = (world.actors[p].pos.y - 30) / 10;
                                        if (pcx == gx && pcy == gy) { player_blocking = true; break; }
                                    }
                                    if (!player_blocking) can_fly = true;
                                }
                            }
                            if (can_fly) {
                                world.tiles[y][x] = TILE_PASSAGE;
                                world.tiles[gy][gx] = t;
                                world.timer[gy][gx] = 2;
                            } else {
                                world.tiles[y][x] = TILE_SMALL_BOMB1;
                                explosion_chain_count = 0;
                                explosion_nuke_sound_played = false;
                                explode_bomb(&world, x, y);
                            }
                        } else if (t == TILE_METAL_WALL_PLACED) {
                            world.tiles[y][x] = TILE_WALL;
                            world.hits[y][x] = 30000;
                            context_play_sample(app->sound_picaxe);
                        } else if (t == TILE_EXPLOSION) {
                            world.tiles[y][x] = TILE_SMOKE1;
                            world.timer[y][x] = 3;
                        } else if (t == TILE_SMOKE1) {
                            world.tiles[y][x] = TILE_SMOKE2;
                            world.timer[y][x] = 3;
                        } else if (t == TILE_SMOKE2) {
                            world.tiles[y][x] = TILE_PASSAGE;
                            world.timer[y][x] = 0;
                        } else if (t == TILE_MONSTER_DYING) {
                            world.tiles[y][x] = TILE_MONSTER_SMOKE1;
                            world.timer[y][x] = 3;
                        } else if (t == TILE_MONSTER_SMOKE1) {
                            world.tiles[y][x] = TILE_MONSTER_SMOKE2;
                            world.timer[y][x] = 3;
                        } else if (t == TILE_MONSTER_SMOKE2) {
                            world.tiles[y][x] = TILE_BLOOD;
                            world.timer[y][x] = 0;
                        } else if (t == TILE_SLIME_DYING) {
                            world.tiles[y][x] = TILE_SLIME_SMOKE1;
                            world.timer[y][x] = 3;
                        } else if (t == TILE_SLIME_SMOKE1) {
                            world.tiles[y][x] = TILE_SLIME_SMOKE2;
                            world.timer[y][x] = 3;
                        } else if (t == TILE_SLIME_SMOKE2) {
                            world.tiles[y][x] = TILE_SLIME_CORPSE;
                            world.timer[y][x] = 0;
                        } else if (t == TILE_BIOMASS) {
                            int clock = 1 + game_rand() % 140;
                            world.timer[y][x] = clock;
                            int dirs[][2] = {{-1,0},{1,0},{0,-1},{0,1}};
                            int d = game_rand() % 4;
                            int nx = x + dirs[d][0], ny = y + dirs[d][1];
                            if (nx >= 0 && nx < MAP_WIDTH && ny >= 0 && ny < MAP_HEIGHT && is_passable(world.tiles[ny][nx])) {
                                world.tiles[ny][nx] = TILE_BIOMASS;
                                world.timer[ny][nx] = clock;
                                world.hits[ny][nx] = 400;
                            }
                        } else if (t == TILE_BUTTON_OFF || t == TILE_BUTTON_OFF + 1) {
                            // Button cooldown expired - stay as is
                        } else if (t == TILE_EXIT || t == TILE_DOOR) {
                            // Permanent tiles - never convert to passage
                        } else {
                            world.tiles[y][x] = TILE_PASSAGE;
                            for (int pi = 0; pi < world.num_actors; pi++) {
                                if (world.actors[pi].is_dead && (world.actors[pi].pos.x / 10) == x && ((world.actors[pi].pos.y - 30) / 10) == y) {
                                    if (world.actors[pi].kind == ACTOR_SLIME) world.tiles[y][x] = TILE_SLIME_CORPSE;
                                    else world.tiles[y][x] = TILE_BLOOD;
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
                        else if (t == TILE_NAPALM1) world.tiles[y][x] = TILE_NAPALM2;
                        else if (t == TILE_NAPALM2) world.tiles[y][x] = TILE_NAPALM1;
                    }
                }
            }
        }

        // Super drill countdown
        if (world.round_counter % 18 == 0) {
            for (int p = 0; p < world.num_players; p++) {
                if (world.actors[p].super_drill_count > 0) {
                    world.actors[p].super_drill_count--;
                    if (world.actors[p].super_drill_count == 0) {
                        world.actors[p].drilling -= 300;
                    }
                }
            }
        }

        world.round_counter++;

        render_world(app, ctx, &world);

        // Time bar (multiplayer only) - draw to buffer before present
        if (!world.campaign_mode && round_time_ms > 0) {
            Uint32 elapsed = SDL_GetTicks() - round_start;
            int width = (int)((635ULL * elapsed) / round_time_ms);
            if (width > 635) width = 635;
            SDL_SetRenderTarget(ctx->renderer, ctx->buffer);
            // Background bar
            SDL_Color bg_c = app->players.palette[6];
            SDL_SetRenderDrawColor(ctx->renderer, bg_c.r, bg_c.g, bg_c.b, 255);
            SDL_Rect bg_bar = {2, 473, 635, 5};
            SDL_RenderFillRect(ctx->renderer, &bg_bar);
            // Progress bar (grows from right)
            SDL_Color fg_c = app->players.palette[0];
            SDL_SetRenderDrawColor(ctx->renderer, fg_c.r, fg_c.g, fg_c.b, 255);
            SDL_Rect fg_bar = {636 - width, 473, width, 5};
            SDL_RenderFillRect(ctx->renderer, &fg_bar);
            SDL_SetRenderTarget(ctx->renderer, NULL);

            // End round on time expiry
            if (elapsed >= round_time_ms) {
                running = false;
            }
        }

        context_present(ctx);
        // Frame pacing: wait until tick_ms has elapsed (local play only;
        // for netplay the server paces inside net_exchange_inputs)
#ifdef MB_NET
        if (!net)
#endif
        {
            Uint32 frame_elapsed = SDL_GetTicks() - frame_start;
            if ((int)frame_elapsed < tick_ms)
                SDL_Delay((Uint32)(tick_ms - (int)frame_elapsed));
        }
    }

    if (world.canvas) SDL_DestroyTexture(world.canvas);
    world.canvas = NULL;

    RoundResult result;
    memset(&result, 0, sizeof(result));
    for (int p = 0; p < world.num_players; p++) {
        result.player_survived[p] = !world.actors[p].is_dead;
        result.player_cash_earned[p] = world.cash_earned[p];
        result.treasures_collected[p] = world.treasures_collected[p];
        result.bombs_dropped[p] = world.bombs_dropped[p];
        result.deaths[p] = world.deaths[p];
        result.meters_ran[p] = world.meters_ran[p];
    }
    // Calculate remaining gold value on level
    for (int y = 0; y < MAP_HEIGHT; y++)
        for (int x = 0; x < MAP_WIDTH; x++)
            result.gold_remaining += get_treasure_value(world.tiles[y][x]);
    if (quit_requested && world.campaign_mode) result.end_type = ROUND_END_QUIT;
    else if (quit_requested && !world.campaign_mode) result.end_type = ROUND_END_FINAL;
    else if (world.exited) result.end_type = ROUND_END_EXITED;
    else if (world.campaign_mode && world.actors[0].is_dead) result.end_type = ROUND_END_FAILED;
    else result.end_type = ROUND_END_NORMAL;
    result.lives_gained = world.lives_gained;
    return result;
}
