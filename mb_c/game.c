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

// ==================== Tile helpers ====================

bool is_passable(uint8_t val) {
    return val == TILE_PASSAGE || val == TILE_TELEPORT || val == TILE_BLOOD
        || val == TILE_EXPLOSION || val == TILE_SMOKE1 || val == TILE_SMOKE2
        || (val >= 0x3A && val <= 0x40) || val == TILE_SLIME_CORPSE
        || val == TILE_EXIT || val == TILE_MINE;
}

static bool is_treasure(uint8_t val) {
    if (val == TILE_DIAMOND) return true;
    if (val >= TILE_GOLD_SHIELD && val <= TILE_GOLD_CROWN) return true;
    if (val == TILE_MEDIKIT || val == TILE_WEAPONS_CRATE || val == TILE_LIFE_ITEM) return true;
    if (val >= TILE_SMALL_PICKAXE && val <= TILE_DRILL) return true;
    if (val == TILE_BUTTON_OFF || val == (TILE_BUTTON_OFF + 1)) return true;
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
        || val == TILE_JUMPING_BOMB || val == TILE_EXPLOSIVE_PLASTIC;
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

// ==================== Explosions ====================

static void apply_explosion_damage(World* world, int cx, int cy, int dmg) {
    for (int i = 0; i < world->num_actors; ++i) {
        Actor* actor = &world->actors[i];
        if (i == 0 && world->god_mode) continue;
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

    if (cx > 0 && !is_passable(world->tiles[cy][cx-1])) world->burned[cy][cx] |= BURNED_L;
    if (cx < MAP_WIDTH - 1 && !is_passable(world->tiles[cy][cx+1])) world->burned[cy][cx] |= BURNED_R;
    if (cy > 0 && !is_passable(world->tiles[cy-1][cx])) world->burned[cy][cx] |= BURNED_U;
    if (cy < MAP_HEIGHT - 1 && !is_passable(world->tiles[cy+1][cx])) world->burned[cy][cx] |= BURNED_D;

    if (is_bomb(val)) {
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
    } else if (val != TILE_WALL && val != TILE_DOOR && val != TILE_BUTTON_OFF && val != (TILE_BUTTON_OFF + 1)) {
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

static void explode_barrel(World* world, int cx, int cy, App* app) {
    world->tiles[cy][cx] = TILE_EXPLOSION;
    world->timer[cy][cx] = 3;
    if (app->sound_explos1) context_play_sample_freq(app->sound_explos1, 11000);
    int from = rand() % 5;
    for (int i = from; i < 15; i++) {
        int dx = (rand() % 20) - 10;
        int dy = (rand() % 20) - 10;
        int tx = cx + dx, ty = cy + dy;
        if (tx >= 0 && tx < MAP_WIDTH && ty >= 0 && ty < MAP_HEIGHT) {
            explode_pattern(world, tx, ty, 84, BIG_BOMB_PATTERN, sizeof(BIG_BOMB_PATTERN)/sizeof(BIG_BOMB_PATTERN[0]));
            if (app->sound_explos1) context_play_sample_freq(app->sound_explos1, 11000);
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
    world->timer[y][x] = 250;
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
    int r = rand() % 3;
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
            int dx = (rand() % 8) - 4;
            int dy = (rand() % 8) - 4;
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
        world->timer[ny][nx] = 1 + rand() % 180;
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
        default: return 1;
    }
}

static int monster_damage(ActorKind kind) {
    switch (kind) {
        case ACTOR_FURRY: return 2;
        case ACTOR_GRENADIER: return 3;
        case ACTOR_SLIME: return 1;
        case ACTOR_ALIEN: return 5;
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
                world->timer[y][x] = rand() % 30;
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
        if (rand() % 2) {
            world->actors[0].pos.x = 15;  world->actors[0].pos.y = 45;  world->actors[0].facing = DIR_RIGHT;
            world->actors[1].pos.x = 625; world->actors[1].pos.y = 465; world->actors[1].facing = DIR_LEFT;
        } else {
            world->actors[0].pos.x = 625; world->actors[0].pos.y = 465; world->actors[0].facing = DIR_LEFT;
            world->actors[1].pos.x = 15;  world->actors[1].pos.y = 45;  world->actors[1].facing = DIR_RIGHT;
        }
    }
    if (num_players == 3) {
        if (rand() % 2) {
            world->actors[2].pos.x = 15;  world->actors[2].pos.y = 465; world->actors[2].facing = DIR_RIGHT;
        } else {
            world->actors[2].pos.x = 625; world->actors[2].pos.y = 45;  world->actors[2].facing = DIR_LEFT;
        }
    } else if (num_players == 4) {
        if (rand() % 2) {
            world->actors[2].pos.x = 15;  world->actors[2].pos.y = 465; world->actors[2].facing = DIR_RIGHT;
            world->actors[3].pos.x = 625; world->actors[3].pos.y = 45;  world->actors[3].facing = DIR_LEFT;
        } else {
            world->actors[2].pos.x = 625; world->actors[2].pos.y = 45;  world->actors[2].facing = DIR_LEFT;
            world->actors[3].pos.x = 15;  world->actors[3].pos.y = 465; world->actors[3].facing = DIR_RIGHT;
        }
    }
    world->god_mode = false;
}

static void randomize_exits(World* world) {
    // Count exits, keep only one random one
    int exit_count = 0;
    for (int y = 0; y < MAP_HEIGHT; y++)
        for (int x = 0; x < MAP_WIDTH; x++)
            if (world->tiles[y][x] == TILE_EXIT) exit_count++;
    if (exit_count <= 1) return;
    int keep = rand() % exit_count;
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
    return is_passable(val) || is_treasure(val);
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
    int r = rand() % 5;
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

    if (dx > dy || (rand() % 100) < 3) {
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
        if (p == 0 && world->god_mode) continue;
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
            bool detected = (abs(mcx - pcx) < 20 && abs(mcy - pcy) < 20)
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
    if (dist < 4) return;

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
            if (world->tiles[mcy][mcx] == TILE_PASSAGE || world->tiles[mcy][mcx] == TILE_BLOOD) {
                world->tiles[mcy][mcx] = gval;
                world->timer[mcy][mcx] = 1;
            }
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

    switch (actor->facing) {
        case DIR_LEFT:  d_dir = dx; d_ortho = dy; finishing = dx > 5; break;
        case DIR_RIGHT: d_dir = dx; d_ortho = dy; finishing = dx < 5; break;
        case DIR_UP:    d_dir = dy; d_ortho = dx; finishing = dy > 5; break;
        case DIR_DOWN:  d_dir = dy; d_ortho = dx; finishing = dy < 5; break;
    }

    int ncx = cx + (actor->facing == DIR_RIGHT ? 1 : (actor->facing == DIR_LEFT ? -1 : 0));
    int ncy = cy + (actor->facing == DIR_DOWN ? 1 : (actor->facing == DIR_UP ? -1 : 0));

    bool can_move = d_ortho > 3 && d_ortho < 6;
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

    // Interact with map when centered
    if (d_dir == 5 && ncx >= 0 && ncx < MAP_WIDTH && ncy >= 0 && ncy < MAP_HEIGHT) {
        uint8_t val = world->tiles[ncy][ncx];
        if (is_diggable(val) && world->hits[ncy][ncx] < 30000) {
            world->hits[ncy][ncx] -= actor->drilling;
            if (is_stone(val)) {
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
        } else if (val == TILE_MINE) {
            world->timer[ncy][ncx] = 1;
        }
    }

    actor->animation_timer++;
    if (actor->animation_timer >= 4) {
        actor->animation = (actor->animation + 1) % 4;
        actor->animation_timer = 0;
    }
}

static void animate_monsters(World* world, App* app) {
    (void)app;
    monster_detect_players(world);

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
                }

                if (m->kind == ACTOR_GRENADIER) {
                    grenadier_maybe_throw(world, i);
                }
            }
        }

        if ((world->round_counter % 33 == 0 && !monster_can_move(m, world)) || world->round_counter % 121 == 0) {
            Direction dirs[] = {DIR_LEFT, DIR_RIGHT, DIR_UP, DIR_DOWN};
            m->moving = true;
            m->facing = dirs[rand() % 4];
        }
    }
}

// ==================== Rendering ====================

static void render_actor(App* app, SDL_Renderer* renderer, Actor* actor, int actor_idx, int num_players) {
    if (actor->is_dead) return;
    int glyph;
    if (actor_idx < num_players) {
        int base = actor->is_digging ? GLYPH_PLAYER_DIG_START : GLYPH_PLAYER_START;
        base += actor_idx * 1000;
        int anim_frame = 0;
        if (actor->is_digging) {
            static const int pp[] = {0, 1, 2, 3, 2, 1};
            anim_frame = pp[actor->animation % 6];
        } else {
            anim_frame = actor->animation % 4;
        }
        glyph = base + (int)actor->facing + (anim_frame * 4);
    } else {
        int base = (int)monster_glyph_base(actor->kind);
        int anim_frame = actor->animation % 4;
        glyph = base + (int)actor->facing + (anim_frame * 4);
    }
    glyphs_render(&app->glyphs, renderer, actor->pos.x - 5, actor->pos.y - 5, (GlyphType)glyph);
}

static void render_burned_borders(App* app, SDL_Renderer* renderer, World* world, int x, int y) {
    uint8_t val = world->tiles[y][x];
    int px = x * 10;
    int py = y * 10 + 30;

    if (val == TILE_EXPLOSION || val == TILE_SMOKE1 || val == TILE_SMOKE2 || is_passable(val)) {
        uint8_t b = (val == TILE_EXPLOSION) ? 0xF : world->burned[y][x];
        if (b & BURNED_L && x > 0) {
            uint8_t n = world->tiles[y][x-1];
            if (is_sand(n)||n==TILE_GRAVEL_LIGHT||n==TILE_GRAVEL_HEAVY) glyphs_render(&app->glyphs, renderer, px-4, py, GLYPH_BURNED_SAND_R);
            else if (is_stone(n)||n==TILE_STONE_CRACKED_LIGHT||n==TILE_STONE_CRACKED_HEAVY||is_brick(n)) glyphs_render(&app->glyphs, renderer, px-4, py, GLYPH_BURNED_STONE_R);
        }
        if (b & BURNED_R && x < MAP_WIDTH-1) {
            uint8_t n = world->tiles[y][x+1];
            if (is_sand(n)||n==TILE_GRAVEL_LIGHT||n==TILE_GRAVEL_HEAVY) glyphs_render(&app->glyphs, renderer, px+10, py, GLYPH_BURNED_SAND_L);
            else if (is_stone(n)||n==TILE_STONE_CRACKED_LIGHT||n==TILE_STONE_CRACKED_HEAVY||is_brick(n)) glyphs_render(&app->glyphs, renderer, px+10, py, GLYPH_BURNED_STONE_L);
        }
        if (b & BURNED_U && y > 0) {
            uint8_t n = world->tiles[y-1][x];
            if (is_sand(n)||n==TILE_GRAVEL_LIGHT||n==TILE_GRAVEL_HEAVY) glyphs_render(&app->glyphs, renderer, px, py-3, GLYPH_BURNED_SAND_D);
            else if (is_stone(n)||n==TILE_STONE_CRACKED_LIGHT||n==TILE_STONE_CRACKED_HEAVY||is_brick(n)) glyphs_render(&app->glyphs, renderer, px, py-3, GLYPH_BURNED_STONE_D);
        }
        if (b & BURNED_D && y < MAP_HEIGHT-1) {
            uint8_t n = world->tiles[y+1][x];
            if (is_sand(n)||n==TILE_GRAVEL_LIGHT||n==TILE_GRAVEL_HEAVY) glyphs_render(&app->glyphs, renderer, px, py+10, GLYPH_BURNED_SAND_U);
            else if (is_stone(n)||n==TILE_STONE_CRACKED_LIGHT||n==TILE_STONE_CRACKED_HEAVY||is_brick(n)) glyphs_render(&app->glyphs, renderer, px, py+10, GLYPH_BURNED_STONE_U);
        }
    }
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

        for (int y = 1; y < MAP_HEIGHT - 1; ++y)
            for (int x = 1; x < MAP_WIDTH - 1; ++x)
                if (!world->fog[y][x])
                    glyphs_render(&app->glyphs, ctx->renderer, x * 10, y * 10 + 30, (GlyphType)(GLYPH_MAP_START + world->tiles[y][x]));

        for (int y = 1; y < MAP_HEIGHT - 1; ++y)
            for (int x = 1; x < MAP_WIDTH - 1; ++x)
                if (!world->fog[y][x])
                    render_burned_borders(app, ctx->renderer, world, x, y);

        for (int i = 0; i < world->num_actors; ++i) {
            Actor* a = &world->actors[i];
            if (!a->is_dead) {
                int acx = a->pos.x / 10;
                int acy = (a->pos.y - 30) / 10;
                if (acx >= 0 && acx < MAP_WIDTH && acy >= 0 && acy < MAP_HEIGHT && !world->fog[acy][acx])
                    render_actor(app, ctx->renderer, a, i, world->num_players);
            }
        }
    } else {
        for (int y = 0; y < MAP_HEIGHT; ++y)
            for (int x = 0; x < MAP_WIDTH; ++x)
                glyphs_render(&app->glyphs, ctx->renderer, x * 10, y * 10 + 30, (GlyphType)(GLYPH_MAP_START + world->tiles[y][x]));

        for (int y = 0; y < MAP_HEIGHT; ++y)
            for (int x = 0; x < MAP_WIDTH; ++x)
                render_burned_borders(app, ctx->renderer, world, x, y);

        for (int i = 0; i < world->num_actors; ++i)
            render_actor(app, ctx->renderer, &world->actors[i], i, world->num_players);
    }

    SDL_SetRenderTarget(ctx->renderer, NULL);
    context_present(ctx);
}

// ==================== Player interaction with map tiles ====================

static void player_interact_tile(App* app, World* world, int p, int ncx, int ncy) {
    uint8_t val = world->tiles[ncy][ncx];

    // Pickaxe/drill items
    if (val == TILE_SMALL_PICKAXE) { world->actors[p].drilling += 1; world->tiles[ncy][ncx] = TILE_PASSAGE; context_play_sample(app->sound_picaxe); return; }
    if (val == TILE_LARGE_PICKAXE) { world->actors[p].drilling += 3; world->tiles[ncy][ncx] = TILE_PASSAGE; context_play_sample(app->sound_picaxe); return; }
    if (val == TILE_DRILL) { world->actors[p].drilling += 5; world->tiles[ncy][ncx] = TILE_PASSAGE; context_play_sample(app->sound_picaxe); return; }

    // Mine
    if (val == TILE_MINE) {
        world->timer[ncy][ncx] = 1;
        return;
    }

    // Medikit
    if (val == TILE_MEDIKIT) {
        world->actors[p].health = world->actors[p].max_health;
        world->tiles[ncy][ncx] = TILE_PASSAGE;
        context_play_sample(app->sound_picaxe);
        return;
    }

    // Weapons crate - 3 categories: rare (1/5), medium (1/5), common (3/5)
    if (val == TILE_WEAPONS_CRATE) {
        int cat = rand() % 5;
        if (cat == 0) {
            // Rare: 1-2 of atomic/grenade/flamethrower/clone
            int cnt = 1 + rand() % 2;
            int weaps[] = {EQUIP_ATOMIC_BOMB, EQUIP_GRENADE, EQUIP_FLAMETHROWER, EQUIP_CLONE};
            app->player_inventory[p][weaps[rand() % 4]] += cnt;
        } else if (cat == 1) {
            // Medium: 1-5 of napalm/large_crucifix/teleport/biomass/extinguisher/jumping_bomb/super_drill
            int cnt = 1 + rand() % 5;
            int weaps[] = {EQUIP_NAPALM, EQUIP_LARGE_CRUCIFIX, EQUIP_TELEPORT, EQUIP_BIOMASS,
                           EQUIP_EXTINGUISHER, EQUIP_JUMPING_BOMB, EQUIP_SUPER_DRILL};
            app->player_inventory[p][weaps[rand() % 7]] += cnt;
        } else {
            // Common: 3-12 of basic weapons
            int cnt = 3 + rand() % 10;
            int weaps[] = {EQUIP_SMALL_BOMB, EQUIP_BIG_BOMB, EQUIP_DYNAMITE, EQUIP_SMALL_RADIO,
                           EQUIP_LARGE_RADIO, EQUIP_MINE, EQUIP_BARREL, EQUIP_SMALL_CRUCIFIX,
                           EQUIP_PLASTIC, EQUIP_EXPLOSIVE_PLASTIC, EQUIP_DIGGER, EQUIP_METAL_WALL};
            app->player_inventory[p][weaps[rand() % 12]] += cnt;
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

    // Exit tile (campaign only)
    if (val == TILE_EXIT && world->campaign_mode) {
        world->exited = true;
        return;
    }

    // Button
    if (val == TILE_BUTTON_OFF) {
        if (world->timer[ncy][ncx] <= 1) open_doors(world);
        return;
    }
    if (val == TILE_BUTTON_OFF + 1) { // ButtonOn
        if (world->timer[ncy][ncx] <= 1) close_doors(world);
        return;
    }
}

// ==================== Main game loop ====================

RoundResult game_run(App* app, ApplicationContext* ctx, uint8_t* level_data) {
    int tracks[] = {0, 39, 55};
    context_play_music_at(ctx, "OEKU.S3M", tracks[rand() % 3]);

    bool campaign = (app->options.players == 1);
    int num_players = campaign ? 1 : app->options.players;

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
        world.actors[p].drilling = app->player_inventory[p][EQUIP_SMALL_PICKAXE]
            + 3 * app->player_inventory[p][EQUIP_LARGE_PICKAXE]
            + 5 * app->player_inventory[p][EQUIP_DRILL];
    }

    bool running = true, quit_requested = false;
    while (running) {
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_QUIT) { running = false; quit_requested = true; break; }

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
                                // Non-placeable items: use immediately
                                if (w == EQUIP_FLAMETHROWER && app->player_inventory[p][w] > 0) {
                                    // Flamethrower: expand explosion in facing direction
                                    app->player_inventory[p][w]--;
                                    int dx = (actor->facing == DIR_RIGHT) ? 1 : (actor->facing == DIR_LEFT) ? -1 : 0;
                                    int dy = (actor->facing == DIR_DOWN) ? 1 : (actor->facing == DIR_UP) ? -1 : 0;
                                    int fx = cx + dx, fy = cy + dy;
                                    for (int i = 0; i < 20; i++) {
                                        if (fx < 0 || fx >= MAP_WIDTH || fy < 0 || fy >= MAP_HEIGHT) break;
                                        uint8_t fv = world.tiles[fy][fx];
                                        if (fv == TILE_WALL || fv == TILE_DOOR) break;
                                        explode_cell_ex(&world, fx, fy, 100, true);
                                        fx += dx; fy += dy;
                                    }
                                    if (app->sound_explos1) context_play_sample_freq(app->sound_explos1, 11000);
                                } else if (w == EQUIP_EXTINGUISHER && app->player_inventory[p][w] > 0) {
                                    app->player_inventory[p][w]--;
                                    int dx = (actor->facing == DIR_RIGHT) ? 1 : (actor->facing == DIR_LEFT) ? -1 : 0;
                                    int dy = (actor->facing == DIR_DOWN) ? 1 : (actor->facing == DIR_UP) ? -1 : 0;
                                    int fx = cx + dx, fy = cy + dy;
                                    for (int i = 0; i < 6; i++) {
                                        if (fx < 0 || fx >= MAP_WIDTH || fy < 0 || fy >= MAP_HEIGHT) break;
                                        if (!is_passable(world.tiles[fy][fx]) && !is_bomb(world.tiles[fy][fx])) break;
                                        world.timer[fy][fx] = 0;
                                        if (is_bomb(world.tiles[fy][fx])) world.hits[fy][fx] = 20;
                                        fx += dx; fy += dy;
                                    }
                                } else if (w == EQUIP_ARMOR) {
                                    // Armor is passive - does nothing on action
                                } else if (w == EQUIP_SUPER_DRILL && app->player_inventory[p][w] > 0) {
                                    app->player_inventory[p][w]--;
                                    actor->drilling += 300;
                                } else if (w == EQUIP_CLONE && app->player_inventory[p][w] > 0 && world.num_actors < MAX_ACTORS) {
                                    app->player_inventory[p][w]--;
                                    // Spawn clone at player position (acts as monster ally - simplified)
                                } else if (app->player_inventory[p][w] > 0 && (world.tiles[cy][cx] == TILE_PASSAGE || is_treasure(world.tiles[cy][cx]))) {
                                    // Placeable weapons
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
                                        case EQUIP_PLASTIC: tile = TILE_PLASTIC_BOMB; timer = 100; break;
                                        case EQUIP_EXPLOSIVE_PLASTIC: tile = TILE_EXPLOSIVE_PLASTIC_BOMB; timer = 90; break;
                                        case EQUIP_DIGGER: tile = TILE_DIGGER_BOMB; timer = 100; break;
                                        case EQUIP_METAL_WALL: tile = TILE_METAL_WALL_PLACED; timer = 1; break;
                                        case EQUIP_TELEPORT: tile = TILE_TELEPORT; timer = 0; break;
                                        case EQUIP_BIOMASS: tile = TILE_BIOMASS; timer = rand() % 80; break;
                                        case EQUIP_JUMPING_BOMB:
                                            tile = TILE_JUMPING_BOMB; timer = 80 + rand() % 80;
                                            break;
                                        default: place = false; break;
                                    }
                                    if (place) {
                                        world.tiles[cy][cx] = tile;
                                        world.timer[cy][cx] = timer;
                                        if (w == EQUIP_JUMPING_BOMB) world.hits[cy][cx] = 5;
                                        if (w == EQUIP_BIOMASS) world.hits[cy][cx] = 400;
                                        app->player_inventory[p][w]--;
                                    }
                                }
                            }
                        } break;
                        case ACT_CYCLE: {
                            int total_inv = 0;
                            for (int i = 0; i < EQUIP_TOTAL; i++) total_inv += app->player_inventory[p][i];
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
                        case ACT_REMOTE: {
                            // Detonate all radio bombs belonging to this player
                            for (int ry = 0; ry < MAP_HEIGHT; ry++)
                                for (int rx = 0; rx < MAP_WIDTH; rx++)
                                    if (is_radio_for(world.tiles[ry][rx], p))
                                        world.timer[ry][rx] = 1;
                        } break;
                        default: break;
                    }
                }

                if (e.type == SDL_CONTROLLERBUTTONDOWN && e.cbutton.button == SDL_CONTROLLER_BUTTON_BACK) {
                    running = false;
                    if (world.campaign_mode) quit_requested = true;
                }
                if (e.type == SDL_KEYDOWN && e.key.keysym.scancode == SDL_SCANCODE_ESCAPE) {
                    running = false;
                    if (world.campaign_mode) quit_requested = true;
                }
                if (e.type == SDL_KEYDOWN && e.key.keysym.scancode == SDL_SCANCODE_F10) {
                    running = false;
                    quit_requested = true;
                }
            }
        }

        // Check deaths (players)
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

        // Campaign: exit or all dead ends round
        // Multiplayer: <= 1 alive ends round
        if (world.exited && world.round_end_timer == 0) {
            world.round_end_timer = 60;
        } else if (world.campaign_mode) {
            if (alive_count == 0 && world.round_end_timer == 0)
                world.round_end_timer = 120;
        } else {
            if (alive_count <= 1 && world.round_end_timer == 0)
                world.round_end_timer = 120;
        }
        if (world.round_end_timer > 0) {
            world.round_end_timer--;
            if (world.round_end_timer == 0) running = false;
        }

        // Player movement
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

                switch (actor->facing) {
                    case DIR_LEFT:  d_dir = dx; d_ortho = dy; finishing = dx > 5; break;
                    case DIR_RIGHT: d_dir = dx; d_ortho = dy; finishing = dx < 5; break;
                    case DIR_UP:    d_dir = dy; d_ortho = dx; finishing = dy > 5; break;
                    case DIR_DOWN:  d_dir = dy; d_ortho = dx; finishing = dy < 5; break;
                    default: break;
                }

                int ncx = cx + (actor->facing == DIR_RIGHT ? 1 : (actor->facing == DIR_LEFT ? -1 : 0));
                int ncy = cy + (actor->facing == DIR_DOWN ? 1 : (actor->facing == DIR_UP ? -1 : 0));

                if (d_ortho == 5 && (finishing || (ncx >= 0 && ncx < MAP_WIDTH && ncy >= 0 && ncy < MAP_HEIGHT && (is_passable(world.tiles[ncy][ncx]) || is_treasure(world.tiles[ncy][ncx]))))) {
                    if (actor->facing == DIR_LEFT) actor->pos.x--;
                    else if (actor->facing == DIR_RIGHT) actor->pos.x++;
                    else if (actor->facing == DIR_UP) actor->pos.y--;
                    else if (actor->facing == DIR_DOWN) actor->pos.y++;

                    int new_cx = actor->pos.x / 10;
                    int new_cy = (actor->pos.y - 30) / 10;
                    int c_dx = actor->pos.x % 10;
                    int c_dy = (actor->pos.y - 30) % 10;

                    // Collect treasures in overlapping cell
                    if (c_dx != 5 || c_dy != 5) {
                        int tcx = new_cx, tcy = new_cy;
                        if (actor->facing == DIR_LEFT && c_dx < 5) tcx--;
                        else if (actor->facing == DIR_RIGHT && c_dx > 5) tcx++;
                        else if (actor->facing == DIR_UP && c_dy < 5) tcy--;
                        else if (actor->facing == DIR_DOWN && c_dy > 5) tcy++;
                        if (tcx >= 0 && tcx < MAP_WIDTH && tcy >= 0 && tcy < MAP_HEIGHT) {
                            uint8_t tv = world.tiles[tcy][tcx];
                            if (tv >= TILE_GOLD_SHIELD && tv <= TILE_GOLD_CROWN) {
                                app->player_cash[p] += get_treasure_value(tv);
                                world.tiles[tcy][tcx] = TILE_PASSAGE;
                                context_play_sample_freq(app->sound_kili, 10000 + (rand() % 5000));
                            } else if (tv == TILE_DIAMOND) {
                                app->player_cash[p] += 1000;
                                world.tiles[tcy][tcx] = TILE_PASSAGE;
                                context_play_sample_freq(app->sound_kili, 10000 + (rand() % 5000));
                            }
                        }
                    }

                    // Interact with treasure in current cell
                    uint8_t cur_tile = world.tiles[new_cy][new_cx];
                    if (cur_tile >= TILE_GOLD_SHIELD && cur_tile <= TILE_GOLD_CROWN) {
                        app->player_cash[p] += get_treasure_value(cur_tile);
                        world.tiles[new_cy][new_cx] = TILE_PASSAGE;
                        context_play_sample_freq(app->sound_kili, 10000 + (rand() % 5000));
                    } else if (cur_tile == TILE_DIAMOND) {
                        app->player_cash[p] += 1000;
                        world.tiles[new_cy][new_cx] = TILE_PASSAGE;
                        context_play_sample_freq(app->sound_kili, 10000 + (rand() % 5000));
                    }

                    // Interact with special tiles when centered
                    if (c_dx == 5 && c_dy == 5) {
                        player_interact_tile(app, &world, p, new_cx, new_cy);

                        // Teleporter
                        if (world.tiles[new_cy][new_cx] == TILE_TELEPORT) {
                            int tele[256][2], nt = 0;
                            for (int ty = 0; ty < MAP_HEIGHT; ty++)
                                for (int tx = 0; tx < MAP_WIDTH; tx++)
                                    if (world.tiles[ty][tx] == TILE_TELEPORT && (tx != new_cx || ty != new_cy)) {
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
                    }

                    actor->animation_timer++;
                    if (actor->animation_timer >= 4) {
                        actor->animation = (actor->animation + 1) % 4;
                        actor->animation_timer = 0;
                    }
                } else if (d_ortho == 5 && d_dir == 5) {
                    if (ncx >= 0 && ncx < MAP_WIDTH && ncy >= 0 && ncy < MAP_HEIGHT) {
                        uint8_t target = world.tiles[ncy][ncx];
                        if (is_diggable(target)) {
                            actor->is_digging = true;
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
                            actor->animation_timer++;
                            if (actor->animation_timer >= 8) {
                                actor->animation = (actor->animation + 1) % 6;
                                actor->animation_timer = 0;
                                if ((actor->animation % 6) == 3) context_play_sample_freq(app->sound_picaxe, 10500 + (rand() % 1000));
                            }
                        } else if (is_pushable(target)) {
                            if (world.hits[ncy][ncx] > 1) {
                                world.hits[ncy][ncx] -= actor->drilling;
                            } else {
                                int pcx = ncx + (actor->facing == DIR_RIGHT ? 1 : (actor->facing == DIR_LEFT ? -1 : 0));
                                int pcy = ncy + (actor->facing == DIR_DOWN ? 1 : (actor->facing == DIR_UP ? -1 : 0));
                                if (pcx >= 0 && pcx < MAP_WIDTH && pcy >= 0 && pcy < MAP_HEIGHT && is_passable(world.tiles[pcy][pcx])) {
                                    // Check no actor blocking destination
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
                            actor->animation_timer++;
                            if (actor->animation_timer >= 8) { actor->animation = (actor->animation + 1) % 4; actor->animation_timer = 0; }
                        } else if (target == TILE_WALL || target == TILE_DOOR) {
                            actor->moving = false;
                        }
                    }
                } else if (d_ortho != 5) {
                    if (actor->facing == DIR_UP || actor->facing == DIR_DOWN) actor->pos.x = (actor->pos.x / 10) * 10 + 5;
                    else actor->pos.y = ((actor->pos.y - 30) / 10) * 10 + 35;
                }
            } else { actor->animation = 0; }
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
                        if (t == TILE_SMALL_BOMB3 || t == TILE_MINE) {
                            if (app->sound_pikkupom) context_play_sample_freq(app->sound_pikkupom, 11000);
                            explode_pattern(&world, x, y, 60, SMALL_BOMB_PATTERN, sizeof(SMALL_BOMB_PATTERN)/sizeof(SMALL_BOMB_PATTERN[0]));
                        } else if (t == TILE_BIG_BOMB3 || is_small_radio(t) || t == TILE_EXPLOSIVE_PLASTIC) {
                            // Big bomb pattern: big bomb, small radios, explosive plastic
                            if (app->sound_explos1) context_play_sample_freq(app->sound_explos1, 11000);
                            explode_pattern(&world, x, y, 84, BIG_BOMB_PATTERN, sizeof(BIG_BOMB_PATTERN)/sizeof(BIG_BOMB_PATTERN[0]));
                        } else if (t == TILE_DYNAMITE3 || is_big_radio(t)) {
                            // Dynamite pattern: dynamite, big radios
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
                        } else if (t == TILE_BARREL) {
                            explode_barrel(&world, x, y, app);
                        } else if (t == TILE_GRENADE_FLY_R || t == TILE_GRENADE_FLY_L || t == TILE_GRENADE_FLY_D || t == TILE_GRENADE_FLY_U) {
                            if (app->sound_pikkupom) context_play_sample_freq(app->sound_pikkupom, 11000);
                            explode_pattern(&world, x, y, 60, SMALL_BOMB_PATTERN, sizeof(SMALL_BOMB_PATTERN)/sizeof(SMALL_BOMB_PATTERN[0]));
                        } else if (t == TILE_SMALL_CRUCIFIX_BOMB) {
                            explode_crucifix(&world, x, y, true, app);
                        } else if (t == TILE_LARGE_CRUCIFIX_BOMB) {
                            explode_crucifix(&world, x, y, false, app);
                        } else if (t == TILE_PLASTIC_BOMB) {
                            expand_fill(&world, x, y, 45, expand_passable, finalize_plastic, NULL);
                        } else if (t == TILE_EXPLOSIVE_PLASTIC_BOMB) {
                            expand_fill(&world, x, y, 50, expand_passable, finalize_explosive_plastic, NULL);
                        } else if (t == TILE_DIGGER_BOMB) {
                            expand_fill(&world, x, y, 75, expand_digger, finalize_digger, NULL);
                            if (app->sound_explos2) context_play_sample_freq(app->sound_explos2, 11000);
                        } else if (t == TILE_NAPALM1 || t == TILE_NAPALM2) {
                            expand_fill(&world, x, y, 75, expand_napalm, finalize_napalm, NULL);
                            if (app->sound_explos2) context_play_sample_freq(app->sound_explos2, 11000);
                        } else if (t == TILE_JUMPING_BOMB) {
                            explode_jumping_bomb(&world, x, y, app);
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
                            int clock = 1 + rand() % 140;
                            world.timer[y][x] = clock;
                            int dirs[][2] = {{-1,0},{1,0},{0,-1},{0,1}};
                            int d = rand() % 4;
                            int nx = x + dirs[d][0], ny = y + dirs[d][1];
                            if (nx >= 0 && nx < MAP_WIDTH && ny >= 0 && ny < MAP_HEIGHT && is_passable(world.tiles[ny][nx])) {
                                world.tiles[ny][nx] = TILE_BIOMASS;
                                world.timer[ny][nx] = clock;
                                world.hits[ny][nx] = 400;
                            }
                        } else if (t == TILE_BUTTON_OFF + 1) {
                            // ButtonOn timer expired - stay as ButtonOn
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
                        // Grenades fly in their direction
                        else if (t == TILE_GRENADE_FLY_R || t == TILE_GRENADE_FLY_L || t == TILE_GRENADE_FLY_D || t == TILE_GRENADE_FLY_U) {
                            int gx = x, gy = y;
                            if (t == TILE_GRENADE_FLY_R) gx++;
                            else if (t == TILE_GRENADE_FLY_L) gx--;
                            else if (t == TILE_GRENADE_FLY_D) gy++;
                            else if (t == TILE_GRENADE_FLY_U) gy--;
                            world.tiles[y][x] = TILE_PASSAGE;
                            if (gx >= 0 && gx < MAP_WIDTH && gy >= 0 && gy < MAP_HEIGHT && is_passable(world.tiles[gy][gx])) {
                                world.tiles[gy][gx] = t;
                                world.timer[gy][gx] = world.timer[y][x];
                                world.timer[y][x] = 0;
                            } else {
                                // Hit wall, explode next frame at current position
                                world.tiles[y][x] = t;
                                world.timer[y][x] = 1;
                            }
                        }
                    }
                }
            }
        }

        world.round_counter++;

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
    if (quit_requested && world.campaign_mode) result.end_type = ROUND_END_QUIT;
    else if (quit_requested && !world.campaign_mode) result.end_type = ROUND_END_FINAL;
    else if (world.exited) result.end_type = ROUND_END_EXITED;
    else if (world.campaign_mode && world.actors[0].is_dead) result.end_type = ROUND_END_FAILED;
    else result.end_type = ROUND_END_NORMAL;
    result.lives_gained = world.lives_gained;
    return result;
}
