#include "cpu.h"
#include "shop.h"
#include "net.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Timing
static int cpu_shop_delay[MAX_PLAYERS];
static int cpu_drill_power(App* app, int p);

// Shop navigation state
static int cpu_shop_target[MAX_PLAYERS];
static int cpu_shop_action[MAX_PLAYERS];
static uint32_t cpu_shop_budget[MAX_PLAYERS];
static bool cpu_shop_sold[MAX_PLAYERS][EQUIP_TOTAL];
static bool cpu_sell_done[MAX_PLAYERS]; // true once sell phase is complete — never sell again

// Per-player personality (randomized once per game, affects shopping/play style)
static int cpu_personality[MAX_PLAYERS]; // 0=balanced, 1=aggressive, 2=miser, 3=explorer
static bool cpu_personality_init[MAX_PLAYERS];
static int cpu_number_counter = 0; // running counter for unique CPU names

static const char* cpu_personality_names[] = {"Balanced", "Aggro", "Miser", "Explorer"};

void cpu_assign(int p, char* name_buf, int name_max) {
    cpu_personality[p] = rand() % 4;
    cpu_personality_init[p] = true;
    cpu_number_counter++;
    snprintf(name_buf, name_max, "%s %d", cpu_personality_names[cpu_personality[p]], cpu_number_counter);
}

#define CPU_SHOP_MIN_DELAY   3  // ~50ms cursor moves (2x faster)
#define CPU_SHOP_MAX_DELAY   7  // ~115ms
#define CPU_SHOP_ACT_DELAY   9  // ~150ms pause before buy/sell action
#define CPU_THINK_MIN_DELAY  4
#define CPU_THINK_MAX_DELAY 12

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

// Navigate cursor one step toward target item (shop grid is 4 columns)
static bool cpu_shop_navigate(int* cursor, int target) {
    if (*cursor == target) return true; // arrived
    int cx = *cursor % 4, cy = *cursor / 4;
    int tx = target % 4, ty = target / 4;
    // Move toward target: prefer vertical then horizontal
    if (cy < ty) { *cursor += 4; if (*cursor > EQUIP_TOTAL) *cursor = EQUIP_TOTAL; }
    else if (cy > ty) { *cursor -= 4; if (*cursor < 0) *cursor = 0; }
    else if (cx < tx) (*cursor)++;
    else if (cx > tx) (*cursor)--;
    return false;
}

void cpu_shop_tick(App* app, int p, int* cursor, bool* ready) {
    if (*ready) return;

    // Throttle cursor movement
    if (cpu_shop_delay[p] > 0) { cpu_shop_delay[p]--; return; }

    // Personality should be initialized by cpu_assign(); fallback just in case
    if (!cpu_personality_init[p]) { cpu_personality[p] = rand() % 4; cpu_personality_init[p] = true; }

    // Initialize budget on first tick of each shopping phase
    if (cpu_shop_budget[p] == 0) {
        uint32_t base = app->player_cash[p];
        int pers = cpu_personality[p];
        int spend_pct;
        if (app->options.win_by_money) {
            // Money mode: misers save a lot, aggressives still buy weapons
            switch (pers) {
                case 1: spend_pct = 55 + rand() % 15; break; // aggressive: 55-70%
                case 2: spend_pct = 30 + rand() % 15; break; // miser: 30-45%
                case 3: spend_pct = 45 + rand() % 20; break; // explorer: 45-65%
                default: spend_pct = 40 + rand() % 20; break; // balanced: 40-60%
            }
        } else {
            switch (pers) {
                case 1: spend_pct = 80 + rand() % 15; break; // aggressive: 80-95%
                case 2: spend_pct = 55 + rand() % 15; break; // miser: 55-70%
                case 3: spend_pct = 65 + rand() % 20; break; // explorer: 65-85%
                default: spend_pct = 70 + rand() % 20; break; // balanced: 70-90%
            }
        }
        cpu_shop_budget[p] = base * spend_pct / 100;
        cpu_shop_target[p] = -1;
        cpu_shop_action[p] = 0;
        cpu_sell_done[p] = false;
        memset(cpu_shop_sold[p], 0, sizeof(cpu_shop_sold[p]));
    }

    // If navigating to a target, move cursor toward it
    if (cpu_shop_target[p] >= 0) {
        int target = cpu_shop_target[p];
        if (!cpu_shop_navigate(cursor, target)) {
            cpu_shop_delay[p] = cpu_random_delay(CPU_SHOP_MIN_DELAY, CPU_SHOP_MAX_DELAY);
            return; // still moving cursor
        }
        // Arrived at target — perform action
        cpu_shop_delay[p] = cpu_random_delay(CPU_SHOP_ACT_DELAY, CPU_SHOP_ACT_DELAY + 10);
        if (target == EQUIP_TOTAL) {
            *ready = true;
            cpu_shop_budget[p] = 0; // reset for next round
            return;
        }
        if (cpu_shop_action[p] == 1) {
            shop_try_sell(app, p, target);
            cpu_shop_sold[p][target] = true;
            cpu_shop_target[p] = -1;
        } else {
            shop_try_buy(app, p, target);
            cpu_shop_target[p] = -1;
            // Short delay for repeat buys — decision phase will re-select same item if still wanted
            cpu_shop_delay[p] = cpu_random_delay(4, 8);
        }
        return;
    }

    // Decide next action
    cpu_shop_delay[p] = cpu_random_delay(CPU_SHOP_MIN_DELAY, CPU_SHOP_MAX_DELAY);

    bool want_money = app->options.win_by_money;
    uint32_t budget = cpu_shop_budget[p];

    // Phase 1: Sell excess items UPFRONT only — once done, never sell again
    // Misers sell aggressively: dump expensive items from weapon crates for cash
    if (!cpu_sell_done[p]) {
        bool is_miser = (cpu_personality[p] == 2);
        // Only dump genuine surplus from weapon crates. Default keep is at
        // least 1 of every utility so the CPU can actually use what it owns.
        static const struct { int item; int keep_normal; int keep_miser; int keep_rounds; } sell_rules[] = {
            {EQUIP_METAL_WALL,       1, 0, 1},
            {EQUIP_BIOMASS,          1, 0, 2},
            {EQUIP_CLONE,            1, 0, 2},
            {EQUIP_EXTINGUISHER,     1, 0, 2},
            {EQUIP_BARREL,           1, 0, 3},
            {EQUIP_JUMPING_BOMB,     1, 0, 3},
            {EQUIP_PLASTIC,          1, 0, 3},
            {EQUIP_EXPLOSIVE_PLASTIC,1, 0, 3},
            {EQUIP_LARGE_CRUCIFIX,   1, 0, 2},
            {EQUIP_SMALL_CRUCIFIX,   1, 0, 3},
            {EQUIP_FLAMETHROWER,     1, 0, 2},
            {EQUIP_GRENADE,          1, 0, 4},
            {EQUIP_LARGE_RADIO,      1, 0, 3},
            {EQUIP_SMALL_RADIO,      1, 0, 3},
            {EQUIP_BIG_BOMB,         3, 0, 99},
            {EQUIP_NAPALM,           1, 0, 99},
            {EQUIP_ATOMIC_BOMB,      1, 0, 2},
            {EQUIP_TELEPORT,         1, 0, 2},
            {EQUIP_SUPER_DRILL,      1, 0, 2},
            {EQUIP_ARMOR,            2, 0, 3},
        };
        bool found_sell = false;
        for (int i = 0; i < (int)(sizeof(sell_rules)/sizeof(sell_rules[0])); i++) {
            int item = sell_rules[i].item;
            int keep = want_money ? (is_miser ? sell_rules[i].keep_miser : sell_rules[i].keep_normal) : sell_rules[i].keep_rounds;
            if (app->player_inventory[p][item] > keep) {
                cpu_shop_target[p] = item;
                cpu_shop_action[p] = 1;
                found_sell = true;
                break;
            }
        }
        if (found_sell) return;
        // Nothing left to sell — lock sell phase, recalculate budget with new cash
        cpu_sell_done[p] = true;
        uint32_t new_base = app->player_cash[p];
        int pers2 = cpu_personality[p];
        int sp;
        if (want_money) {
            switch (pers2) {
                case 1: sp = 55 + rand() % 15; break;
                case 2: sp = 30 + rand() % 15; break;
                case 3: sp = 45 + rand() % 20; break;
                default: sp = 40 + rand() % 20; break;
            }
        } else {
            switch (pers2) {
                case 1: sp = 80 + rand() % 15; break;
                case 2: sp = 55 + rand() % 15; break;
                case 3: sp = 65 + rand() % 20; break;
                default: sp = 70 + rand() % 20; break;
            }
        }
        budget = cpu_shop_budget[p] = new_base * sp / 100;
    }

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
    (void)lv; // all fields used conditionally
    bool barrelly = has_preview && lv.barrels > 3;

    // Phase 2: Ensure minimum drill power — never start at 1
    int drill = cpu_drill_power(app, p);
    int drill_target = stony ? (7 + rand() % 3) : (3 + rand() % 2);
    if (drill < drill_target) {
        int pick = -1;
        if (drill_target - drill >= 5 && cash >= EQUIPMENT_PRICES[EQUIP_DRILL]) pick = EQUIP_DRILL;
        else if (drill_target - drill >= 3 && cash >= EQUIPMENT_PRICES[EQUIP_LARGE_PICKAXE]) pick = EQUIP_LARGE_PICKAXE;
        else if (cash >= EQUIPMENT_PRICES[EQUIP_SMALL_PICKAXE]) pick = EQUIP_SMALL_PICKAXE;
        if (pick >= 0) { cpu_shop_target[p] = pick; cpu_shop_action[p] = 0; return; }
    }

    // Phase 3: Armor — more in dangerous levels
    {
        int armor = app->player_inventory[p][EQUIP_ARMOR];
        int armor_target = want_money ? (rand() % 2) : (monsters ? 2 + rand() % 2 : 1 + rand() % 2);
        if (armor < armor_target && cash >= EQUIPMENT_PRICES[EQUIP_ARMOR] + 200) {
            cpu_shop_target[p] = EQUIP_ARMOR; cpu_shop_action[p] = 0; return;
        }
    }

    // Phase 4: Weapons/tools based on strategy + level features
    typedef struct { int item; int want; } BuyGoal;
    BuyGoal goals[24];
    int ngoals = 0;

    // Add buy goal with personality-scaled random variance
    // Aggressive buys more, miser buys less, explorer varies wildly
    int _var_range = (cpu_personality[p] == 1) ? 4 : (cpu_personality[p] == 2) ? 2 : 3;
    int _var_bias  = (cpu_personality[p] == 1) ? 1 : (cpu_personality[p] == 2) ? -1 : 0;
    #define G(itm, n) do { \
        if (cpu_shop_sold[p][(itm)]) break; \
        int _target = (n) + (rand() % (_var_range + 1)) - _var_range/2 + _var_bias; \
        if (_target < 1) _target = 1; \
        int _w = _target - app->player_inventory[p][(itm)]; \
        if (_w > 0 && ngoals < 24) goals[ngoals++] = (BuyGoal){(itm), _w}; \
    } while(0)

    if (want_money) {
        // Mining + combat essentials for money mode
        if (stony) { G(EQUIP_LARGE_PICKAXE, 2); G(EQUIP_DYNAMITE, 4); G(EQUIP_DIGGER, 2); }
        G(EQUIP_SMALL_BOMB, sandy ? 4 : 6);
        G(EQUIP_BIG_BOMB, 3);    // cheap ($3) and effective for clearing
        G(EQUIP_DYNAMITE, 2);
        G(EQUIP_MINE, 4);        // mines are great defense while mining
        if (rich) G(EQUIP_TELEPORT, 1);
        // Always some combat weapons — killing = stealing their cash
        G(EQUIP_GRENADE, 2);
        if (corridor) G(EQUIP_FLAMETHROWER, 1);
        if (monsters) { G(EQUIP_GRENADE, 3); G(EQUIP_MINE, 6); }
        G(EQUIP_EXTINGUISHER, 1);
        if (cash > 1200) G(EQUIP_SUPER_DRILL, 1);
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
    // Personality-driven curiosity and unique shopping behavior
    {
        int pers = cpu_personality[p];
        if (pers == 1 && ngoals < 24) {
            // Aggressive: heavy weapons, lots of bombs
            if (rand() % 2 == 0 && !cpu_shop_sold[p][EQUIP_ATOMIC_BOMB])
                goals[ngoals++] = (BuyGoal){EQUIP_ATOMIC_BOMB, 1};
            if (rand() % 2 == 0 && !cpu_shop_sold[p][EQUIP_FLAMETHROWER])
                goals[ngoals++] = (BuyGoal){EQUIP_FLAMETHROWER, 1 + rand() % 2};
            if (!cpu_shop_sold[p][EQUIP_NAPALM])
                goals[ngoals++] = (BuyGoal){EQUIP_NAPALM, 1 + rand() % 2};
            if (!cpu_shop_sold[p][EQUIP_LARGE_CRUCIFIX])
                goals[ngoals++] = (BuyGoal){EQUIP_LARGE_CRUCIFIX, 1};
        } else if (pers == 2 && ngoals < 24) {
            // Miser: banks money, sells expensive crate loot, invests in mining for big treasure
            // Buy very few weapons — just enough to survive
            if (!cpu_shop_sold[p][EQUIP_SMALL_BOMB] && app->player_inventory[p][EQUIP_SMALL_BOMB] < 3)
                goals[ngoals++] = (BuyGoal){EQUIP_SMALL_BOMB, 3};
            // Mining gear to reach high-value treasure faster
            if (!cpu_shop_sold[p][EQUIP_SMALL_PICKAXE])
                goals[ngoals++] = (BuyGoal){EQUIP_SMALL_PICKAXE, 3 + rand() % 2};
            // That's it — save the rest
        } else if (pers == 3 && ngoals < 24) {
            // Explorer: utility items
            if (rand() % 2 == 0 && !cpu_shop_sold[p][EQUIP_TELEPORT])
                goals[ngoals++] = (BuyGoal){EQUIP_TELEPORT, 1 + rand() % 2};
            if (rand() % 2 == 0 && !cpu_shop_sold[p][EQUIP_CLONE])
                goals[ngoals++] = (BuyGoal){EQUIP_CLONE, 1};
            if (rand() % 2 == 0 && !cpu_shop_sold[p][EQUIP_SUPER_DRILL])
                goals[ngoals++] = (BuyGoal){EQUIP_SUPER_DRILL, 1};
            if (!cpu_shop_sold[p][EQUIP_DIGGER])
                goals[ngoals++] = (BuyGoal){EQUIP_DIGGER, 1 + rand() % 2};
        } else if (ngoals < 24) {
            // Balanced: grab some cheap bombs and a random utility
            if (!cpu_shop_sold[p][EQUIP_BIG_BOMB])
                goals[ngoals++] = (BuyGoal){EQUIP_BIG_BOMB, 2 + rand() % 3};
            int random_util[] = {EQUIP_BARREL, EQUIP_SMALL_CRUCIFIX, EQUIP_PLASTIC, EQUIP_JUMPING_BOMB};
            int ri = random_util[rand() % 4];
            if (!cpu_shop_sold[p][ri] && app->player_inventory[p][ri] == 0)
                goals[ngoals++] = (BuyGoal){ri, 1};
        }
    }
    #undef G

    // Fill-in pass: make sure the CPU at least tries every equipment slot
    // over the course of a tournament. For each item it owns 0 of and that
    // isn't already in the goal list, add a small "buy 1" goal. Cheap items
    // get higher priority by being checked first; expensive specialty items
    // still get a chance when there's surplus cash.
    {
        static const int fill_order[] = {
            EQUIP_SMALL_BOMB, EQUIP_BIG_BOMB, EQUIP_DYNAMITE, EQUIP_MINE,
            EQUIP_GRENADE, EQUIP_BARREL, EQUIP_SMALL_CRUCIFIX, EQUIP_PLASTIC,
            EQUIP_EXPLOSIVE_PLASTIC, EQUIP_JUMPING_BOMB, EQUIP_DIGGER,
            EQUIP_SMALL_RADIO, EQUIP_LARGE_RADIO, EQUIP_LARGE_CRUCIFIX,
            EQUIP_FLAMETHROWER, EQUIP_NAPALM, EQUIP_EXTINGUISHER,
            EQUIP_METAL_WALL, EQUIP_BIOMASS, EQUIP_TELEPORT, EQUIP_CLONE,
            EQUIP_SUPER_DRILL, EQUIP_ATOMIC_BOMB,
        };
        int n = (int)(sizeof(fill_order) / sizeof(fill_order[0]));
        for (int i = 0; i < n && ngoals < 24; i++) {
            int item = fill_order[i];
            if (cpu_shop_sold[p][item]) continue;
            if (app->player_inventory[p][item] > 0) continue;
            // Skip already-listed goals
            bool already = false;
            for (int j = 0; j < ngoals; j++) if (goals[j].item == item) { already = true; break; }
            if (already) continue;
            // Personality-based skip: misers ignore rare/expensive specials.
            if (cpu_personality[p] == 2 && EQUIPMENT_PRICES[item] > 200) continue;
            goals[ngoals++] = (BuyGoal){item, 1};
        }
    }

    // Find first affordable goal within budget, navigate to it
    uint32_t spent = cpu_shop_budget[p] > app->player_cash[p] ? cpu_shop_budget[p] - app->player_cash[p] : 0;
    for (int i = 0; i < ngoals; i++) {
        if (goals[i].want <= 0) continue;
        uint32_t price = EQUIPMENT_PRICES[goals[i].item];
        if (price <= app->player_cash[p] && spent + price <= budget) {
            cpu_shop_target[p] = goals[i].item;
            cpu_shop_action[p] = 0; // buy
            return;
        }
    }

    // Nothing left to buy — navigate cursor to READY button
    cpu_shop_target[p] = EQUIP_TOTAL;
    cpu_shop_action[p] = 2;
}

// ==================== CPU Gameplay AI (clean slate) ====================
//
// Goals (so far): explore the level and pick up any treasure found.
// No threat awareness yet — monsters/bombs/enemies are ignored.
//
// Movement model: re-decide only when the actor is tile-aligned. Use a
// uniform-cost BFS over the map, with each tile classified as walkable,
// diggable (slow but free), or bombable (fast but costs a bomb). Find the
// nearest treasure; if none reachable, pick the nearest unvisited tile.
// Walk one step toward it. If that step is into a wall and we have bombs,
// drop one (only if we have an obvious escape route).

#include "fonts.h"

// ---- direction tables (shared with movement & rendering)
static const int DDX[] = {0, 0, -1, 1};
static const int DDY[] = {-1, 1, 0, 0};
static const uint8_t DIR_FLAGS[] = {NET_INPUT_UP, NET_INPUT_DOWN, NET_INPUT_LEFT, NET_INPUT_RIGHT};

// ---- tile classification (kept from prior cpu.c — used by other helpers)
static bool cpu_is_treasure(uint8_t val) {
    return (val >= 0x8F && val <= 0x9A) || val == TILE_DIAMOND;
}
static bool cpu_is_pickup(uint8_t val) {
    // Things we are happy to walk onto and grab. Medikits are handled separately
    // — only worth grabbing when hurt or contested.
    if (cpu_is_treasure(val)) return true;
    if (val == TILE_WEAPONS_CRATE) return true;
    if (val >= 0x8B && val <= 0x8E) return true; // pickaxes / drill
    if (val == 0xB3) return true;             // life
    return false;
}
static bool cpu_walkable(uint8_t val) {
    if (is_passable(val)) return true;
    if (cpu_is_pickup(val)) return true;
    if (val >= 0x32 && val <= 0x34) return true; // sand 1-3 (auto-cleared on entry)
    if (val == 0xAF) return true;
    return false;
}
static bool cpu_diggable(uint8_t val) {
    if (is_stone(val)) return true;
    if (val >= TILE_SAND1 && val <= TILE_SAND3) return true;
    if (val == TILE_GRAVEL_LIGHT || val == TILE_GRAVEL_HEAVY) return true;
    if (val == TILE_STONE_CRACKED_LIGHT || val == TILE_STONE_CRACKED_HEAVY) return true;
    if (val == TILE_BRICK || val == TILE_BRICK_CRACKED_LIGHT || val == TILE_BRICK_CRACKED_HEAVY) return true;
    return false;
}
static bool cpu_bombable(uint8_t val) {
    if (val == TILE_WALL || val == 0xB4 || val == 0xB5) return false;
    if (cpu_walkable(val)) return false;
    if (val == TILE_BOULDER) return true;  // boulders shatter under blasts
    return cpu_diggable(val);
}
static bool cpu_is_bomb_tile(uint8_t val) {
    // Just enough to avoid pathing through obvious live ordnance.
    return val == 0x57 || val == 0x58 || val == 0x59
        || val == 0x77 || val == 0x78
        || val == 0x8B || val == 0x8C || val == 0x8D || val == 0x8E
        || val == 0x9D || val == 0x9E || val == 0x9F
        || val == TILE_BARREL || val == TILE_MINE
        || val == 0xAB || val == 0xA0 || val == 0xA1 || val == 0xA2 || val == 0xA3
        || val == 0x4B || val == 0x4C || val == 0x56;
}
// Rough cross-blast radius estimate for any bomb tile.
static int cpu_bomb_radius(uint8_t val) {
    if (val == 0x57 || val == 0x77 || val == 0x78) return 2;
    if (val == 0x58 || val == 0x8B || val == 0x8C) return 3;
    if (val == 0x59 || val == 0x8D || val == 0x8E) return 4;
    if (val == 0x9D || val == 0x9E || val == 0x9F) return 7;
    if (val == 0x4B) return 4;
    if (val == 0x4C) return 6;
    if (val == 0xA0 || val == 0xA1) return 4;
    if (val == 0xA2 || val == 0xA3) return 4;
    if (val == 0xAB) return 3;
    if (val == TILE_BARREL) return 3;
    if (val == TILE_MINE) return 2;
    return 3;
}

static int cpu_drill_power(App* app, int p) {
    return 1 + app->player_inventory[p][EQUIP_SMALL_PICKAXE]
             + 3 * app->player_inventory[p][EQUIP_LARGE_PICKAXE]
             + 5 * app->player_inventory[p][EQUIP_DRILL];
}
static bool cpu_has_bombs(App* app, int p) {
    static const int bombs[] = {
        EQUIP_SMALL_BOMB, EQUIP_BIG_BOMB, EQUIP_DYNAMITE, EQUIP_NAPALM,
        EQUIP_SMALL_RADIO, EQUIP_LARGE_RADIO, EQUIP_MINE, EQUIP_BARREL,
        EQUIP_SMALL_CRUCIFIX, EQUIP_LARGE_CRUCIFIX, EQUIP_PLASTIC,
        EQUIP_EXPLOSIVE_PLASTIC, EQUIP_DIGGER, EQUIP_JUMPING_BOMB,
        EQUIP_ATOMIC_BOMB, EQUIP_GRENADE, EQUIP_FLAMETHROWER,
    };
    for (size_t i = 0; i < sizeof(bombs)/sizeof(bombs[0]); i++)
        if (app->player_inventory[p][bombs[i]] > 0) return true;
    return false;
}

// ---- per-player state (kept across frames)
typedef enum {
    GOAL_NONE = 0,
    GOAL_TREASURE,
    GOAL_EXPLORE,
} GoalKind;

typedef enum {
    COMBAT_NONE = 0,
    COMBAT_ATTACK_PLAYER,
    COMBAT_ATTACK_MONSTER,
    COMBAT_FLEE_PLAYER,
    COMBAT_FLEE_MONSTER,
    COMBAT_FLEE_BOMB,
    COMBAT_DEFUSE,
    COMBAT_DEFEND,
    COMBAT_NUKE,
    COMBAT_SUPER_DRILL,
    COMBAT_RANGED,
    COMBAT_REMOTE,
} CombatState;

#define CPU_PATH_MAX 256
typedef struct {
    uint8_t visited[MAP_HEIGHT][MAP_WIDTH]; // increments per visit
    GoalKind goal_kind;
    int goal_x, goal_y;
    // Cached path from current tile -> goal (cell coords). path[0] is the next step.
    int16_t path_x[CPU_PATH_MAX];
    int16_t path_y[CPU_PATH_MAX];
    int path_len;
    int last_tile_x, last_tile_y;
    int stuck_ticks;       // ticks spent on the same tile
    int unstick_steps;     // remaining forced random-walk steps after a stall
    // Per-player goal blacklist (used for tiles we abandoned because we got stuck)
    int blacklist_x[4], blacklist_y[4];
    int blacklist_ttl[4];  // ticks remaining
    bool needs_bomb;
    CombatState combat;
} CpuState;

static CpuState cpu_state[MAX_PLAYERS];

// ---- Cost model
// Costs are in "ticks" (one walkable tile = WALK_TICKS). Diggable tiles add
// the actual dig time given the player's current drilling power. Bombable but
// non-diggable tiles are ignored for now (no bombing yet in this pass).
#define WALK_TICKS 10

// Mirror of game.c's get_initial_hits (private there). Returns hits required
// to clear the tile, or -1 if not diggable / indestructible.
// Mirrors game.c get_initial_hits(): the # of hits required to fully clear
// the tile at full health. Lower values = faster to dig.
static int cpu_initial_hits(uint8_t val) {
    if (val == TILE_WALL) return -1;
    if (val >= TILE_SAND1 && val <= TILE_SAND3) return 24;
    if (val == TILE_GRAVEL_LIGHT) return 108;
    if (val == TILE_GRAVEL_HEAVY) return 347;
    if (val == TILE_BOULDER) return 24;
    // Stone "decoration" pieces (corners) — cheaper than full stone
    if (val == TILE_STONE_TOP_LEFT || val == TILE_STONE_TOP_RIGHT
        || val == TILE_STONE_BOTTOM_RIGHT || val == TILE_STONE_BOTTOM_LEFT) return 1227;
    if (val == TILE_STONE1) return 2000;
    if (val == TILE_STONE2) return 2150;
    if (val == TILE_STONE3) return 2200;
    if (val == TILE_STONE4) return 2100;
    if (val == TILE_STONE_CRACKED_LIGHT) return 1000;
    if (val == TILE_STONE_CRACKED_HEAVY) return 500;
    if (val == TILE_BRICK) return 8000;
    if (val == TILE_BRICK_CRACKED_LIGHT) return 4000;
    if (val == TILE_BRICK_CRACKED_HEAVY) return 2000;
    return -1;
}

// Can a boulder at (bx,by) be pushed in direction (dx,dy) — i.e. is the cell
// behind it free of walls/actors/other boulders so the player can shove it?
static bool cpu_boulder_pushable(World* w, int bx, int by, int dx, int dy) {
    int tx = bx + dx, ty = by + dy;
    if (tx < 0 || tx >= MAP_WIDTH || ty < 0 || ty >= MAP_HEIGHT) return false;
    if (!is_passable(w->tiles[ty][tx])) return false;
    for (int ai = 0; ai < w->num_actors; ai++) {
        if (w->actors[ai].is_dead) continue;
        if (w->actors[ai].pos.x / 10 == tx && (w->actors[ai].pos.y - 30) / 10 == ty) return false;
    }
    return true;
}

typedef bool (*CpuGoalFn)(World* w, int x, int y, void* ctx);

// Approximate ticks it takes to clear a wall by bombing: place + escape +
// fuse + return. Cheap compared with digging brick or hard stone.
#define BOMB_TICKS 45

// Per-cell countdown until the next bomb blast reaches it. UINT16_MAX = safe.
static uint16_t cpu_danger[MAP_HEIGHT][MAP_WIDTH];

// Personality + per-replan jitter applied to BOMB_TICKS so the CPU doesn't
// always make the same dig-vs-bomb decision. Computed at the top of cpu_replan.
static int cpu_bomb_cost_bias;
// Player whose visited heatmap should be applied as a soft path cost.
static int cpu_planning_player;

// Cost to enter cell (nx,ny) from (px,py). Returns -1 if not enterable.
static int cpu_step_cost(World* w, int px, int py, int nx, int ny, int drill, bool bombs) {
    uint8_t t = w->tiles[ny][nx];
    if (cpu_is_bomb_tile(t)) return -1;
    if (cpu_walkable(t)) return WALK_TICKS;
    if (t == TILE_BOULDER) {
        if (cpu_boulder_pushable(w, nx, ny, nx - px, ny - py)) return WALK_TICKS * 2;
        // Can't push (wall behind, blocked by actor): fall through to bomb logic.
        if (bombs) {
            int bt = BOMB_TICKS + cpu_bomb_cost_bias;
            if (cpu_planning_player >= 0 && cpu_planning_player < MAX_PLAYERS)
                bt += cpu_state[cpu_planning_player].visited[ny][nx] * 2;
            return bt;
        }
        return -1;
    }
    int dig_ticks = -1;
    if (drill > 0 && cpu_diggable(t)) {
        int hits = w->hits[ny][nx];
        if (hits <= 0) hits = cpu_initial_hits(t);
        if (hits >= 0) dig_ticks = WALK_TICKS + (hits + drill - 1) / drill;
    }
    int bomb_ticks = -1;
    if (bombs && cpu_bombable(t)) bomb_ticks = BOMB_TICKS + cpu_bomb_cost_bias;
    if (dig_ticks < 0 && bomb_ticks < 0) return -1;
    int base;
    if (dig_ticks < 0) base = bomb_ticks;
    else if (bomb_ticks < 0) base = dig_ticks;
    else base = dig_ticks < bomb_ticks ? dig_ticks : bomb_ticks;
    // Heatmap penalty: tiles we keep visiting cost more, pushing the planner
    // toward unexplored ground when it's pursuing a distant goal.
    if (cpu_planning_player >= 0 && cpu_planning_player < MAX_PLAYERS) {
        int v = cpu_state[cpu_planning_player].visited[ny][nx];
        base += v * 2;
    }
    return base;
}

static bool cpu_bfs(World* w, int sx, int sy, CpuGoalFn goal, void* ctx,
                    int drill, bool bombs,
                    int* gx_out, int* gy_out,
                    int16_t* px, int16_t* py, int* path_len_out)
{
    static int16_t parent[MAP_HEIGHT][MAP_WIDTH];
    static uint32_t dist[MAP_HEIGHT][MAP_WIDTH];
    static uint8_t in_open[MAP_HEIGHT][MAP_WIDTH];
    for (int y = 0; y < MAP_HEIGHT; y++)
        for (int x = 0; x < MAP_WIDTH; x++) {
            parent[y][x] = -1;
            dist[y][x] = 0xFFFFFFFFu;
            in_open[y][x] = 0;
        }

    // Dijkstra on a 64x45 grid. Linear scan per pop is fine.
    dist[sy][sx] = 0;
    int remaining = 1;
    in_open[sy][sx] = 1;

    int found_x = -1, found_y = -1;

    while (remaining > 0) {
        int bx = -1, by = -1;
        uint32_t bd = 0xFFFFFFFFu;
        for (int y = 0; y < MAP_HEIGHT; y++)
            for (int x = 0; x < MAP_WIDTH; x++)
                if (in_open[y][x] && dist[y][x] < bd) { bd = dist[y][x]; bx = x; by = y; }
        if (bx < 0) break;
        in_open[by][bx] = 0;
        remaining--;

        if (!(bx == sx && by == sy) && goal(w, bx, by, ctx)) {
            found_x = bx; found_y = by;
            break;
        }

        for (int d = 0; d < 4; d++) {
            int nx = bx + DDX[d], ny = by + DDY[d];
            if (nx < 0 || nx >= MAP_WIDTH || ny < 0 || ny >= MAP_HEIGHT) continue;
            int c = cpu_step_cost(w, bx, by, nx, ny, drill, bombs);
            if (c < 0) continue;
            uint32_t nd = dist[by][bx] + (uint32_t)c;
            // Refuse to walk into a cell that will be in a bomb's blast before
            // we even get there. Add a small safety margin.
            if (cpu_danger[ny][nx] != 0xFFFF
                && (uint32_t)cpu_danger[ny][nx] < nd + 20) continue;
            if (nd < dist[ny][nx]) {
                dist[ny][nx] = nd;
                parent[ny][nx] = (int16_t)(by * MAP_WIDTH + bx);
                if (!in_open[ny][nx]) { in_open[ny][nx] = 1; remaining++; }
            }
        }
    }

    if (found_x < 0) return false;

    // Reconstruct
    int16_t tmpx[CPU_PATH_MAX];
    int16_t tmpy[CPU_PATH_MAX];
    int n = 0;
    int cx = found_x, cy = found_y;
    while (!(cx == sx && cy == sy) && n < CPU_PATH_MAX) {
        tmpx[n] = (int16_t)cx;
        tmpy[n] = (int16_t)cy;
        int16_t pp = parent[cy][cx];
        if (pp < 0) break;
        cx = pp % MAP_WIDTH;
        cy = pp / MAP_WIDTH;
        n++;
    }
    // Reverse so path[0] is first step away from start.
    for (int i = 0; i < n; i++) {
        px[i] = tmpx[n - 1 - i];
        py[i] = tmpy[n - 1 - i];
    }
    *path_len_out = n;
    *gx_out = found_x;
    *gy_out = found_y;
    return true;
}

// ---- goal predicates
typedef struct { int player; bool want_medikit; } TreasureCtx;
static bool cpu_blacklisted(int player, int x, int y) {
    CpuState* st = &cpu_state[player];
    for (int i = 0; i < 4; i++)
        if (st->blacklist_ttl[i] > 0 && st->blacklist_x[i] == x && st->blacklist_y[i] == y) return true;
    return false;
}
static bool goal_treasure(World* w, int x, int y, void* c) {
    TreasureCtx* ctx = c;
    uint8_t t = w->tiles[y][x];
    if (ctx && cpu_blacklisted(ctx->player, x, y)) return false;
    if (cpu_is_pickup(t)) return true;
    if (t == TILE_MEDIKIT && ctx && ctx->want_medikit) return true;
    return false;
}
typedef struct { int gx, gy; } FixedCtx;
static bool goal_fixed(World* w, int x, int y, void* c) {
    (void)w;
    FixedCtx* fc = c;
    return x == fc->gx && y == fc->gy;
}
static bool goal_unvisited(World* w, int x, int y, void* c) {
    int p = *(int*)c;
    (void)w;
    if (cpu_blacklisted(p, x, y)) return false;
    return cpu_state[p].visited[y][x] == 0;
}

// ---- bomb safety: very simple. We need to be able to step OFF the bomb's
// cross axis within ~2 tiles. Returns true if we can flee.
// Can we survive dropping a bomb on (cx,cy)? We're safe if EITHER:
//   (a) we can walk 3+ straight tiles in some direction (out of cross radius
//       before fuse expires — small bombs have radius 2 and fuse ~100 ticks),
//   or (b) we can step into an adjacent walkable cell that's off the bomb's
//       cross axis (perpendicular escape).
static bool cpu_can_escape_bomb(World* w, int cx, int cy) {
    for (int d = 0; d < 4; d++) {
        int run = 0;
        for (int s = 1; s <= 5; s++) {
            int nx = cx + DDX[d] * s, ny = cy + DDY[d] * s;
            if (nx < 0 || nx >= MAP_WIDTH || ny < 0 || ny >= MAP_HEIGHT) break;
            if (!cpu_walkable(w->tiles[ny][nx])) break;
            run++;
            // Perpendicular escape from this cell?
            for (int d2 = 0; d2 < 4; d2++) {
                if (d2 == d) continue;
                if (DDX[d2] == -DDX[d] && DDY[d2] == -DDY[d]) continue;
                int px = nx + DDX[d2], py = ny + DDY[d2];
                if (px < 0 || px >= MAP_WIDTH || py < 0 || py >= MAP_HEIGHT) continue;
                if (cpu_walkable(w->tiles[py][px])) return true;
            }
            if (run >= 3) return true;
        }
    }
    return false;
}

// ---- decision: pick best path for this tick
// Walk the cross of every active bomb, blocked by walls, and store the bomb's
// remaining fuse in cpu_danger[]. Lower numbers = more dangerous.
static void cpu_build_danger(World* w) {
    for (int y = 0; y < MAP_HEIGHT; y++)
        for (int x = 0; x < MAP_WIDTH; x++) cpu_danger[y][x] = 0xFFFF;
    for (int by = 0; by < MAP_HEIGHT; by++)
        for (int bx = 0; bx < MAP_WIDTH; bx++) {
            uint8_t bt = w->tiles[by][bx];
            if (!cpu_is_bomb_tile(bt)) continue;
            int fuse = w->timer[by][bx];
            if (fuse <= 0) fuse = 1; // about to pop
            int r = cpu_bomb_radius(bt);
            // The bomb cell itself
            if (cpu_danger[by][bx] > fuse) cpu_danger[by][bx] = (uint16_t)fuse;
            // Cross arms blocked by walls
            for (int axis = 0; axis < 2; axis++) {
                for (int sign = -1; sign <= 1; sign += 2) {
                    for (int d = 1; d <= r; d++) {
                        int nx = bx + (axis == 0 ? sign * d : 0);
                        int ny = by + (axis == 1 ? sign * d : 0);
                        if (nx < 0 || nx >= MAP_WIDTH || ny < 0 || ny >= MAP_HEIGHT) break;
                        uint8_t t = w->tiles[ny][nx];
                        if (t == TILE_WALL) break;
                        if (cpu_danger[ny][nx] > fuse) cpu_danger[ny][nx] = (uint16_t)fuse;
                        if (cpu_diggable(t) || t == TILE_BOULDER) break; // blast stops at solid mass
                    }
                }
            }
        }
}

static void cpu_replan(App* app, World* world, int p, int cx, int cy) {
    CpuState* st = &cpu_state[p];
    // Personality-driven bomb cost bias plus a small random jitter so the
    // CPU varies its dig-vs-bomb decisions tick to tick.
    int pers = cpu_personality_init[p] ? cpu_personality[p] : 0;
    int bias = 0;
    switch (pers) {
        case 1: bias = -10; break;  // Aggro: prefers bombing
        case 2: bias = +25; break;  // Miser: prefers digging (saves bombs)
        case 3: bias = +15; break;  // Explorer: a bit cheaper than miser
        default: bias = 0; break;   // Balanced
    }
    cpu_bomb_cost_bias = bias + (rand() % 25) - 8;
    cpu_planning_player = p;
    // Use the live drilling stat (it tracks pickaxes picked up mid-level too).
    int drill = world->actors[p].drilling;
    if (drill <= 0) drill = cpu_drill_power(app, p);
    bool bombs = cpu_has_bombs(app, p);

    int gx, gy, n;
    // Decide whether medikits are interesting this tick: hurt OR another live
    // player is close to one (so we'd better grab it before they do).
    Actor* me = &world->actors[p];
    bool hurt = me->health < me->max_health;
    bool contested_medikit = false;
    if (!hurt) {
        for (int my = 0; my < MAP_HEIGHT && !contested_medikit; my++)
            for (int mx = 0; mx < MAP_WIDTH && !contested_medikit; mx++) {
                if (world->tiles[my][mx] != TILE_MEDIKIT) continue;
                for (int op = 0; op < world->num_players; op++) {
                    if (op == p || world->actors[op].is_dead) continue;
                    int ox = world->actors[op].pos.x / 10;
                    int oy = (world->actors[op].pos.y - 30) / 10;
                    int od = abs(ox - mx) + abs(oy - my);
                    int md = abs(cx - mx) + abs(cy - my);
                    if (od <= 4 && md <= od + 2) { contested_medikit = true; break; }
                }
            }
    }
    TreasureCtx tctx = {p, hurt || contested_medikit};

    // Sticky goals: if the previous goal tile still satisfies the original
    // predicate AND a path still exists, keep aiming for it. Prevents the CPU
    // from oscillating between two equidistant treasures every tick.
    if (st->goal_kind != GOAL_NONE && st->goal_x >= 0 && st->goal_y >= 0
        && !(cx == st->goal_x && cy == st->goal_y)) {
        bool still_valid = false;
        uint8_t gt = world->tiles[st->goal_y][st->goal_x];
        if (st->goal_kind == GOAL_TREASURE && (cpu_is_pickup(gt) || (gt == TILE_MEDIKIT && tctx.want_medikit)))
            still_valid = true;
        if (st->goal_kind == GOAL_EXPLORE && st->visited[st->goal_y][st->goal_x] == 0)
            still_valid = true;
        if (still_valid) {
            FixedCtx fc = {st->goal_x, st->goal_y};
            if (cpu_bfs(world, cx, cy, goal_fixed, &fc, drill, bombs, &gx, &gy, st->path_x, st->path_y, &n)) {
                st->path_len = n;
                return;
            }
        }
        // Old goal stale or unreachable — drop it and pick a new one below.
        st->goal_kind = GOAL_NONE;
    }

    if (cpu_bfs(world, cx, cy, goal_treasure, &tctx, drill, bombs, &gx, &gy, st->path_x, st->path_y, &n)) {
        st->goal_kind = GOAL_TREASURE;
        st->goal_x = gx; st->goal_y = gy;
        st->path_len = n;
        return;
    }
    int pp = p;
    if (cpu_bfs(world, cx, cy, goal_unvisited, &pp, drill, bombs, &gx, &gy, st->path_x, st->path_y, &n)) {
        st->goal_kind = GOAL_EXPLORE;
        st->goal_x = gx; st->goal_y = gy;
        st->path_len = n;
        return;
    }
    st->goal_kind = GOAL_NONE;
    st->path_len = 0;
}

// ---- main entry
uint8_t cpu_generate_input(App* app, World* world, int p) {
    Actor* actor = &world->actors[p];
    if (actor->is_dead) return 0;

    int cx = actor->pos.x / 10;
    int cy = (actor->pos.y - 30) / 10;
    if (cx < 0 || cx >= MAP_WIDTH || cy < 0 || cy >= MAP_HEIGHT) return 0;

    // Only re-decide on tile-aligned ticks; otherwise let movement carry us.
    if (actor->pos.x % 10 != 5 || (actor->pos.y - 30) % 10 != 5) return 0;

    CpuState* st = &cpu_state[p];

    cpu_build_danger(world);
    st->combat = COMBAT_NONE;

    // Mark visited (heat increases each tick we sit on a cell)
    if (st->visited[cy][cx] < 250) st->visited[cy][cx]++;
    // Periodic decay so old heat fades and we'll revisit areas eventually.
    if ((world->round_counter & 0x3F) == 0) {
        for (int yy = 0; yy < MAP_HEIGHT; yy++)
            for (int xx = 0; xx < MAP_WIDTH; xx++)
                if (st->visited[yy][xx] > 0) st->visited[yy][xx]--;
    }

    // Are there any hostile actors within a small radius? If so we shouldn't
    // mistake "managing combat / fleeing a bomb" for being stuck.
    bool combat_proximity = false;
    {
        for (int i = 0; i < world->num_actors && !combat_proximity; i++) {
            if (i == p) continue;
            Actor* e = &world->actors[i];
            if (e->is_dead) continue;
            if (i >= world->num_players) {
                if (!e->is_active) continue;
                if (e->kind == ACTOR_CLONE && e->clone_owner == p) continue;
            }
            int ex = e->pos.x / 10, ey = (e->pos.y - 30) / 10;
            if (abs(ex - cx) + abs(ey - cy) <= 4) combat_proximity = true;
        }
    }

    // Stall detection: if we have not actually moved for several tile ticks
    // (stuck pushing an immovable boulder, oscillating between two equally
    // attractive goals, etc.) blacklist the current goal and break out with a
    // burst of random walking.
    if (cx == st->last_tile_x && cy == st->last_tile_y) {
        // Don't accumulate stall ticks while we're managing bombs or enemies.
        if (!combat_proximity && cpu_danger[cy][cx] == 0xFFFF) st->stuck_ticks++;
    } else {
        st->stuck_ticks = 0;
        st->last_tile_x = cx; st->last_tile_y = cy;
    }
    if (st->stuck_ticks > 30 && st->unstick_steps == 0) {
        // Add the current goal to the blacklist so we won't pick it again soon.
        if (st->goal_kind != GOAL_NONE) {
            for (int i = 0; i < 4; i++) {
                if (st->blacklist_ttl[i] == 0) {
                    st->blacklist_x[i] = st->goal_x;
                    st->blacklist_y[i] = st->goal_y;
                    st->blacklist_ttl[i] = 400;
                    break;
                }
            }
        }
        st->goal_kind = GOAL_NONE;
        st->path_len = 0;
        st->stuck_ticks = 0;
        st->unstick_steps = 6;
    }
    for (int i = 0; i < 4; i++) if (st->blacklist_ttl[i] > 0) st->blacklist_ttl[i]--;
    if (st->unstick_steps > 0) {
        st->unstick_steps--;
        int start = rand() % 4;
        for (int k = 0; k < 4; k++) {
            int d = (start + k) % 4;
            int nx2 = cx + DDX[d], ny2 = cy + DDY[d];
            if (nx2 < 0 || nx2 >= MAP_WIDTH || ny2 < 0 || ny2 >= MAP_HEIGHT) continue;
            if (!cpu_walkable(world->tiles[ny2][nx2])) continue;
            return DIR_FLAGS[d];
        }
    }

    // If we've reached the cached goal or moved off-path, replan.
    bool need_plan = (st->path_len == 0)
        || (st->goal_kind != GOAL_NONE && cx == st->goal_x && cy == st->goal_y)
        || (st->path_len > 0 && (st->path_x[0] != cx + DDX[0] && st->path_x[0] != cx + DDX[1]
                                  && st->path_x[0] != cx + DDX[2] && st->path_x[0] != cx + DDX[3]
                                  && !(st->path_x[0] == cx && st->path_y[0] == cy)));
    // Always replan: cheap enough on a 64x45 grid and avoids stale paths after wall changes.
    need_plan = true;
    if (need_plan) cpu_replan(app, world, p, cx, cy);

    // Standing inside ANY bomb's blast cross? Bolt out of the cross. We never
    // want to camp on top of (or in line with) our own bomb. Strategy:
    //   1. Prefer a fully-safe neighbour (cpu_danger == 0xFFFF).
    //   2. Otherwise prefer the neighbour with the highest "fleeability" score
    //      = (look two tiles ahead in that direction; reward leaving the cross).
    //   3. As a last resort just pick any walkable neighbour so we keep moving.
    if (cpu_danger[cy][cx] < 0xFFFF) {
        int best_dir = -1;
        int best_score = -1;
        for (int d = 0; d < 4; d++) {
            int ax = cx + DDX[d], ay = cy + DDY[d];
            if (ax < 0 || ax >= MAP_WIDTH || ay < 0 || ay >= MAP_HEIGHT) continue;
            uint8_t at = world->tiles[ay][ax];
            if (!cpu_walkable(at)) continue;
            int score = 0;
            if (cpu_danger[ay][ax] == 0xFFFF) score = 1000;
            else score = (int)cpu_danger[ay][ax];
            // Look two ahead — if that cell is safe, even better. Reward
            // perpendicular escape from the cross.
            int bx2 = ax + DDX[d], by2 = ay + DDY[d];
            if (bx2 >= 0 && bx2 < MAP_WIDTH && by2 >= 0 && by2 < MAP_HEIGHT
                && cpu_walkable(world->tiles[by2][bx2])
                && cpu_danger[by2][bx2] == 0xFFFF) score += 500;
            // Side step (perpendicular to bomb cross)
            for (int d2 = 0; d2 < 4; d2++) {
                if (d2 == d) continue;
                int sx = ax + DDX[d2], sy = ay + DDY[d2];
                if (sx < 0 || sx >= MAP_WIDTH || sy < 0 || sy >= MAP_HEIGHT) continue;
                if (cpu_walkable(world->tiles[sy][sx]) && cpu_danger[sy][sx] == 0xFFFF) {
                    score += 200; break;
                }
            }
            score += rand() % 30; // jitter so flee feels less mechanical
            if (score > best_score) { best_score = score; best_dir = d; }
        }
        if (best_dir >= 0) {
            st->goal_kind = GOAL_NONE;
            st->path_len = 0;
            st->needs_bomb = false;
            st->combat = COMBAT_FLEE_BOMB; (void)st->combat;
            return DIR_FLAGS[best_dir];
        }
    }

    // ---- Specials: extinguisher / super drill / atomic / radio trigger ----
    {
        // EXTINGUISHER: defuse an adjacent ticking bomb if we have one and the
        // bomb still has time to defuse (timer > 10).
        if (app->player_inventory[p][EQUIP_EXTINGUISHER] > 0) {
            for (int d = 0; d < 4; d++) {
                int nx2 = cx + DDX[d], ny2 = cy + DDY[d];
                if (nx2 < 0 || nx2 >= MAP_WIDTH || ny2 < 0 || ny2 >= MAP_HEIGHT) continue;
                if (!cpu_is_bomb_tile(world->tiles[ny2][nx2])) continue;
                if (world->timer[ny2][nx2] < 10) continue;
                if (actor->selected_weapon != EQUIP_EXTINGUISHER) return NET_INPUT_CYCLE;
                st->combat = COMBAT_DEFUSE;
                return NET_INPUT_ACTION;
            }
        }
        // RADIO TRIGGER: if we have a placed radio and an enemy / monster is in
        // its blast cross, detonate it (handled by NET_INPUT_REMOTE).
        {
            uint8_t sr[4] = {0x63, 0x82, 0x67, 0x69};
            uint8_t br[4] = {0x64, 0x83, 0x68, 0x6A};
            bool trigger = false;
            for (int ry = 0; ry < MAP_HEIGHT && !trigger; ry++)
                for (int rx = 0; rx < MAP_WIDTH && !trigger; rx++) {
                    uint8_t tt = world->tiles[ry][rx];
                    bool mine_radio = (tt == sr[p] || tt == br[p]);
                    if (!mine_radio) continue;
                    int blast = (tt == br[p]) ? 3 : 2;
                    // Don't blow ourselves up
                    if ((cx == rx && abs(cy - ry) <= blast) || (cy == ry && abs(cx - rx) <= blast)) continue;
                    for (int i = 0; i < world->num_actors; i++) {
                        if (i == p || world->actors[i].is_dead) continue;
                        if (i >= world->num_players) {
                            if (!world->actors[i].is_active) continue;
                            if (world->actors[i].kind == ACTOR_CLONE && world->actors[i].clone_owner == p) continue;
                        }
                        int ex = world->actors[i].pos.x / 10, ey = (world->actors[i].pos.y - 30) / 10;
                        if ((ex == rx && abs(ey - ry) <= blast) || (ey == ry && abs(ex - rx) <= blast)) {
                            trigger = true; break;
                        }
                    }
                }
            if (trigger) { st->combat = COMBAT_REMOTE; return NET_INPUT_REMOTE; }
        }
    }

    // ---- Combat: ranged / bomb adjacent enemies / flee if outgunned ----
    {
        int near_x = -1, near_y = -1, near_d = 999;
        bool near_is_player = false;
        int crowd_count = 0;
        for (int i = 0; i < world->num_actors; i++) {
            if (i == p) continue;
            Actor* e = &world->actors[i];
            if (e->is_dead) continue;
            if (i >= world->num_players) {
                if (!e->is_active) continue;
                if (e->kind == ACTOR_CLONE && e->clone_owner == p) continue;
            }
            int ex = e->pos.x / 10, ey = (e->pos.y - 30) / 10;
            int d = abs(ex - cx) + abs(ey - cy);
            if (d <= 5) crowd_count++;
            if (d < near_d) { near_d = d; near_x = ex; near_y = ey; near_is_player = (i < world->num_players); }
        }

        // NUKE the crowd if surrounded and we can run far enough.
        if (crowd_count >= 2 && app->player_inventory[p][EQUIP_ATOMIC_BOMB] > 0
            && cpu_can_escape_bomb(world, cx, cy)
            && cpu_danger[cy][cx] == 0xFFFF) {
            if (actor->selected_weapon != EQUIP_ATOMIC_BOMB) return NET_INPUT_CYCLE;
            st->combat = COMBAT_NUKE;
            return NET_INPUT_ACTION;
        }

        if (near_d <= 5 && near_x >= 0) {
            // RANGED: grenade or flamethrower if we have line of sight.
            bool los = (near_x == cx) || (near_y == cy);
            if (los) {
                int hit = 0;
                int sx = (near_x > cx) - (near_x < cx);
                int sy = (near_y > cy) - (near_y < cy);
                for (int s = 1; s < near_d; s++) {
                    int wx = cx + sx * s, wy = cy + sy * s;
                    if (!cpu_walkable(world->tiles[wy][wx])) { hit = 1; break; }
                }
                if (!hit) {
                    Direction face_want;
                    if (sx > 0) face_want = DIR_RIGHT;
                    else if (sx < 0) face_want = DIR_LEFT;
                    else if (sy > 0) face_want = DIR_DOWN;
                    else face_want = DIR_UP;
                    if (app->player_inventory[p][EQUIP_FLAMETHROWER] > 0 && near_d <= 4) {
                        if (actor->facing != face_want) {
                            int df = face_want == DIR_UP ? NET_INPUT_UP
                                  : face_want == DIR_DOWN ? NET_INPUT_DOWN
                                  : face_want == DIR_LEFT ? NET_INPUT_LEFT : NET_INPUT_RIGHT;
                            return (uint8_t)df;
                        }
                        if (actor->selected_weapon != EQUIP_FLAMETHROWER) return NET_INPUT_CYCLE;
                        st->combat = COMBAT_RANGED;
                        return NET_INPUT_ACTION;
                    }
                    if (app->player_inventory[p][EQUIP_GRENADE] > 0 && near_d >= 2 && near_d <= 5) {
                        if (actor->facing != face_want) {
                            int df = face_want == DIR_UP ? NET_INPUT_UP
                                  : face_want == DIR_DOWN ? NET_INPUT_DOWN
                                  : face_want == DIR_LEFT ? NET_INPUT_LEFT : NET_INPUT_RIGHT;
                            return (uint8_t)df;
                        }
                        if (actor->selected_weapon != EQUIP_GRENADE) return NET_INPUT_CYCLE;
                        st->combat = COMBAT_RANGED;
                        return NET_INPUT_ACTION;
                    }
                }
            }
        }

        if (near_d <= 3 && near_x >= 0) {
            bool has_bombs_now = cpu_has_bombs(app, p);
            bool already_bomb_here = cpu_danger[cy][cx] != 0xFFFF;
            if (has_bombs_now && !already_bomb_here && cpu_can_escape_bomb(world, cx, cy)) {
                int sel = actor->selected_weapon;
                bool sel_is_bomb = sel != EQUIP_SMALL_PICKAXE && sel != EQUIP_LARGE_PICKAXE
                    && sel != EQUIP_DRILL && sel != EQUIP_ARMOR
                    && app->player_inventory[p][sel] > 0;
                if (!sel_is_bomb) return NET_INPUT_CYCLE;
                if ((rand() % 5) == 0) return 0;
                st->combat = near_is_player ? COMBAT_ATTACK_PLAYER : COMBAT_ATTACK_MONSTER;
                return NET_INPUT_ACTION;
            }
            // No bombs / no escape: flee directly away from the enemy.
            int best_d = -1, best_score = -1;
            for (int d = 0; d < 4; d++) {
                int nx2 = cx + DDX[d], ny2 = cy + DDY[d];
                if (nx2 < 0 || nx2 >= MAP_WIDTH || ny2 < 0 || ny2 >= MAP_HEIGHT) continue;
                if (!cpu_walkable(world->tiles[ny2][nx2])) continue;
                if (cpu_danger[ny2][nx2] != 0xFFFF) continue;
                int s = abs(nx2 - near_x) + abs(ny2 - near_y);
                if (s > best_score) { best_score = s; best_d = d; }
            }
            if (best_d >= 0) {
                st->combat = near_is_player ? COMBAT_FLEE_PLAYER : COMBAT_FLEE_MONSTER;
                return DIR_FLAGS[best_d];
            }
        }
    }

    // ---- Activate SUPER_DRILL when about to grind a hard wall ----
    if (actor->super_drill_count == 0 && app->player_inventory[p][EQUIP_SUPER_DRILL] > 0
        && st->path_len > 0) {
        int wx = st->path_x[0], wy = st->path_y[0];
        if (wx >= 0 && wx < MAP_WIDTH && wy >= 0 && wy < MAP_HEIGHT) {
            uint8_t wt = world->tiles[wy][wx];
            int hits = world->hits[wy][wx];
            if (hits <= 0) hits = cpu_initial_hits(wt);
            int live_drill = world->actors[p].drilling > 0 ? world->actors[p].drilling : 1;
            if (cpu_diggable(wt) && hits > 1500 && (hits / live_drill) > 200) {
                if (actor->selected_weapon != EQUIP_SUPER_DRILL) return NET_INPUT_CYCLE;
                st->combat = COMBAT_SUPER_DRILL;
                return NET_INPUT_ACTION;
            }
        }
    }

    if (st->path_len == 0) return NET_INPUT_STOP;

    // Determine direction of first step.
    int nx = st->path_x[0], ny = st->path_y[0];
    int dir = -1;
    for (int d = 0; d < 4; d++) if (cx + DDX[d] == nx && cy + DDY[d] == ny) { dir = d; break; }
    if (dir < 0) { st->path_len = 0; return NET_INPUT_STOP; }

    uint8_t t = world->tiles[ny][nx];
    st->needs_bomb = false;

    if (cpu_walkable(t)) {
        return DIR_FLAGS[dir];
    }
    if (t == TILE_BOULDER) {
        if (cpu_boulder_pushable(world, nx, ny, nx - cx, ny - cy)) {
            // Walking into the boulder pushes it (engine handles the push).
            return DIR_FLAGS[dir];
        }
        // Boulder is wedged — fall through into the bomb branch below by
        // pretending it's a bombable wall.
    }
    // Compute the cheaper of dig vs bomb for this wall, matching BFS cost.
    int live_drill = world->actors[p].drilling;
    if (live_drill <= 0) live_drill = cpu_drill_power(app, p);
    bool has_bombs = cpu_has_bombs(app, p);
    int dig_ticks = -1;
    if (live_drill > 0 && cpu_diggable(t)) {
        int hits = world->hits[ny][nx];
        if (hits <= 0) hits = cpu_initial_hits(t);
        if (hits >= 0) dig_ticks = WALK_TICKS + (hits + live_drill - 1) / live_drill;
    }
    int bomb_ticks = (has_bombs && cpu_bombable(t)) ? BOMB_TICKS : -1;
    bool prefer_bomb = (bomb_ticks >= 0) && (dig_ticks < 0 || bomb_ticks < dig_ticks);

    if (prefer_bomb) {
        // No facing required: bomb's cross blast hits the wall regardless of
        // which way we're looking. (Earlier `DIR_FLAGS[dir] | NET_INPUT_STOP`
        // hack was a no-op because the OR collapsed direction bits into STOP.)
        if (!cpu_can_escape_bomb(world, cx, cy)) {
            // Try digging instead if possible
            if (dig_ticks >= 0) return DIR_FLAGS[dir];
            for (int d = 0; d < 4; d++) {
                int wx = cx + DDX[d], wy = cy + DDY[d];
                if (wx >= 0 && wx < MAP_WIDTH && wy >= 0 && wy < MAP_HEIGHT && cpu_walkable(world->tiles[wy][wx]))
                    return DIR_FLAGS[d];
            }
            return NET_INPUT_STOP;
        }
        // Make sure we're holding a usable bomb. If currently on a non-bomb,
        // cycle weapons until we land on something throwable.
        int sel = actor->selected_weapon;
        bool sel_is_bomb = sel != EQUIP_SMALL_PICKAXE && sel != EQUIP_LARGE_PICKAXE
            && sel != EQUIP_DRILL && sel != EQUIP_ARMOR
            && app->player_inventory[p][sel] > 0;
        if (!sel_is_bomb) return NET_INPUT_CYCLE;
        st->needs_bomb = true;
        if ((rand() % 6) == 0) return 0;  // jittered placement
        return NET_INPUT_ACTION;
    }
    if (dig_ticks >= 0) return DIR_FLAGS[dir];
    return NET_INPUT_STOP;
}

// ==================== Debug overlay ====================

static bool cpu_debug_on = false;
void cpu_debug_toggle(void) { cpu_debug_on = !cpu_debug_on; }
bool cpu_debug_enabled(void) { return cpu_debug_on; }
void cpu_debug_set(bool on) { cpu_debug_on = on; }

static const char* cpu_combat_verb(CombatState c) {
    switch (c) {
        case COMBAT_ATTACK_PLAYER:  return "ATK player";
        case COMBAT_ATTACK_MONSTER: return "ATK monster";
        case COMBAT_FLEE_PLAYER:    return "DEF flee player";
        case COMBAT_FLEE_MONSTER:   return "DEF flee monster";
        case COMBAT_FLEE_BOMB:      return "DEF flee bomb";
        case COMBAT_DEFUSE:         return "DEF defuse";
        case COMBAT_DEFEND:         return "DEF block";
        case COMBAT_NUKE:           return "ATK nuke";
        case COMBAT_SUPER_DRILL:    return "ACT super drill";
        case COMBAT_RANGED:         return "ATK ranged";
        case COMBAT_REMOTE:         return "ATK remote bomb";
        default: return NULL;
    }
}

static const char* cpu_goal_verb(GoalKind k, World* w, int gx, int gy, bool needs_bomb) {
    if (needs_bomb) return "bombing wall";
    if (k == GOAL_TREASURE) {
        if (gx >= 0 && gy >= 0) {
            uint8_t t = w->tiles[gy][gx];
            if (t == TILE_WEAPONS_CRATE) return "walking to crate";
            if (t == TILE_MEDIKIT) return "walking to medikit";
            if (t == TILE_DIAMOND) return "walking to diamond";
        }
        return "walking to treasure";
    }
    if (k == GOAL_EXPLORE) return "exploring";
    return "idle";
}

void cpu_debug_render(SDL_Renderer* renderer, App* app, World* world) {
    if (!cpu_debug_on) return;
    // Player slot colors: blue, red, green, yellow (matching the in-game HUD).
    SDL_Color colors[4] = {
        { 80, 160, 255, 255},  // p0 blue
        {255,  80,  80, 255},  // p1 red
        { 80, 255,  80, 255},  // p2 green
        {255, 220,  80, 255},  // p3 yellow
    };
    SDL_BlendMode prev;
    SDL_GetRenderDrawBlendMode(renderer, &prev);
    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);

    for (int p = 0; p < world->num_players; p++) {
        if (!is_cpu_player(app, p)) continue;
        Actor* a = &world->actors[p];
        if (a->is_dead) continue;
        CpuState* st = &cpu_state[p];

        SDL_Color c = colors[p % 4];
        SDL_SetRenderDrawColor(renderer, c.r, c.g, c.b, 220);

        int cx = a->pos.x;
        int cy = a->pos.y;
        int prev_x = cx;
        int prev_y = cy;
        for (int i = 0; i < st->path_len; i++) {
            int sx = st->path_x[i] * 10 + 5;
            int sy = st->path_y[i] * 10 + 30 + 5;
            SDL_RenderDrawLine(renderer, prev_x, prev_y, sx, sy);
            prev_x = sx; prev_y = sy;
        }

        // Goal marker
        if (st->goal_kind != GOAL_NONE) {
            SDL_Rect r = {st->goal_x * 10 + 2, st->goal_y * 10 + 30 + 2, 6, 6};
            SDL_RenderDrawRect(renderer, &r);
        }

        // Verb label above the actor (combat state takes precedence)
        const char* combat = cpu_combat_verb(st->combat);
        const char* verb = combat ? combat
            : cpu_goal_verb(st->goal_kind, world, st->goal_x, st->goal_y, st->needs_bomb);
        int tx = cx - 20;
        int ty = cy - 12;
        if (tx < 0) tx = 0;
        if (ty < 30) ty = 30;
        render_text(renderer, &app->font, tx, ty, c, verb);
    }

    SDL_SetRenderDrawBlendMode(renderer, prev);
}
