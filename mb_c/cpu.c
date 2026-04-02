#include "cpu.h"
#include "shop.h"
#include "net.h"
#include <stdlib.h>
#include <string.h>

// Timing: CPU delays between actions to look human-like
static int cpu_shop_delay[MAX_PLAYERS];   // frames until next shop action
static int cpu_think_delay[MAX_PLAYERS];  // frames until next gameplay decision

#define CPU_SHOP_MIN_DELAY  12  // ~200ms at 60fps
#define CPU_SHOP_MAX_DELAY  25  // ~400ms
#define CPU_THINK_MIN_DELAY  4  // ~65ms
#define CPU_THINK_MAX_DELAY 12  // ~200ms

static int cpu_random_delay(int lo, int hi) {
    return lo + rand() % (hi - lo + 1);
}

// ==================== CPU Shopping AI ====================

// Level analysis result
typedef struct {
    int stone, sand, treasure, open;
    int monsters, barrels, mines, corridors;
} LevelAnalysis;

// Analyze the level preview bitmap using the same tile classification as
// render_shop_ui, plus count tactical features.
static LevelAnalysis cpu_analyze_level(const uint8_t* map) {
    LevelAnalysis a = {0};
    for (int y = 0; y < 45; y++) {
        for (int x = 0; x < 64; x++) {
            uint8_t val = map[y * 66 + x];
            if (val == 0x30 || val == 0x66 || val == 0xAF) a.open++;
            else if (val == 0x31) a.stone++;
            else if ((val >= 0x37 && val <= 0x46) || val == 0x42 || val == 0x70 || val == 0x71) a.sand++;
            else if (val == 0x73 || (val >= 0x8F && val <= 0x9A) || val == 0x6D || val == 0x79 || val == 0xB3) a.treasure++;
            if (val == TILE_BARREL) a.barrels++;
            if (val == TILE_MINE) a.mines++;
            // Monster spawns (furry, grenadier, slime, alien)
            if ((val >= 0x74 && val <= 0x78) || val == 0x7A || val == 0x7B) a.monsters++;
        }
    }
    // Estimate corridors: count passages with exactly 2 opposite open neighbors
    for (int y = 1; y < 44; y++) {
        for (int x = 1; x < 63; x++) {
            uint8_t val = map[y * 66 + x];
            if (val != 0x30 && val != 0x66) continue;
            bool lr = (map[y*66+x-1] == 0x30 || map[y*66+x-1] == 0x66)
                   && (map[y*66+x+1] == 0x30 || map[y*66+x+1] == 0x66);
            bool ud = (map[(y-1)*66+x] == 0x30 || map[(y-1)*66+x] == 0x66)
                   && (map[(y+1)*66+x] == 0x30 || map[(y+1)*66+x] == 0x66);
            bool l = map[y*66+x-1] == 0x30 || map[y*66+x-1] == 0x66;
            bool r = map[y*66+x+1] == 0x30 || map[y*66+x+1] == 0x66;
            bool u = map[(y-1)*66+x] == 0x30 || map[(y-1)*66+x] == 0x66;
            bool d = map[(y+1)*66+x] == 0x30 || map[(y+1)*66+x] == 0x66;
            if ((lr && !u && !d) || (ud && !l && !r)) a.corridors++;
        }
    }
    return a;
}

// Current drill power from inventory
static int cpu_drill_power(App* app, int p) {
    return 1 + app->player_inventory[p][EQUIP_SMALL_PICKAXE]
             + 3 * app->player_inventory[p][EQUIP_LARGE_PICKAXE]
             + 5 * app->player_inventory[p][EQUIP_DRILL];
}

// Sell excess items the CPU doesn't need, freeing up cash.
// Returns true if something was sold this tick.
static bool cpu_sell_excess(App* app, int p, int* cursor, bool want_money) {
    // Items to consider selling and their max-keep counts
    static const struct { int item; int keep_money; int keep_rounds; } sell_rules[] = {
        {EQUIP_METAL_WALL,  0, 0},
        {EQUIP_BIOMASS,     0, 1},
        {EQUIP_CLONE,       0, 1},
        {EQUIP_EXTINGUISHER,0, 1},
        {EQUIP_BARREL,      0, 1},
        {EQUIP_JUMPING_BOMB,0, 2},
        {EQUIP_PLASTIC,     0, 1},
        {EQUIP_EXPLOSIVE_PLASTIC, 0, 1},
        {EQUIP_LARGE_CRUCIFIX, 0, 1},
        {EQUIP_SMALL_CRUCIFIX, 0, 2},
        {EQUIP_FLAMETHROWER,0, 1},
        {EQUIP_GRENADE,     0, 2},
        {EQUIP_LARGE_RADIO, 0, 2},
        {EQUIP_SMALL_RADIO, 0, 2},
        // In money mode, sell excess combat items
        {EQUIP_BIG_BOMB,    2, 99},
        {EQUIP_NAPALM,      0, 99},
        {EQUIP_ATOMIC_BOMB, 0, 1},
    };
    int nrules = sizeof(sell_rules) / sizeof(sell_rules[0]);
    for (int i = 0; i < nrules; i++) {
        int item = sell_rules[i].item;
        int keep = want_money ? sell_rules[i].keep_money : sell_rules[i].keep_rounds;
        if (app->player_inventory[p][item] > keep) {
            *cursor = item;
            shop_try_sell(app, p, item);
            return true;
        }
    }
    return false;
}

// Shopping state machine: CPU takes one action per tick for visual effect.
// Phase 1: Sell excess items to raise cash.
// Phase 2: Ensure minimum drill power (never start at 1).
// Phase 3: Buy armor progressively.
// Phase 4: Buy weapons/tools based on strategy + level analysis.
void cpu_shop_tick(App* app, int p, int* cursor, bool* ready) {
    if (*ready) return;

    // Throttle: wait between actions
    if (cpu_shop_delay[p] > 0) { cpu_shop_delay[p]--; return; }
    cpu_shop_delay[p] = cpu_random_delay(CPU_SHOP_MIN_DELAY, CPU_SHOP_MAX_DELAY);

    bool want_money = app->options.win_by_money;

    // Phase 1: Sell excess items first
    if (cpu_sell_excess(app, p, cursor, want_money)) return;

    uint32_t cash = app->player_cash[p];

    // Analyze upcoming level if available
    LevelAnalysis lv = {0};
    bool has_preview = false;
    if (app->level_count > 0) {
        int lvl_idx;
        if (app->selected_level_count > 0 && app->current_round < app->selected_level_count) {
            int sel = app->selected_levels[app->current_round];
            lvl_idx = (sel == 0) ? (app->current_round % app->level_count) : (sel - 1);
        } else {
            lvl_idx = app->current_round % app->level_count;
        }
        if (lvl_idx >= 0 && lvl_idx < app->level_count && app->level_data[lvl_idx]) {
            lv = cpu_analyze_level(app->level_data[lvl_idx]);
            has_preview = true;
        }
    }

    bool stony    = has_preview && lv.stone > 400;
    bool sandy    = has_preview && lv.sand > 600;
    bool rich     = has_preview && lv.treasure > 20;
    bool wide     = has_preview && lv.open > 800;
    bool corridor = has_preview && lv.corridors > 100;
    bool monsters = has_preview && lv.monsters > 3;
    bool mined    = has_preview && lv.mines > 5;
    bool barrelly = has_preview && lv.barrels > 3;

    // Phase 2: Ensure minimum drill power — never start at 1
    int drill = cpu_drill_power(app, p);
    int drill_target = stony ? 8 : 4;
    if (drill < drill_target) {
        if (drill_target - drill >= 5 && cash >= EQUIPMENT_PRICES[EQUIP_DRILL]) {
            *cursor = EQUIP_DRILL; shop_try_buy(app, p, EQUIP_DRILL); return;
        }
        if (drill_target - drill >= 3 && cash >= EQUIPMENT_PRICES[EQUIP_LARGE_PICKAXE]) {
            *cursor = EQUIP_LARGE_PICKAXE; shop_try_buy(app, p, EQUIP_LARGE_PICKAXE); return;
        }
        if (cash >= EQUIPMENT_PRICES[EQUIP_SMALL_PICKAXE]) {
            *cursor = EQUIP_SMALL_PICKAXE; shop_try_buy(app, p, EQUIP_SMALL_PICKAXE); return;
        }
    }

    // Phase 3: Armor — more in dangerous levels
    {
        int armor = app->player_inventory[p][EQUIP_ARMOR];
        int armor_target = want_money ? 1 : (monsters ? 3 : 2);
        if (armor < armor_target && cash >= EQUIPMENT_PRICES[EQUIP_ARMOR] + 200) {
            *cursor = EQUIP_ARMOR; shop_try_buy(app, p, EQUIP_ARMOR); return;
        }
    }

    // Phase 4: Weapons/tools based on strategy + level features
    typedef struct { int item; int want; } BuyGoal;
    BuyGoal goals[24];
    int ngoals = 0;

    #define G(itm, n) do { int _w = (n) - app->player_inventory[p][(itm)]; \
        if (_w > 0) goals[ngoals++] = (BuyGoal){(itm), _w}; } while(0)

    if (want_money) {
        // Mining essentials
        if (stony) { G(EQUIP_LARGE_PICKAXE, 2); G(EQUIP_DYNAMITE, 4); G(EQUIP_DIGGER, 2); }
        G(EQUIP_SMALL_BOMB, sandy ? 3 : 5);
        G(EQUIP_DYNAMITE, 2);
        G(EQUIP_MINE, 3);
        if (rich) G(EQUIP_TELEPORT, 1);
        // Corridor defense: flamethrower/grenade to fend off attackers
        if (corridor) { G(EQUIP_FLAMETHROWER, 1); G(EQUIP_GRENADE, 2); }
        // Monster defense
        if (monsters) { G(EQUIP_GRENADE, 2); G(EQUIP_MINE, 5); }
        // Extinguisher to defuse enemy bombs
        if (mined || !want_money) G(EQUIP_EXTINGUISHER, 1);
        // Late-game upgrades
        if (cash > 1500) G(EQUIP_SUPER_DRILL, 1);
    } else {
        // Combat loadout
        int bomb_want = wide ? 10 : 6;
        int big_want = wide ? 4 : 2;
        int mine_want = wide ? 5 : 3;
        G(EQUIP_SMALL_BOMB, bomb_want);
        G(EQUIP_BIG_BOMB, big_want);
        G(EQUIP_DYNAMITE, 3);
        G(EQUIP_MINE, mine_want);
        // Corridor weapons: grenades and flamethrower are devastating
        if (corridor) { G(EQUIP_FLAMETHROWER, 2); G(EQUIP_GRENADE, 3); }
        else { G(EQUIP_GRENADE, 1); }
        // Wide open: napalm spreads well
        if (wide) G(EQUIP_NAPALM, 2);
        // Extinguisher: defuse own/enemy bombs in tight spots
        G(EQUIP_EXTINGUISHER, 1);
        // Radio bombs for tactical placement
        if (cash > 600) G(EQUIP_LARGE_RADIO, 2);
        if (cash > 800) G(EQUIP_SMALL_RADIO, 2);
        // Heavy weapons with surplus cash
        if (cash > 1200) G(EQUIP_ATOMIC_BOMB, 1);
        // Digger to tunnel through defenses
        if (stony) G(EQUIP_DIGGER, 2);
        // Barrel-heavy maps: barrel can be pushed into enemies then detonated
        if (barrelly) G(EQUIP_SMALL_BOMB, bomb_want + 3);
        // Super drill for emergency escape
        if (cash > 1000) G(EQUIP_SUPER_DRILL, 1);
    }
    #undef G

    // Try to buy one item per tick
    for (int i = 0; i < ngoals; i++) {
        if (goals[i].want <= 0) continue;
        if (EQUIPMENT_PRICES[goals[i].item] <= cash) {
            *cursor = goals[i].item;
            shop_try_buy(app, p, goals[i].item);
            return;
        }
    }

    // Nothing left to buy — mark ready
    *cursor = EQUIP_TOTAL;
    *ready = true;
}

// ==================== CPU Gameplay AI ====================

static const int DDX[] = {0, 0, -1, 1};
static const int DDY[] = {-1, 1, 0, 0};
static const uint8_t DIR_FLAGS[] = {NET_INPUT_UP, NET_INPUT_DOWN, NET_INPUT_LEFT, NET_INPUT_RIGHT};

static bool cpu_is_bomb(uint8_t val) {
    return (val >= 0x32 && val <= 0x36) || (val >= 0x47 && val <= 0x55)
        || val == 0x56 || val == 0x57 || val == 0xA0 || val == 0xA1
        || val == 0xA2 || val == 0xA3 || val == 0xAB;
}

static bool cpu_is_treasure(uint8_t val) {
    return (val >= 0x8F && val <= 0x9A) || val == 0x6D;
}

static bool cpu_is_valuable(uint8_t val) {
    return cpu_is_treasure(val) || val == 0x73 || val == 0x79; // treasure, crate, medikit
}

static bool cpu_walkable(uint8_t val) {
    if (is_passable(val)) return true;
    if (cpu_is_treasure(val)) return true;
    if (val >= 0x37 && val <= 0x39) return true;
    if (val >= 0x8B && val <= 0x8E) return true;
    if (val == 0x79 || val == 0xB3) return true;
    if (val == 0x73) return true;
    return false;
}

static bool cpu_diggable(uint8_t val) {
    if (is_stone(val)) return true;
    if (val == 0x37 || val == 0x38 || val == 0x39) return true;
    if (val >= 0x40 && val <= 0x46) return true;
    if (val == 0xAC || val == 0xAD || val == 0xAE) return true;
    if (val == 0x70 || val == 0x71) return true;
    return false;
}

static bool cpu_bombable(uint8_t val) {
    if (cpu_diggable(val)) return true;
    if (val == TILE_WALL || val == TILE_BARREL || val == 0xA9) return false;
    return !cpu_walkable(val) && !is_passable(val);
}

// Get blast radius of a bomb tile. Returns 0 if not a timed bomb.
static int cpu_bomb_radius(uint8_t val) {
    // Small bombs (3 animation frames)
    if (val == 0x32 || val == 0x33 || val == 0x34) return 2;
    // Big bombs
    if (val == 0x35 || val == 0x36 || val == 0x47) return 3;
    // Dynamite
    if (val == 0x48 || val == 0x49 || val == 0x4A) return 4;
    // Atomic/nuke
    if (val == 0x9D || val == 0x9E || val == 0x9F) return 7;
    // Napalm (spreads, treat as radius 5)
    if (val == 0xA3 || val == 0x57) return 5;
    // Small crucifix (cross rays)
    if (val == 0x4B) return 4;
    // Large crucifix (long cross rays)
    if (val == 0x4C) return 6;
    // Plastic explosive (fills area)
    if (val == 0xA0 || val == 0xA1) return 4;
    // Digger bomb (line)
    if (val == 0xA2) return 3;
    // Barrel (explodes in chain, big bomb equivalent)
    if (val == TILE_BARREL) return 3;
    // Jumping bomb
    if (val == 0xAB) return 3;
    // Small/big radio (dormant until triggered, timer=0)
    if (val == 0x63 || val == 0x82 || val == 0x67 || val == 0x69) return 2;
    if (val == 0x64 || val == 0x83 || val == 0x68 || val == 0x6A) return 3;
    // Mine (instant on contact)
    if (val == TILE_MINE) return 2;
    return 2; // unknown bomb type, assume small
}

// How dangerous is this cell? Models cross-pattern blasts and area bombs.
static int cpu_danger(World* world, int cx, int cy) {
    if (cx < 0 || cx >= MAP_WIDTH || cy < 0 || cy >= MAP_HEIGHT) return 999;
    int sev = 0;
    // Scan along cross axes (max range 7 for nukes)
    for (int axis = 0; axis < 2; axis++) {
        for (int sign = -1; sign <= 1; sign += 2) {
            for (int dist = 0; dist <= 7; dist++) {
                int nx = cx + (axis == 0 ? sign * dist : 0);
                int ny = cy + (axis == 1 ? sign * dist : 0);
                if (nx < 0 || nx >= MAP_WIDTH || ny < 0 || ny >= MAP_HEIGHT) break;
                uint8_t tile = world->tiles[ny][nx];
                if (cpu_is_bomb(tile) && world->timer[ny][nx] > 0 && world->timer[ny][nx] < 60) {
                    int radius = cpu_bomb_radius(tile);
                    if (dist <= radius) {
                        int urgency = 60 - world->timer[ny][nx];
                        sev += urgency * (radius - dist + 1);
                    }
                }
                // Barrel in blast path = chain explosion risk
                if (tile == TILE_BARREL && dist > 0) {
                    // Check if any bomb can reach this barrel
                    for (int bd = 1; bd <= 4; bd++) {
                        int bx = nx + (axis == 0 ? -sign * bd : 0);
                        int by = ny + (axis == 1 ? -sign * bd : 0);
                        if (bx >= 0 && bx < MAP_WIDTH && by >= 0 && by < MAP_HEIGHT
                            && cpu_is_bomb(world->tiles[by][bx]) && world->timer[by][bx] > 0 && world->timer[by][bx] < 60)
                            sev += 30;
                    }
                }
                // Wall stops blast propagation in this direction
                if (tile == TILE_WALL && dist > 0) break;
            }
        }
    }
    // Area bombs nearby (plastic, napalm expand — not just cross)
    for (int dy = -4; dy <= 4; dy++)
        for (int dx = -4; dx <= 4; dx++) {
            int nx = cx + dx, ny = cy + dy;
            if (nx < 0 || nx >= MAP_WIDTH || ny < 0 || ny >= MAP_HEIGHT) continue;
            uint8_t tile = world->tiles[ny][nx];
            // Plastic/napalm expand to fill; treat as area danger
            if ((tile == 0xA0 || tile == 0xA1 || tile == 0xA3 || tile == 0x57)
                && world->timer[ny][nx] > 0 && world->timer[ny][nx] < 60) {
                int dist = abs(dx) + abs(dy);
                if (dist <= 4) sev += (60 - world->timer[ny][nx]) * 2;
            }
        }
    return sev;
}

// Per-player state
static uint8_t cpu_visited[MAX_PLAYERS][MAP_HEIGHT][MAP_WIDTH];
static int cpu_last_dir[MAX_PLAYERS];
static bool cpu_flee_mode[MAX_PLAYERS]; // set after placing a bomb — flee next tick

// Decay visited map (call periodically)
static void cpu_decay_visited(int p) {
    for (int y = 0; y < MAP_HEIGHT; y++)
        for (int x = 0; x < MAP_WIDTH; x++)
            if (cpu_visited[p][y][x] > 0) cpu_visited[p][y][x]--;
}

// ==================== A* Pathfinder ====================

#define ASTAR_MAX 2970
typedef struct {
    int16_t x, y, g, f;
    int16_t parent;
    uint8_t dir;
    bool first_bomb;
} AStarNode;
typedef bool (*GoalFunc)(World*, int, int, void*);
typedef struct { uint8_t dir; bool needs_bomb; } AStarResult;

static AStarResult cpu_astar(World* world, int sx, int sy, GoalFunc goal, void* ctx,
                              int drill, bool bombs, int max_steps, int player) {
    AStarResult none = {0, false};
    static uint8_t vis[MAP_HEIGHT][MAP_WIDTH];
    static AStarNode open[ASTAR_MAX];
    memset(vis, 0, sizeof(vis));

    int cnt = 0;
    open[cnt++] = (AStarNode){sx, sy, 0, 0, -1, 0, false};
    vis[sy][sx] = 1;
    int head = 0, steps = 0;

    while (head < cnt && steps < max_steps) {
        int best = head;
        for (int i = head + 1; i < cnt; i++)
            if (open[i].f < open[best].f) best = i;
        if (best != head) { AStarNode t = open[head]; open[head] = open[best]; open[best] = t; }
        AStarNode cur = open[head++];
        steps++;

        for (int d = 0; d < 4; d++) {
            int nx = cur.x + DDX[d], ny = cur.y + DDY[d];
            if (nx < 0 || nx >= MAP_WIDTH || ny < 0 || ny >= MAP_HEIGHT) continue;
            if (vis[ny][nx]) continue;

            uint8_t tile = world->tiles[ny][nx];

            // Darkness: fogged tiles are unknowns
            if (world->darkness && !world->fog[ny][nx]) {
                vis[ny][nx] = 1;
                int ng = cur.g + 5;
                uint8_t fd = (cur.parent == -1) ? DIR_FLAGS[d] : cur.dir;
                bool fb = (cur.parent == -1) ? false : cur.first_bomb;
                if (cnt < ASTAR_MAX) open[cnt++] = (AStarNode){nx, ny, ng, ng, head-1, fd, fb};
                continue;
            }

            // Mine blindness
            if (tile == TILE_MINE && (rand() % 100) < 30) tile = TILE_PASSAGE;

            bool first = (cur.parent == -1);
            int cost = 0;
            bool bomb_step = false;

            if (cpu_walkable(tile)) {
                cost = 1;
            } else if (tile == TILE_MINE) {
                cost = 12;
            } else if (bombs && cpu_bombable(tile)) {
                // Prefer bombing over digging — clears multiple tiles
                cost = 5;
                bomb_step = true;
            } else if (drill > 0 && cpu_diggable(tile)) {
                cost = is_stone(tile) ? 10 : 4;
            } else if (tile == TILE_BARREL) {
                int bx = nx + DDX[d], by = ny + DDY[d];
                if (bx >= 0 && bx < MAP_WIDTH && by >= 0 && by < MAP_HEIGHT && is_passable(world->tiles[by][bx]))
                    cost = 4;
                else continue;
            } else {
                continue;
            }

            if (cpu_is_bomb(tile)) continue;
            int dng = cpu_danger(world, nx, ny);
            if (dng > 0) cost += 15 + dng / 5;

            // Heavy penalty for tiles occupied by other players
            for (int op = 0; op < world->num_players; op++) {
                if (op == player || world->actors[op].is_dead) continue;
                if (world->actors[op].pos.x / 10 == nx && (world->actors[op].pos.y - 30) / 10 == ny)
                    cost += 30;
            }

            // Avoid monster-occupied tiles and tiles adjacent to monsters (skip own clones)
            for (int m = world->num_players; m < world->num_actors; m++) {
                if (world->actors[m].is_dead || !world->actors[m].is_active) continue;
                if (world->actors[m].kind == ACTOR_CLONE && world->actors[m].clone_owner == player) continue;
                int mx = world->actors[m].pos.x / 10;
                int my = (world->actors[m].pos.y - 30) / 10;
                int md = abs(mx - nx) + abs(my - ny);
                if (md == 0) cost += 50;       // on monster tile
                else if (md == 1) cost += 20;  // adjacent to monster
                else if (md == 2) cost += 5;   // near monster
            }

            // Penalize recently visited tiles to prevent oscillation
            if (player >= 0 && player < MAX_PLAYERS)
                cost += cpu_visited[player][ny][nx] * 3;

            int ng = cur.g + cost;
            uint8_t fd = first ? DIR_FLAGS[d] : cur.dir;
            bool fb = first ? bomb_step : cur.first_bomb;

            if (goal(world, nx, ny, ctx)) return (AStarResult){fd, fb};
            if (cnt < ASTAR_MAX) { vis[ny][nx] = 1; open[cnt++] = (AStarNode){nx, ny, ng, ng, head-1, fd, fb}; }
        }
    }
    return none;
}

static bool goal_treasure(World* w, int x, int y, void* c) { (void)c; return cpu_is_treasure(w->tiles[y][x]); }
static bool goal_medikit(World* w, int x, int y, void* c) { (void)c; return w->tiles[y][x] == 0x79; }
typedef struct { int ex, ey; } EnemyGoalCtx;
static bool goal_near_enemy(World* w, int x, int y, void* c) {
    (void)w; EnemyGoalCtx* e = c; return abs(x - e->ex) + abs(y - e->ey) <= 2;
}
// Goal: any unvisited passable area (exploration)
typedef struct { int player; } ExploreCtx;
static bool goal_explore(World* w, int x, int y, void* c) {
    ExploreCtx* ec = c;
    if (!cpu_walkable(w->tiles[y][x])) return false;
    return cpu_visited[ec->player][y][x] == 0;
}

// Select weapon, returns CYCLE or 0
static uint8_t cpu_select(App* app, Actor* a, int p, int w) {
    if (app->player_inventory[p][w] <= 0 || a->selected_weapon == w) return 0;
    return NET_INPUT_CYCLE;
}

// Place a bomb and flag flee mode for next tick
static uint8_t cpu_place_bomb(int p) {
    cpu_flee_mode[p] = true;
    return NET_INPUT_ACTION;
}

// Can the CPU safely place a bomb at (cx,cy) and escape the blast?
// Checks that at least one adjacent walkable tile is outside the bomb's cross-blast.
static bool cpu_can_bomb_safely(World* world, int cx, int cy, int blast_radius) {
    // Current tile must be passage or treasure (game requires this for placement)
    uint8_t here = world->tiles[cy][cx];
    if (here != TILE_PASSAGE && !cpu_is_treasure(here) && here != 0xAF) return false;

    for (int d = 0; d < 4; d++) {
        int nx = cx + DDX[d], ny = cy + DDY[d];
        if (nx < 0 || nx >= MAP_WIDTH || ny < 0 || ny >= MAP_HEIGHT) continue;
        if (!cpu_walkable(world->tiles[ny][nx])) continue;
        // Is (nx,ny) outside the bomb's cross-blast from (cx,cy)?
        // Bomb at (cx,cy) blasts along same row and column.
        // (nx,ny) is one step away. It's in the cross if nx==cx or ny==cy.
        // Since we moved one step, we're at dist 1. Check if we can keep going
        // to get out of blast range.
        bool safe = false;
        // Continue in this direction to see if we can reach outside blast radius
        for (int step = 1; step <= blast_radius + 1; step++) {
            int sx = cx + DDX[d] * step, sy = cy + DDY[d] * step;
            if (sx < 0 || sx >= MAP_WIDTH || sy < 0 || sy >= MAP_HEIGHT) break;
            if (step > blast_radius) { safe = true; break; } // past blast radius
            if (!cpu_walkable(world->tiles[sy][sx])) break; // blocked
        }
        // Or: step perpendicular (off the blast axis) from (nx,ny)
        if (!safe) {
            for (int d2 = 0; d2 < 4; d2++) {
                int px = nx + DDX[d2], py = ny + DDY[d2];
                if (px < 0 || px >= MAP_WIDTH || py < 0 || py >= MAP_HEIGHT) continue;
                if (!cpu_walkable(world->tiles[py][px])) continue;
                // (px,py) is off the cross axis if px != cx AND py != cy
                if (px != cx && py != cy) { safe = true; break; }
            }
        }
        if (safe) return true;
    }
    return false;
}

// Pick best bomb for clearing an obstacle; prefers small bomb near valuables
static int cpu_pick_bomb(App* app, World* world, int p, int tx, int ty) {
    // Check if treasure/crate is nearby — use small bomb to avoid destroying it
    bool valuable_near = false;
    for (int dy = -3; dy <= 3; dy++)
        for (int dx = -3; dx <= 3; dx++) {
            int nx = tx + dx, ny = ty + dy;
            if (nx >= 0 && nx < MAP_WIDTH && ny >= 0 && ny < MAP_HEIGHT && cpu_is_valuable(world->tiles[ny][nx]))
                valuable_near = true;
        }
    if (valuable_near && app->player_inventory[p][EQUIP_SMALL_BOMB] > 0) return EQUIP_SMALL_BOMB;
    if (is_stone(world->tiles[ty][tx]) && app->player_inventory[p][EQUIP_DIGGER] > 0) return EQUIP_DIGGER;
    if (is_stone(world->tiles[ty][tx]) && app->player_inventory[p][EQUIP_DYNAMITE] > 0) return EQUIP_DYNAMITE;
    if (app->player_inventory[p][EQUIP_SMALL_BOMB] > 0) return EQUIP_SMALL_BOMB;
    if (app->player_inventory[p][EQUIP_BIG_BOMB] > 0) return EQUIP_BIG_BOMB;
    return -1;
}

static bool cpu_line_of_sight(World* world, int cx, int cy, int tx, int ty) {
    if (cx == tx) {
        int lo = cy < ty ? cy : ty, hi = cy < ty ? ty : cy;
        for (int y = lo + 1; y < hi; y++) if (!cpu_walkable(world->tiles[y][cx])) return false;
        return true;
    }
    if (cy == ty) {
        int lo = cx < tx ? cx : tx, hi = cx < tx ? tx : cx;
        for (int x = lo + 1; x < hi; x++) if (!cpu_walkable(world->tiles[cy][x])) return false;
        return true;
    }
    return false;
}

// Face toward a target position
static uint8_t cpu_face_toward(Actor* a, int cx, int cy, int tx, int ty) {
    if (tx > cx && a->facing != DIR_RIGHT) return NET_INPUT_RIGHT | NET_INPUT_STOP;
    if (tx < cx && a->facing != DIR_LEFT)  return NET_INPUT_LEFT | NET_INPUT_STOP;
    if (ty > cy && a->facing != DIR_DOWN)  return NET_INPUT_DOWN | NET_INPUT_STOP;
    if (ty < cy && a->facing != DIR_UP)    return NET_INPUT_UP | NET_INPUT_STOP;
    return 0;
}

// ==================== Main CPU Decision Loop ====================

uint8_t cpu_generate_input(App* app, World* world, int p) {
    Actor* actor = &world->actors[p];
    if (actor->is_dead) return 0;

    int cx = actor->pos.x / 10;
    int cy = (actor->pos.y - 30) / 10;
    if (cx < 0 || cx >= MAP_WIDTH || cy < 0 || cy >= MAP_HEIGHT) return 0;

    if (actor->pos.x % 10 != 5 || (actor->pos.y - 30) % 10 != 5) return 0;

    // Mark visited — stronger penalty to prevent oscillation
    if (cpu_visited[p][cy][cx] < 250) cpu_visited[p][cy][cx] += 40;
    // Faster decay
    if (world->round_counter % 30 == 0) cpu_decay_visited(p);

    int danger = cpu_danger(world, cx, cy);
    bool in_danger = danger > 0;

    // Flee mode: after placing a bomb, immediately flee regardless of think delay
    if (cpu_flee_mode[p]) {
        cpu_flee_mode[p] = false;
        int best = -1, best_score = -9999;
        for (int d = 0; d < 4; d++) {
            int nx = cx + DDX[d], ny = cy + DDY[d];
            if (nx < 0 || nx >= MAP_WIDTH || ny < 0 || ny >= MAP_HEIGHT) continue;
            if (!cpu_walkable(world->tiles[ny][nx])) continue;
            int score = -cpu_danger(world, nx, ny);
            // Also factor in monster distance (prefer away from monsters)
            for (int m = world->num_players; m < world->num_actors; m++) {
                if (world->actors[m].is_dead || !world->actors[m].is_active) continue;
                if (world->actors[m].kind == ACTOR_CLONE && world->actors[m].clone_owner == p) continue;
                int mx = world->actors[m].pos.x / 10, my = (world->actors[m].pos.y - 30) / 10;
                score += abs(nx - mx) + abs(ny - my); // prefer farther from monsters
            }
            if (score > best_score) { best_score = score; best = d; }
        }
        if (best >= 0) return DIR_FLAGS[best];
    }

    if (!in_danger) {
        if (cpu_think_delay[p] > 0) { cpu_think_delay[p]--; return 0; }
        cpu_think_delay[p] = cpu_random_delay(CPU_THINK_MIN_DELAY, CPU_THINK_MAX_DELAY);
    }

    bool has_bombs = false;
    for (int w = 0; w < EQUIP_TOTAL; w++)
        if (w != EQUIP_SMALL_PICKAXE && w != EQUIP_LARGE_PICKAXE && w != EQUIP_DRILL && w != EQUIP_ARMOR)
            if (app->player_inventory[p][w] > 0) { has_bombs = true; break; }
    int drill = cpu_drill_power(app, p);

    // Safety: check escape for the actual selected weapon's blast radius
    int sel_blast = 2; // default small bomb
    switch (actor->selected_weapon) {
        case EQUIP_BIG_BOMB: case EQUIP_LARGE_RADIO: sel_blast = 3; break;
        case EQUIP_DYNAMITE: case EQUIP_NAPALM: sel_blast = 4; break;
        case EQUIP_ATOMIC_BOMB: sel_blast = 6; break;
        default: sel_blast = 2; break;
    }
    bool safe_to_bomb = has_bombs && cpu_can_bomb_safely(world, cx, cy, sel_blast);

    // ===== 1. DANGER: flee from bombs, use super drill if trapped =====
    if (in_danger) {
        int best = -1, best_sev = danger;
        for (int d = 0; d < 4; d++) {
            int nx = cx + DDX[d], ny = cy + DDY[d];
            if (nx < 0 || nx >= MAP_WIDTH || ny < 0 || ny >= MAP_HEIGHT) continue;
            if (!cpu_walkable(world->tiles[ny][nx]) && !cpu_diggable(world->tiles[ny][nx])) continue;
            int sev = cpu_danger(world, nx, ny);
            if (sev < best_sev) { best_sev = sev; best = d; }
        }
        if (best >= 0) { cpu_last_dir[p] = best; return DIR_FLAGS[best]; }

        // Trapped: super drill to escape
        if (app->player_inventory[p][EQUIP_SUPER_DRILL] > 0 && actor->super_drill_count == 0) {
            uint8_t sw = cpu_select(app, actor, p, EQUIP_SUPER_DRILL);
            return sw ? sw : cpu_place_bomb(p);
        }
        // Extinguisher: defuse adjacent bomb if timer allows
        if (app->player_inventory[p][EQUIP_EXTINGUISHER] > 0) {
            for (int d = 0; d < 4; d++) {
                int nx = cx + DDX[d], ny = cy + DDY[d];
                if (nx >= 0 && nx < MAP_WIDTH && ny >= 0 && ny < MAP_HEIGHT
                    && cpu_is_bomb(world->tiles[ny][nx]) && world->timer[ny][nx] > 10) {
                    uint8_t face = cpu_face_toward(actor, cx, cy, nx, ny);
                    if (face) return face;
                    uint8_t sw = cpu_select(app, actor, p, EQUIP_EXTINGUISHER);
                    return sw ? sw : cpu_place_bomb(p);
                }
            }
        }
        return DIR_FLAGS[rand() % 4];
    }

    // ===== 1b. MONSTER THREAT: survive monster encounters =====
    // Monsters are actors[num_players..num_actors-1]. Skip own clones.
    // Enemy clones are treated as hostile — attack them.
    {
        int nearest_mdist = 999, nearest_mx = 0, nearest_my = 0;
        bool nearest_is_enemy_clone = false;
        for (int m = world->num_players; m < world->num_actors; m++) {
            if (world->actors[m].is_dead || !world->actors[m].is_active) continue;
            if (world->actors[m].kind == ACTOR_CLONE && world->actors[m].clone_owner == p) continue;
            int mx = world->actors[m].pos.x / 10;
            int my = (world->actors[m].pos.y - 30) / 10;
            int md = abs(mx - cx) + abs(my - cy);
            if (md < nearest_mdist) {
                nearest_mdist = md; nearest_mx = mx; nearest_my = my;
                nearest_is_enemy_clone = (world->actors[m].kind == ACTOR_CLONE);
            }
        }

        if (nearest_mdist <= 3) {
            // Monster/enemy clone is very close — fight or flight
            // Enemy clones: always fight aggressively (they're dangerous and worth killing)
            // Regular monsters: fight if possible, flee if not

            // Try ranged weapon first if in line of sight
            bool mlos = cpu_line_of_sight(world, cx, cy, nearest_mx, nearest_my);
            if (mlos && nearest_mdist >= 2) {
                if (app->player_inventory[p][EQUIP_FLAMETHROWER] > 0) {
                    uint8_t face = cpu_face_toward(actor, cx, cy, nearest_mx, nearest_my);
                    if (face) return face;
                    uint8_t sw = cpu_select(app, actor, p, EQUIP_FLAMETHROWER);
                    return sw ? sw : cpu_place_bomb(p);
                }
                if (app->player_inventory[p][EQUIP_GRENADE] > 0) {
                    uint8_t face = cpu_face_toward(actor, cx, cy, nearest_mx, nearest_my);
                    if (face) return face;
                    uint8_t sw = cpu_select(app, actor, p, EQUIP_GRENADE);
                    return sw ? sw : cpu_place_bomb(p);
                }
            }

            // Adjacent monster/clone: place a bomb and flee, or just run
            // Enemy clones: always bomb if safe (they fight back hard)
            if (nearest_mdist <= 1) {
                if (nearest_is_enemy_clone && safe_to_bomb) {
                    if (app->player_inventory[p][EQUIP_MINE] > 0) {
                        uint8_t sw = cpu_select(app, actor, p, EQUIP_MINE);
                        if (sw) return sw;
                    }
                    return cpu_place_bomb(p);
                }
                int best_flee = -1, best_flee_dist = 0;
                for (int d = 0; d < 4; d++) {
                    int nx = cx + DDX[d], ny = cy + DDY[d];
                    if (nx < 0 || nx >= MAP_WIDTH || ny < 0 || ny >= MAP_HEIGHT) continue;
                    if (!cpu_walkable(world->tiles[ny][nx])) continue;
                    int fd = abs(nx - nearest_mx) + abs(ny - nearest_my);
                    if (fd > best_flee_dist) { best_flee_dist = fd; best_flee = d; }
                }
                // Drop a mine/bomb on our way out if safe
                if (best_flee >= 0 && safe_to_bomb) {
                    if (app->player_inventory[p][EQUIP_MINE] > 0) {
                        uint8_t sw = cpu_select(app, actor, p, EQUIP_MINE);
                        if (sw) return sw;
                        cpu_flee_mode[p] = true;
                        return NET_INPUT_ACTION;
                    }
                    if (app->player_inventory[p][actor->selected_weapon] > 0) {
                        cpu_flee_mode[p] = true;
                        return NET_INPUT_ACTION;
                    }
                }
                if (best_flee >= 0) return DIR_FLAGS[best_flee];
            }

            // Monster at 2-3 tiles: place bomb between us if safe, then retreat
            if (nearest_mdist <= 3 && safe_to_bomb && has_bombs) {
                // Face toward monster and place bomb
                uint8_t face = cpu_face_toward(actor, cx, cy, nearest_mx, nearest_my);
                if (face) return face;
                return cpu_place_bomb(p);
            }

            // Can't fight — just run away
            {
                int best = -1, best_dist = 0;
                for (int d = 0; d < 4; d++) {
                    int nx = cx + DDX[d], ny = cy + DDY[d];
                    if (nx < 0 || nx >= MAP_WIDTH || ny < 0 || ny >= MAP_HEIGHT) continue;
                    if (!cpu_walkable(world->tiles[ny][nx])) continue;
                    int fd = abs(nx - nearest_mx) + abs(ny - nearest_my);
                    if (fd > best_dist) { best_dist = fd; best = d; }
                }
                if (best >= 0) return DIR_FLAGS[best];
            }
        }

        // Monster at 4-6 tiles: be aware, factor into pathfinding decisions
        // (handled by A* which avoids monster positions via player-occupied cost)
    }

    // ===== 1c. UNSTICK: if adjacent to another CPU player, move away =====
    {
        bool stuck_with_cpu = false;
        int stuck_ox = 0, stuck_oy = 0;
        for (int i = 0; i < world->num_players; i++) {
            if (i == p || world->actors[i].is_dead || !is_cpu_player(app, i)) continue;
            int ox = world->actors[i].pos.x / 10, oy = (world->actors[i].pos.y - 30) / 10;
            if (abs(ox - cx) + abs(oy - cy) <= 1) {
                stuck_with_cpu = true; stuck_ox = ox; stuck_oy = oy; break;
            }
        }
        if (stuck_with_cpu) {
            // Move away from the other CPU: pick direction that increases distance
            int best = -1, best_dist = 0;
            for (int d = 0; d < 4; d++) {
                int nx = cx + DDX[d], ny = cy + DDY[d];
                if (nx < 0 || nx >= MAP_WIDTH || ny < 0 || ny >= MAP_HEIGHT) continue;
                if (!cpu_walkable(world->tiles[ny][nx])) continue;
                int dist = abs(nx - stuck_ox) + abs(ny - stuck_oy);
                // Also avoid tiles with other players
                bool occupied = false;
                for (int j = 0; j < world->num_players; j++) {
                    if (j == p || world->actors[j].is_dead) continue;
                    if (world->actors[j].pos.x / 10 == nx && (world->actors[j].pos.y - 30) / 10 == ny)
                        occupied = true;
                }
                if (!occupied && dist > best_dist) { best_dist = dist; best = d; }
            }
            if (best >= 0) { cpu_last_dir[p] = best; return DIR_FLAGS[best]; }
        }
    }

    // ===== 1c. REMOTE BOMBS: check if any placed radio has a target in blast range =====
    {
        // Find all placed radios belonging to this player
        // Small radio blast = 2 cross, big radio blast = 3 cross
        uint8_t sr[4], br[4]; // radio tile values per player
        sr[0]=0x63; sr[1]=0x82; sr[2]=0x67; sr[3]=0x69;
        br[0]=0x64; br[1]=0x83; br[2]=0x68; br[3]=0x6A;
        bool should_trigger = false;

        for (int ry = 0; ry < MAP_HEIGHT && !should_trigger; ry++) {
            for (int rx = 0; rx < MAP_WIDTH && !should_trigger; rx++) {
                uint8_t tile = world->tiles[ry][rx];
                bool is_my_radio = (tile == sr[p] || tile == br[p]);
                if (!is_my_radio) continue;
                int blast = (tile == br[p]) ? 3 : 2;

                // Check if any enemy player or monster is in blast cross
                for (int i = 0; i < world->num_players && !should_trigger; i++) {
                    if (i == p || world->actors[i].is_dead) continue;
                    int ex = world->actors[i].pos.x / 10;
                    int ey = (world->actors[i].pos.y - 30) / 10;
                    if ((ex == rx && abs(ey - ry) <= blast) || (ey == ry && abs(ex - rx) <= blast))
                        should_trigger = true;
                }
                // Also check monsters (actors beyond num_players)
                for (int i = world->num_players; i < world->num_actors && !should_trigger; i++) {
                    if (world->actors[i].is_dead) continue;
                    int mx = world->actors[i].pos.x / 10;
                    int my = (world->actors[i].pos.y - 30) / 10;
                    if ((mx == rx && abs(my - ry) <= blast) || (my == ry && abs(mx - rx) <= blast))
                        should_trigger = true;
                }
                // Check if radio would chain-detonate other bombs (extinguished bombs, nukes near it)
                if (!should_trigger) {
                    for (int dy = -blast; dy <= blast; dy++) {
                        int ny = ry + dy;
                        if (ny < 0 || ny >= MAP_HEIGHT) continue;
                        if (ny >= 0 && ny < MAP_HEIGHT && cpu_is_bomb(world->tiles[ny][rx])) {
                            // There's a bomb in blast range — would chain. Check if that chain hits enemies.
                            for (int i = 0; i < world->num_players; i++) {
                                if (i == p || world->actors[i].is_dead) continue;
                                int ex = world->actors[i].pos.x / 10;
                                int ey = (world->actors[i].pos.y - 30) / 10;
                                if (abs(ex - rx) + abs(ey - ry) <= 5) should_trigger = true;
                            }
                        }
                    }
                    for (int dx = -blast; dx <= blast; dx++) {
                        int nx = rx + dx;
                        if (nx < 0 || nx >= MAP_WIDTH) continue;
                        if (cpu_is_bomb(world->tiles[ry][nx])) {
                            for (int i = 0; i < world->num_players; i++) {
                                if (i == p || world->actors[i].is_dead) continue;
                                int ex = world->actors[i].pos.x / 10;
                                int ey = (world->actors[i].pos.y - 30) / 10;
                                if (abs(ex - nx) + abs(ey - ry) <= 5) should_trigger = true;
                            }
                        }
                    }
                }

                // Safety: don't trigger if we're in our own blast radius
                if (should_trigger) {
                    if ((cx == rx && abs(cy - ry) <= blast) || (cy == ry && abs(cx - rx) <= blast))
                        should_trigger = false; // we'd hit ourselves
                }
            }
        }
        if (should_trigger) return NET_INPUT_REMOTE;

        // Place radio bombs near chokepoints if we have them and an enemy is nearby
        if ((app->player_inventory[p][EQUIP_LARGE_RADIO] > 0 || app->player_inventory[p][EQUIP_SMALL_RADIO] > 0)
            && is_passable(world->tiles[cy][cx])) {
            // Check if any enemy within 8 tiles — good ambush spot
            for (int i = 0; i < world->num_players; i++) {
                if (i == p || world->actors[i].is_dead) continue;
                int ex = world->actors[i].pos.x / 10;
                int ey = (world->actors[i].pos.y - 30) / 10;
                int ed = abs(ex - cx) + abs(ey - cy);
                if (ed >= 3 && ed <= 8) {
                    // Place a radio if we're in a corridor or chokepoint
                    int open_adj = 0;
                    for (int d = 0; d < 4; d++) {
                        int nx = cx + DDX[d], ny = cy + DDY[d];
                        if (nx >= 0 && nx < MAP_WIDTH && ny >= 0 && ny < MAP_HEIGHT
                            && cpu_walkable(world->tiles[ny][nx])) open_adj++;
                    }
                    if (open_adj <= 2 && (world->tiles[cy][cx] == TILE_PASSAGE || cpu_is_treasure(world->tiles[cy][cx]))) {
                        int radio = app->player_inventory[p][EQUIP_LARGE_RADIO] > 0 ? EQUIP_LARGE_RADIO : EQUIP_SMALL_RADIO;
                        uint8_t sw = cpu_select(app, actor, p, radio);
                        return sw ? sw : cpu_place_bomb(p);
                    }
                }
            }
        }
    }

    // ===== 1d. HEAL: seek medikits when low health =====
    if (actor->health < actor->max_health / 3) {
        AStarResult r = cpu_astar(world, cx, cy, goal_medikit, NULL, drill, has_bombs, 400, p);
        if (r.dir) {
            if (r.needs_bomb) {
                int tx = cx, ty = cy;
                for (int d = 0; d < 4; d++) if (DIR_FLAGS[d] == r.dir) { tx=cx+DDX[d]; ty=cy+DDY[d]; }
                int bw = cpu_pick_bomb(app, world, p, tx, ty);
                if (bw >= 0 && safe_to_bomb) { uint8_t sw = cpu_select(app, actor, p, bw); return sw ? sw : cpu_place_bomb(p); }
            }
            return r.dir;
        }
    }

    // ===== 2. COMBAT: attack nearby enemies (players + enemy clones) =====
    // Target selection: prefer richer players and closer ones; also target enemy clones
    {
        int best_e = -1, best_score = -999, bex = 0, bey = 0, best_d = 999;
        // Scan players
        for (int i = 0; i < world->num_players; i++) {
            if (i == p || world->actors[i].is_dead) continue;
            int ex = world->actors[i].pos.x / 10, ey = (world->actors[i].pos.y - 30) / 10;
            int d = abs(ex - cx) + abs(ey - cy);
            int value = (int)(app->player_cash[i] / 10) + (int)(world->cash_earned[i] / 5);
            int score = value - d * 3;
            if (score > best_score || (score == best_score && d < best_d)) {
                best_score = score; best_e = i; bex = ex; bey = ey; best_d = d;
            }
        }
        // Also consider enemy clones as targets (they're dangerous and fight)
        for (int m = world->num_players; m < world->num_actors; m++) {
            if (world->actors[m].is_dead || !world->actors[m].is_active) continue;
            if (world->actors[m].kind != ACTOR_CLONE) continue;
            if (world->actors[m].clone_owner == p) continue; // skip own clones
            int ex = world->actors[m].pos.x / 10, ey = (world->actors[m].pos.y - 30) / 10;
            int d = abs(ex - cx) + abs(ey - cy);
            int score = 20 - d * 3; // treat clones as moderate-value targets
            if (score > best_score || (score == best_score && d < best_d)) {
                best_score = score; best_e = m; bex = ex; bey = ey; best_d = d;
            }
        }
        if (best_e >= 0) {
            bool los = cpu_line_of_sight(world, cx, cy, bex, bey);

            // Flamethrower in corridor
            if (los && best_d <= 6 && app->player_inventory[p][EQUIP_FLAMETHROWER] > 0) {
                uint8_t face = cpu_face_toward(actor, cx, cy, bex, bey);
                if (face) return face;
                uint8_t sw = cpu_select(app, actor, p, EQUIP_FLAMETHROWER);
                return sw ? sw : cpu_place_bomb(p);
            }
            // Grenade at range
            if (los && best_d >= 2 && best_d <= 5 && app->player_inventory[p][EQUIP_GRENADE] > 0) {
                uint8_t face = cpu_face_toward(actor, cx, cy, bex, bey);
                if (face) return face;
                uint8_t sw = cpu_select(app, actor, p, EQUIP_GRENADE);
                return sw ? sw : cpu_place_bomb(p);
            }
            // Close range: place bomb and retreat
            if (best_d <= 2 && safe_to_bomb) {
                if (app->player_inventory[p][EQUIP_MINE] > 0) {
                    uint8_t sw = cpu_select(app, actor, p, EQUIP_MINE);
                    return sw ? sw : cpu_place_bomb(p);
                }
                if (safe_to_bomb && app->player_inventory[p][actor->selected_weapon] > 0)
                    return cpu_place_bomb(p);
                return NET_INPUT_CYCLE;
            }
            // Push bombs/barrels toward enemy
            for (int d = 0; d < 4; d++) {
                int nx = cx + DDX[d], ny = cy + DDY[d];
                if (nx < 0 || nx >= MAP_WIDTH || ny < 0 || ny >= MAP_HEIGHT) continue;
                uint8_t tile = world->tiles[ny][nx];
                int bx = nx + DDX[d], by = ny + DDY[d];
                if (bx < 0 || bx >= MAP_WIDTH || by < 0 || by >= MAP_HEIGHT) continue;
                if (!is_passable(world->tiles[by][bx])) continue;
                bool toward = (DDX[d] != 0 && bey == ny && (bex - nx) * DDX[d] > 0)
                           || (DDY[d] != 0 && bex == nx && (bey - ny) * DDY[d] > 0);
                if (toward && ((cpu_is_bomb(tile) && world->timer[ny][nx] > 30) || tile == TILE_BARREL))
                    return DIR_FLAGS[d];
            }
            // Path toward enemy: always in rounds mode; in money mode only if close
            if (!app->options.win_by_money || best_d < 10) {
                EnemyGoalCtx eg = {bex, bey};
                AStarResult r = cpu_astar(world, cx, cy, goal_near_enemy, &eg, drill, has_bombs, 600, p);
                if (r.dir) {
                    if (r.needs_bomb) {
                        int tx = cx, ty = cy;
                        for (int d = 0; d < 4; d++) if (DIR_FLAGS[d] == r.dir) { tx = cx+DDX[d]; ty = cy+DDY[d]; }
                        int bw = cpu_pick_bomb(app, world, p, tx, ty);
                        if (bw >= 0 && safe_to_bomb) { uint8_t sw = cpu_select(app, actor, p, bw); return sw ? sw : cpu_place_bomb(p); }
                    }
                    cpu_last_dir[p] = -1;
                    return r.dir;
                }
            }
        }
    }

    // ===== 3. TREASURE: find and collect =====
    {
        AStarResult r = cpu_astar(world, cx, cy, goal_treasure, NULL, drill, has_bombs, 800, p);
        if (r.dir) {
            if (r.needs_bomb) {
                int tx = cx, ty = cy;
                for (int d = 0; d < 4; d++) if (DIR_FLAGS[d] == r.dir) { tx = cx+DDX[d]; ty = cy+DDY[d]; }
                int bw = cpu_pick_bomb(app, world, p, tx, ty);
                if (bw >= 0 && safe_to_bomb) { uint8_t sw = cpu_select(app, actor, p, bw); return sw ? sw : cpu_place_bomb(p); }
            }
            cpu_last_dir[p] = -1;
            return r.dir;
        }
    }

    // ===== 4. EXPLORE: visit unvisited areas =====
    {
        ExploreCtx ec = {p};
        AStarResult r = cpu_astar(world, cx, cy, goal_explore, &ec, drill, has_bombs, 600, p);
        if (r.dir) {
            if (r.needs_bomb) {
                int tx = cx, ty = cy;
                for (int d = 0; d < 4; d++) if (DIR_FLAGS[d] == r.dir) { tx = cx+DDX[d]; ty = cy+DDY[d]; }
                int bw = cpu_pick_bomb(app, world, p, tx, ty);
                if (bw >= 0 && safe_to_bomb) { uint8_t sw = cpu_select(app, actor, p, bw); return sw ? sw : cpu_place_bomb(p); }
            }
            cpu_last_dir[p] = -1;
            return r.dir;
        }
    }

    // ===== 5. STUCK: bomb adjacent wall, avoid repeating last direction =====
    {
        int start = rand() % 4;
        for (int i = 0; i < 4; i++) {
            int d = (start + i) % 4;
            if (d == cpu_last_dir[p]) continue; // don't go back
            int nx = cx + DDX[d], ny = cy + DDY[d];
            if (nx < 0 || nx >= MAP_WIDTH || ny < 0 || ny >= MAP_HEIGHT) continue;
            if (cpu_walkable(world->tiles[ny][nx])) { cpu_last_dir[p] = d; return DIR_FLAGS[d]; }
            if (cpu_diggable(world->tiles[ny][nx]) || cpu_bombable(world->tiles[ny][nx])) {
                if (has_bombs) {
                    int bw = cpu_pick_bomb(app, world, p, nx, ny);
                    if (bw >= 0 && safe_to_bomb) { uint8_t sw = cpu_select(app, actor, p, bw); return sw ? sw : cpu_place_bomb(p); }
                }
                return DIR_FLAGS[d]; // dig through
            }
        }
    }

    if (safe_to_bomb && app->player_inventory[p][actor->selected_weapon] > 0) return cpu_place_bomb(p);
    return safe_to_bomb ? NET_INPUT_CYCLE : 0;
}
