#include "cpu.h"
#include "shop.h"
#include "net.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Timing
static int cpu_shop_delay[MAX_PLAYERS];
static int cpu_think_delay[MAX_PLAYERS];

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

#define CPU_SHOP_MIN_DELAY   6  // ~100ms cursor moves
#define CPU_SHOP_MAX_DELAY  14  // ~230ms
#define CPU_SHOP_ACT_DELAY  18  // ~300ms pause before buy/sell action
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

// Current drill power from inventory
static int cpu_drill_power(App* app, int p) {
    return 1 + app->player_inventory[p][EQUIP_SMALL_PICKAXE]
             + 3 * app->player_inventory[p][EQUIP_LARGE_PICKAXE]
             + 5 * app->player_inventory[p][EQUIP_DRILL];
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
        static const struct { int item; int keep_normal; int keep_miser; int keep_rounds; } sell_rules[] = {
            {EQUIP_METAL_WALL,       0, 0, 0},
            {EQUIP_BIOMASS,          0, 0, 1},
            {EQUIP_CLONE,            0, 0, 1},
            {EQUIP_EXTINGUISHER,     0, 0, 1},
            {EQUIP_BARREL,           0, 0, 1},
            {EQUIP_JUMPING_BOMB,     0, 0, 2},
            {EQUIP_PLASTIC,          0, 0, 1},
            {EQUIP_EXPLOSIVE_PLASTIC,0, 0, 1},
            {EQUIP_LARGE_CRUCIFIX,   0, 0, 1},
            {EQUIP_SMALL_CRUCIFIX,   0, 0, 2},
            {EQUIP_FLAMETHROWER,     0, 0, 1},
            {EQUIP_GRENADE,          0, 0, 2},
            {EQUIP_LARGE_RADIO,      0, 0, 2},
            {EQUIP_SMALL_RADIO,      0, 0, 2},
            {EQUIP_BIG_BOMB,         2, 0, 99},
            {EQUIP_NAPALM,           0, 0, 99},
            {EQUIP_ATOMIC_BOMB,      0, 0, 1},
            // Misers sell these expensive items from weapon crates
            {EQUIP_TELEPORT,         1, 0, 1},
            {EQUIP_SUPER_DRILL,      1, 0, 1},
            {EQUIP_ARMOR,            1, 0, 2},
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

// ==================== CPU Gameplay AI ====================

static const int DDX[] = {0, 0, -1, 1};
static const int DDY[] = {-1, 1, 0, 0};
static const uint8_t DIR_FLAGS[] = {NET_INPUT_UP, NET_INPUT_DOWN, NET_INPUT_LEFT, NET_INPUT_RIGHT};

static bool cpu_is_bomb(uint8_t val) {
    // Must match game.c is_bomb() — DO NOT use ranges that include sand/stone tiles
    return val == 0x57 || val == 0x77 || val == 0x78  // small bomb 1-3
        || val == 0x58 || val == 0x8B || val == 0x8C  // big bomb 1-3
        || val == 0x59 || val == 0x8D || val == 0x8E  // dynamite 1-3
        || val == 0x9D || val == 0x9E || val == 0x9F  // atomic 1-3
        || val == TILE_BARREL || val == TILE_MINE
        || val == 0x63 || val == 0x82 || val == 0x67 || val == 0x69 // small radios
        || val == 0x64 || val == 0x83 || val == 0x68 || val == 0x6A // big radios
        || val == 0x4B || val == 0x4C  // crucifix bombs
        || val == 0xA0 || val == 0xA1  // plastic bombs
        || val == 0xA2 || val == 0xA3  // digger, napalm
        || val == 0xAB                 // jumping bomb
        || val == 0x56;                // napalm2
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
    if (val >= 0x32 && val <= 0x34) return true; // sand (TILE_SAND1-TILE_SAND3)
    if (val >= 0x8B && val <= 0x8E) return true; // pickaxes/drill
    if (val == 0x79 || val == 0xB3) return true;  // medikit, life item
    if (val == 0x73) return true; // weapons crate
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

// Get blast radius of a bomb tile.
static int cpu_bomb_radius(uint8_t val) {
    // Small bombs
    if (val == 0x57 || val == 0x77 || val == 0x78) return 2;
    // Big bombs
    if (val == 0x58 || val == 0x8B || val == 0x8C) return 3;
    // Dynamite
    if (val == 0x59 || val == 0x8D || val == 0x8E) return 4;
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
// Key insight: it takes ~10 ticks to move 1 tile. A bomb at timer T with blast
// radius R is dangerous if T < (dist_to_escape) * 10. We model this as:
// "escape ticks needed" = (radius - dist_from_bomb + 1) * 10
// If timer < escape_ticks, we're in mortal danger.
static int cpu_danger(World* world, int cx, int cy) {
    if (cx < 0 || cx >= MAP_WIDTH || cy < 0 || cy >= MAP_HEIGHT) return 999;
    int sev = 0;
    for (int axis = 0; axis < 2; axis++) {
        for (int sign = -1; sign <= 1; sign += 2) {
            for (int dist = 0; dist <= 7; dist++) {
                int nx = cx + (axis == 0 ? sign * dist : 0);
                int ny = cy + (axis == 1 ? sign * dist : 0);
                if (nx < 0 || nx >= MAP_WIDTH || ny < 0 || ny >= MAP_HEIGHT) break;
                uint8_t tile = world->tiles[ny][nx];
                if (cpu_is_bomb(tile) && world->timer[ny][nx] > 0) {
                    int radius = cpu_bomb_radius(tile);
                    if (dist <= radius) {
                        int timer = world->timer[ny][nx];
                        int tiles_to_escape = radius - dist + 1;
                        int ticks_needed = tiles_to_escape * 12; // ~12 ticks per tile
                        if (timer < ticks_needed + 20) {
                            // We might not escape in time — severity scales with urgency
                            int urgency = ticks_needed + 20 - timer;
                            if (urgency < 1) urgency = 1;
                            sev += urgency * (radius - dist + 2);
                        }
                    }
                }
                if (tile == TILE_BARREL && dist > 0) {
                    for (int bd = 1; bd <= 4; bd++) {
                        int bx = nx + (axis == 0 ? -sign * bd : 0);
                        int by = ny + (axis == 1 ? -sign * bd : 0);
                        if (bx >= 0 && bx < MAP_WIDTH && by >= 0 && by < MAP_HEIGHT
                            && cpu_is_bomb(world->tiles[by][bx]) && world->timer[by][bx] > 0 && world->timer[by][bx] < 80)
                            sev += 40;
                    }
                }
                if (tile == TILE_WALL && dist > 0) break;
            }
        }
    }
    for (int dy = -5; dy <= 5; dy++)
        for (int dx = -5; dx <= 5; dx++) {
            int nx = cx + dx, ny = cy + dy;
            if (nx < 0 || nx >= MAP_WIDTH || ny < 0 || ny >= MAP_HEIGHT) continue;
            uint8_t tile = world->tiles[ny][nx];
            if ((tile == 0xA0 || tile == 0xA1 || tile == 0xA3 || tile == 0x57)
                && world->timer[ny][nx] > 0 && world->timer[ny][nx] < 80) {
                int dist = abs(dx) + abs(dy);
                if (dist <= 5) sev += (80 - world->timer[ny][nx]) * 2;
            }
        }
    return sev;
}

// Per-player state
static uint8_t cpu_visited[MAX_PLAYERS][MAP_HEIGHT][MAP_WIDTH];
static int cpu_last_dir[MAX_PLAYERS];
static bool cpu_flee_mode[MAX_PLAYERS];
static int cpu_last_tile_x[MAX_PLAYERS], cpu_last_tile_y[MAX_PLAYERS];
static int cpu_stuck_counter[MAX_PLAYERS]; // how many ticks we've been in the same small area

// Decay visited map (call periodically)
static void cpu_decay_visited(int p) {
    for (int y = 0; y < MAP_HEIGHT; y++)
        for (int x = 0; x < MAP_WIDTH; x++)
            if (cpu_visited[p][y][x] > 0)
                cpu_visited[p][y][x] -= 1; // slow steady decay
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

            // Check if tile is pushable (boulder, barrel, extinguished bombs, etc.)
            bool is_push = (tile == TILE_BOULDER || tile == TILE_BARREL
                || tile == 0x57 || tile == 0x58 || tile == 0x59 // extinguished bombs
                || tile == 0xAA); // extinguished dynamite
            if (cpu_is_treasure(tile) || tile == 0x9C || tile == 0x73) {
                cost = 0; // FREE — treasure, teleporter, crate always worth walking to
            } else if (cpu_walkable(tile)) {
                cost = 1;
            } else if (tile == TILE_MINE) {
                cost = 12;
            } else if (is_push) {
                // Pushable: check if it can be pushed in this direction
                int bx = nx + DDX[d], by = ny + DDY[d];
                bool can_push = false;
                if (bx >= 0 && bx < MAP_WIDTH && by >= 0 && by < MAP_HEIGHT
                    && is_passable(world->tiles[by][bx])) {
                    bool blocked = false;
                    for (int ai = 0; ai < world->num_actors; ai++) {
                        if (world->actors[ai].is_dead) continue;
                        if (world->actors[ai].pos.x / 10 == bx && (world->actors[ai].pos.y - 30) / 10 == by)
                            { blocked = true; break; }
                    }
                    if (!blocked) { can_push = true; cost = 5; }
                }
                if (!can_push) {
                    // Can't push — try bombing it instead
                    if (bombs && tile != TILE_BARREL) { cost = 5; bomb_step = true; }
                    else continue;
                }
            } else if (drill > 0 && (tile >= 0x32 && tile <= 0x34)) {
                // Sand (TILE_SAND1-TILE_SAND3): trivial to dig, always walk through
                cost = 2;
            } else if (bombs && cpu_bombable(tile)) {
                // Bombing: instant clear, preferred for anything harder than sand
                cost = 4;
                bomb_step = true;
            } else if (drill > 0 && cpu_diggable(tile)) {
                // Digging: cost scales with difficulty vs drill power.
                // When out of bombs, dig cost is reduced — slow progress beats looping.
                int hits = 200;
                if (tile == 0xAC) hits = 8000;
                else if (tile == 0xAD) hits = 4000;
                else if (tile == 0xAE) hits = 2000;
                else if (is_stone(tile)) hits = 2100;
                else if (tile == 0x70) hits = 1000;
                else if (tile == 0x71) hits = 500;
                else if (tile == 0x40) hits = 108;
                else if (tile >= 0x41 && tile <= 0x46) hits = 347;
                int dig_ticks = hits / (drill > 0 ? drill : 1);
                if (bombs) {
                    // Have bombs: digging is expensive (prefer bombing)
                    cost = 2 + dig_ticks / 10;
                    if (cost > 80) cost = 80;
                } else {
                    // No bombs: digging is the only option, make it much cheaper
                    // so A* prefers digging over looping through visited tiles
                    cost = 3 + dig_ticks / 40;
                    if (cost > 20) cost = 20;
                }
            } else {
                continue;
            }

            if (cpu_is_bomb(tile)) continue;
            int dng = cpu_danger(world, nx, ny);
            if (dng > 100) cost += 200;        // near-certain death — almost impassable
            else if (dng > 30) cost += 50 + dng;  // serious danger
            else if (dng > 0) cost += 15 + dng;

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

            // Visited penalty: moderate to discourage oscillation but allow retracing
            // corridors to reach distant targets. Only very recent tiles (high values)
            // get heavy penalty — older visited tiles are just slightly more expensive.
            if (player >= 0 && player < MAX_PLAYERS) {
                int v = cpu_visited[player][ny][nx];
                if (v > 200) cost += 15;       // visited many times: moderate discouragement
                else if (v > 100) cost += 10;  // visited twice
                else if (v > 0) cost += 5;     // visited once: slight preference for new tiles
            }

            // Penalize reversing the last direction taken (first step only)
            if (first && player >= 0 && player < MAX_PLAYERS) {
                int reverse = -1;
                switch (cpu_last_dir[player]) {
                    case 0: reverse = 1; break; // up→down
                    case 1: reverse = 0; break; // down→up
                    case 2: reverse = 3; break; // left→right
                    case 3: reverse = 2; break; // right→left
                }
                if (d == reverse) cost += 200;
            }

            int ng = cur.g + cost;
            uint8_t fd = first ? DIR_FLAGS[d] : cur.dir;
            bool fb = first ? bomb_step : cur.first_bomb;

            if (goal(world, nx, ny, ctx)) return (AStarResult){fd, fb};
            if (cnt < ASTAR_MAX) { vis[ny][nx] = 1; open[cnt++] = (AStarNode){nx, ny, ng, ng, head-1, fd, fb}; }
        }
    }
    return none;
}

static bool goal_treasure(World* w, int x, int y, void* c) {
    (void)c;
    uint8_t t = w->tiles[y][x];
    return cpu_is_treasure(t) || t == 0x73 || t == 0x9C; // treasure, weapon crate, teleporter
}
static bool goal_medikit(World* w, int x, int y, void* c) { (void)c; return w->tiles[y][x] == 0x79; }
typedef struct { int ex, ey; } EnemyGoalCtx;
static bool goal_near_enemy(World* w, int x, int y, void* c) {
    (void)w; EnemyGoalCtx* e = c; return abs(x - e->ex) + abs(y - e->ey) <= 2;
}
// Goal: any tile the CPU hasn't visited much (exploration)
typedef struct { int player; } ExploreCtx;
static bool goal_explore(World* w, int x, int y, void* c) {
    ExploreCtx* ec = c;
    (void)w;
    return cpu_visited[ec->player][y][x] <= 10;
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
// Check if a cell is in the danger zone after placing a new bomb at (bx,by)
// considering chain reactions with existing bombs.
static bool cpu_cell_in_combined_danger(World* world, int testx, int testy, int bx, int by, int blast) {
    // In our new bomb's cross blast? Check walls block propagation.
    if (testx == bx && abs(testy - by) <= blast) {
        int step = (testy > by) ? 1 : -1;
        bool blocked = false;
        for (int y = by + step; y != testy; y += step) {
            if (world->tiles[y][bx] == TILE_WALL) { blocked = true; break; }
        }
        if (!blocked) return true;
    }
    if (testy == by && abs(testx - bx) <= blast) {
        int step = (testx > bx) ? 1 : -1;
        bool blocked = false;
        for (int x = bx + step; x != testx; x += step) {
            if (world->tiles[by][x] == TILE_WALL) { blocked = true; break; }
        }
        if (!blocked) return true;
    }
    // Check existing bombs that our new bomb would chain-trigger
    // Our bomb at (bx,by) with radius=blast hits tiles in its cross.
    // Any bomb in that cross will also explode.
    for (int axis = 0; axis < 2; axis++) {
        for (int sign = -1; sign <= 1; sign += 2) {
            for (int dist = 1; dist <= blast; dist++) {
                int nx = bx + (axis == 0 ? sign * dist : 0);
                int ny = by + (axis == 1 ? sign * dist : 0);
                if (nx < 0 || nx >= MAP_WIDTH || ny < 0 || ny >= MAP_HEIGHT) break;
                uint8_t tile = world->tiles[ny][nx];
                if (tile == TILE_WALL) break;
                if (cpu_is_bomb(tile) || tile == TILE_BARREL) {
                    // This bomb/barrel will chain-react. Check if testx,testy is in ITS blast.
                    int chain_r = cpu_bomb_radius(tile);
                    if ((testx == nx && abs(testy - ny) <= chain_r) || (testy == ny && abs(testx - nx) <= chain_r))
                        return true;
                }
            }
        }
    }
    return false;
}

static bool cpu_can_bomb_safely(World* world, int cx, int cy, int blast_radius) {
    uint8_t here = world->tiles[cy][cx];
    if (here != TILE_PASSAGE && !cpu_is_treasure(here) && here != 0xAF) return false;

    // Also refuse if there's already a ticking bomb nearby — chain risk too high
    for (int dy = -2; dy <= 2; dy++)
        for (int dx = -2; dx <= 2; dx++) {
            int nx = cx + dx, ny = cy + dy;
            if (nx >= 0 && nx < MAP_WIDTH && ny >= 0 && ny < MAP_HEIGHT)
                if (cpu_is_bomb(world->tiles[ny][nx]) && world->timer[ny][nx] > 0 && world->timer[ny][nx] < 50)
                    return false; // existing bomb about to go off — don't add another
        }

    // For each escape direction, simulate fleeing 2-3 steps and check
    // if any position along the escape is in the combined danger zone.
    for (int d = 0; d < 4; d++) {
        bool escape_safe = true;
        bool escape_possible = true;
        // Walk up to blast_radius+1 steps in this direction
        for (int step = 1; step <= blast_radius + 2; step++) {
            int ex = cx + DDX[d] * step, ey = cy + DDY[d] * step;
            if (ex < 0 || ex >= MAP_WIDTH || ey < 0 || ey >= MAP_HEIGHT) { escape_possible = false; break; }
            if (!cpu_walkable(world->tiles[ey][ex])) { escape_possible = false; break; }
            // Check danger from existing bombs at this escape cell
            if (cpu_danger(world, ex, ey) > 20) { escape_safe = false; break; }
        }
        if (!escape_possible) {
            // Try stepping perpendicular after 1 step
            int nx = cx + DDX[d], ny = cy + DDY[d];
            if (nx >= 0 && nx < MAP_WIDTH && ny >= 0 && ny < MAP_HEIGHT && cpu_walkable(world->tiles[ny][nx])) {
                for (int d2 = 0; d2 < 4; d2++) {
                    int px = nx + DDX[d2], py = ny + DDY[d2];
                    if (px < 0 || px >= MAP_WIDTH || py < 0 || py >= MAP_HEIGHT) continue;
                    if (!cpu_walkable(world->tiles[py][px])) continue;
                    // Must be off the bomb's cross AND not in existing danger
                    if (!cpu_cell_in_combined_danger(world, px, py, cx, cy, blast_radius)
                        && cpu_danger(world, px, py) <= 10)
                        return true;
                }
            }
            continue;
        }
        if (escape_safe) {
            // Verify the escape endpoint is actually outside our bomb's combined danger
            int ex = cx + DDX[d] * (blast_radius + 1), ey = cy + DDY[d] * (blast_radius + 1);
            if (ex >= 0 && ex < MAP_WIDTH && ey >= 0 && ey < MAP_HEIGHT
                && !cpu_cell_in_combined_danger(world, ex, ey, cx, cy, blast_radius))
                return true;
        }
    }
    return false;
}

// Pick best bomb for clearing obstacle at (tx,ty) from current position (cx,cy).
// Considers: blast radius must reach the target, prefers smaller bombs near valuables,
// prefers larger bombs for thick walls (multiple tiles to clear).
static int cpu_pick_bomb_from(App* app, World* world, int p, int cx, int cy, int tx, int ty) {
    int dist = abs(tx - cx) + abs(ty - cy); // Manhattan distance to target
    bool same_axis = (tx == cx || ty == cy); // bomb blasts in cross, only hits same axis

    // Check if treasure/crate is near the target — use smallest bomb possible
    bool valuable_near = false;
    for (int dy = -2; dy <= 2; dy++)
        for (int dx = -2; dx <= 2; dx++) {
            int nx = tx + dx, ny = ty + dy;
            if (nx >= 0 && nx < MAP_WIDTH && ny >= 0 && ny < MAP_HEIGHT && cpu_is_valuable(world->tiles[ny][nx]))
                valuable_near = true;
        }

    // Count how many wall tiles are in line from bomb position toward target
    // (thicker walls benefit from bigger blast radius)
    int wall_depth = 0;
    if (same_axis) {
        int sdx = (tx > cx) ? 1 : (tx < cx) ? -1 : 0;
        int sdy = (ty > cy) ? 1 : (ty < cy) ? -1 : 0;
        for (int s = 1; s <= 7; s++) {
            int wx = cx + sdx * s, wy = cy + sdy * s;
            if (wx < 0 || wx >= MAP_WIDTH || wy < 0 || wy >= MAP_HEIGHT) break;
            if (cpu_walkable(world->tiles[wy][wx]) || is_passable(world->tiles[wy][wx])) break;
            wall_depth++;
        }
    }

    // Candidate weapons sorted by preference
    // For thick walls: prefer bigger blast. For near valuables: prefer smaller.
    // Weapon must have blast radius >= dist to reach the target.
    typedef struct { int weapon; int radius; } BombOption;
    BombOption options[] = {
        // Digger intentionally excluded — only used for large stone fields (checked separately)
        {EQUIP_DYNAMITE, 4},      // big radius, good for thick walls
        {EQUIP_BIG_BOMB, 3},      // cheap, medium radius
        {EQUIP_SMALL_BOMB, 2},    // cheapest, small radius
        {EQUIP_NAPALM, 5},        // huge area
        {EQUIP_SMALL_CRUCIFIX, 4},
        {EQUIP_LARGE_CRUCIFIX, 6},
        {EQUIP_ATOMIC_BOMB, 7},   // nuke — overkill but works
    };
    int nopts = sizeof(options) / sizeof(options[0]);

    if (valuable_near) {
        // Near valuables: pick smallest bomb that reaches
        for (int i = nopts - 1; i >= 0; i--) {
            if (app->player_inventory[p][options[i].weapon] <= 0) continue;
            if (!same_axis || options[i].radius < dist) continue;
            return options[i].weapon;
        }
    }

    // For stone: only use digger on large connected stone fields (4+ tiles deep)
    // Digger carves a long tunnel — wasteful on a single stone tile
    if (is_stone(world->tiles[ty][tx]) && app->player_inventory[p][EQUIP_DIGGER] > 0
        && same_axis && 3 >= dist && wall_depth >= 4)
        return EQUIP_DIGGER;

    // For thick walls: prefer bigger radius to clear more in one blast
    if (wall_depth >= 3) {
        for (int i = 0; i < nopts; i++) {
            if (app->player_inventory[p][options[i].weapon] <= 0) continue;
            if (!same_axis || options[i].radius < dist) continue;
            if (options[i].radius >= wall_depth) return options[i].weapon;
        }
    }

    // Default: pick any weapon that reaches
    for (int i = 0; i < nopts; i++) {
        if (app->player_inventory[p][options[i].weapon] <= 0) continue;
        if (same_axis && options[i].radius >= dist) return options[i].weapon;
        if (!same_axis && options[i].radius >= 1) return options[i].weapon; // any bomb works off-axis
    }
    return -1;
}

// Should we actually bomb this tile? Checks it's destructible and worth bombing.
static bool cpu_worth_bombing(World* world, int tx, int ty) {
    if (tx < 0 || tx >= MAP_WIDTH || ty < 0 || ty >= MAP_HEIGHT) return false;
    uint8_t tile = world->tiles[ty][tx];
    if (tile == TILE_WALL || tile == 0xB4 || tile == 0xB5) return false; // indestructible
    if (cpu_walkable(tile) || is_passable(tile)) return false; // already passable
    return cpu_bombable(tile) || tile == TILE_BOULDER;
}

// Find any usable offensive weapon. Returns weapon index or -1.
static int cpu_find_weapon(App* app, int p) {
    // Priority order: mine, small bomb, big bomb, dynamite, then anything
    static const int prefer[] = {EQUIP_MINE, EQUIP_SMALL_BOMB, EQUIP_BIG_BOMB, EQUIP_DYNAMITE,
        EQUIP_NAPALM, EQUIP_SMALL_CRUCIFIX, EQUIP_LARGE_CRUCIFIX, EQUIP_PLASTIC,
        EQUIP_EXPLOSIVE_PLASTIC, EQUIP_DIGGER, EQUIP_JUMPING_BOMB, EQUIP_ATOMIC_BOMB,
        EQUIP_SMALL_RADIO, EQUIP_LARGE_RADIO, EQUIP_BARREL, EQUIP_GRENADE, EQUIP_FLAMETHROWER};
    for (int i = 0; i < (int)(sizeof(prefer)/sizeof(prefer[0])); i++)
        if (app->player_inventory[p][prefer[i]] > 0) return prefer[i];
    return -1;
}

// Switch to a weapon, handling the cycling properly. Returns the action to take.
// If already on the weapon: returns 0 (ready to fire).
// If need to cycle: returns NET_INPUT_CYCLE.
// If weapon not available: returns 0 and sets *ok = false.
static uint8_t cpu_switch_to(App* app, Actor* actor, int p, int weapon, bool* ok) {
    *ok = true;
    if (app->player_inventory[p][weapon] <= 0) { *ok = false; return 0; }
    if (actor->selected_weapon == weapon) return 0; // ready
    return NET_INPUT_CYCLE;
}

// Try to use any available weapon. Returns action (ACTION, CYCLE, or 0 if nothing).
static uint8_t cpu_use_any_weapon(App* app, Actor* actor, int p) {
    // If current weapon has ammo, just use it
    if (actor->selected_weapon >= 0 && actor->selected_weapon < EQUIP_TOTAL
        && actor->selected_weapon != EQUIP_SMALL_PICKAXE && actor->selected_weapon != EQUIP_LARGE_PICKAXE
        && actor->selected_weapon != EQUIP_DRILL && actor->selected_weapon != EQUIP_ARMOR
        && app->player_inventory[p][actor->selected_weapon] > 0)
        return NET_INPUT_ACTION;
    // Otherwise cycle to find something
    int w = cpu_find_weapon(app, p);
    if (w < 0) return 0;
    bool ok;
    uint8_t sw = cpu_switch_to(app, actor, p, w, &ok);
    return ok ? (sw ? sw : NET_INPUT_ACTION) : 0;
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

    // Mark visited only when arriving at a new tile
    if (cx != cpu_last_tile_x[p] || cy != cpu_last_tile_y[p]) {
        cpu_last_tile_x[p] = cx;
        cpu_last_tile_y[p] = cy;
        if (cpu_visited[p][cy][cx] < 250) {
            int nv = cpu_visited[p][cy][cx] + 80;
            cpu_visited[p][cy][cx] = nv > 250 ? 250 : nv;
        }
        cpu_stuck_counter[p] = 0; // moved to new tile, reset stuck
    } else {
        cpu_stuck_counter[p]++;
    }
    if (world->round_counter % 100 == 0) cpu_decay_visited(p);

    // If truly stuck looping (>60 ticks on same tile or very high visited all around),
    // pick a random walkable/diggable direction to break out
    if (cpu_stuck_counter[p] > 60) {
        cpu_stuck_counter[p] = 0;
        int start = rand() % 4;
        for (int i = 0; i < 4; i++) {
            int d = (start + i) % 4;
            int nx = cx + DDX[d], ny = cy + DDY[d];
            if (nx >= 0 && nx < MAP_WIDTH && ny >= 0 && ny < MAP_HEIGHT
                && (cpu_walkable(world->tiles[ny][nx]) || cpu_diggable(world->tiles[ny][nx]))) {
                cpu_last_dir[p] = d;
                return DIR_FLAGS[d];
            }
        }
    }

    // Search budget: full map when visible, limited in darkness
    int search_limit = world->darkness ? 600 : ASTAR_MAX;

    int danger = cpu_danger(world, cx, cy);
    bool in_danger = danger > 0;

    // Flee mode: after placing a bomb, immediately flee regardless of think delay
    // Also stay in flee mode if still in serious danger
    if (cpu_flee_mode[p] || danger > 30) {
        cpu_flee_mode[p] = false;
        int best = -1, best_score = -9999;
        for (int d = 0; d < 4; d++) {
            int nx = cx + DDX[d], ny = cy + DDY[d];
            if (nx < 0 || nx >= MAP_WIDTH || ny < 0 || ny >= MAP_HEIGHT) continue;
            if (!cpu_walkable(world->tiles[ny][nx])) continue;
            int ndanger = cpu_danger(world, nx, ny);
            int score = -ndanger * 10;
            // Check 2 tiles ahead: prefer paths that lead to even safer cells
            for (int d2 = 0; d2 < 4; d2++) {
                int nx2 = nx + DDX[d2], ny2 = ny + DDY[d2];
                if (nx2 >= 0 && nx2 < MAP_WIDTH && ny2 >= 0 && ny2 < MAP_HEIGHT
                    && cpu_walkable(world->tiles[ny2][nx2]))
                    score = score > (-cpu_danger(world, nx2, ny2) * 5) ? score : (-cpu_danger(world, nx2, ny2) * 5);
            }
            // Prefer getting off the cross axis of bombs
            for (int m = world->num_players; m < world->num_actors; m++) {
                if (world->actors[m].is_dead || !world->actors[m].is_active) continue;
                if (world->actors[m].kind == ACTOR_CLONE && world->actors[m].clone_owner == p) continue;
                int mx = world->actors[m].pos.x / 10, my = (world->actors[m].pos.y - 30) / 10;
                score += abs(nx - mx) + abs(ny - my);
            }
            if (score > best_score) { best_score = score; best = d; }
        }
        if (best >= 0) {
            // Keep flee mode active if destination is still seriously dangerous
            int nx = cx + DDX[best], ny = cy + DDY[best];
            if (cpu_danger(world, nx, ny) > 20) cpu_flee_mode[p] = true;
            cpu_last_dir[p] = best;
            return DIR_FLAGS[best];
        }
    }

    // Any bomb danger at all bypasses think delay — always react immediately
    if (in_danger) {
        // handled above in flee mode
    }

    if (!in_danger) {
        if (cpu_think_delay[p] > 0) {
            // Check: are we pushing into an immovable object? If so, force new decision
            int fx = cx + DDX[actor->facing], fy = cy + DDY[actor->facing];
            bool blocked_ahead = false;
            if (fx >= 0 && fx < MAP_WIDTH && fy >= 0 && fy < MAP_HEIGHT) {
                uint8_t ft = world->tiles[fy][fx];
                if (!cpu_walkable(ft) && !cpu_diggable(ft) && ft != TILE_PASSAGE)
                    blocked_ahead = true;
                // Boulder/pushable against wall = immovable
                if (ft == TILE_BOULDER || (ft >= 0x57 && ft <= 0x59)) {
                    int bx = fx + DDX[actor->facing], by = fy + DDY[actor->facing];
                    if (bx < 0 || bx >= MAP_WIDTH || by < 0 || by >= MAP_HEIGHT
                        || !is_passable(world->tiles[by][bx]))
                        blocked_ahead = true;
                }
            }
            if (!blocked_ahead) {
                cpu_think_delay[p]--;
                return 0; // keep current movement
            }
            // Blocked: cancel delay and make a new decision now
            cpu_think_delay[p] = 0;
        }
        if (cpu_think_delay[p] <= 0)
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

    // ===== 1a. GRAB ADJACENT VALUABLES — highest non-danger priority =====
    // Pick up treasure, crates, medikits next to us. Prefer highest value.
    {
        int best_grab = -1, best_value = 0;
        for (int d = 0; d < 4; d++) {
            int nx = cx + DDX[d], ny = cy + DDY[d];
            if (nx < 0 || nx >= MAP_WIDTH || ny < 0 || ny >= MAP_HEIGHT) continue;
            uint8_t t = world->tiles[ny][nx];
            int val = 0;
            // Treasure values (matching game.c get_treasure_value)
            if (t == 0x8F) val = 15;      // shield
            else if (t == 0x90) val = 25;  // egg
            else if (t == 0x91) val = 15;  // pile
            else if (t == 0x92) val = 10;  // bracelet
            else if (t == 0x93) val = 30;  // bar
            else if (t == 0x94) val = 35;  // cross
            else if (t == 0x95) val = 50;  // scepter
            else if (t == 0x96) val = 65;  // rubin
            else if (t == 0x9A) val = 100; // crown
            else if (t == 0x6D) val = 1000; // diamond
            else if (t == 0x73) val = 50;  // weapon crate — always valuable
            else if (t == 0x79) val = 30;  // medikit
            else if (t >= 0x8B && t <= 0x8E) val = 20; // pickaxes/drill items
            if (val > best_value) { best_value = val; best_grab = d; }
        }
        if (best_grab >= 0) {
            cpu_last_dir[p] = best_grab;
            return DIR_FLAGS[best_grab];
        }
        // Also check 2 tiles away — chase nearby high-value items through passable tiles
        for (int d = 0; d < 4; d++) {
            int nx = cx + DDX[d], ny = cy + DDY[d];
            if (nx < 0 || nx >= MAP_WIDTH || ny < 0 || ny >= MAP_HEIGHT) continue;
            if (!cpu_walkable(world->tiles[ny][nx])) continue;
            int nx2 = nx + DDX[d], ny2 = ny + DDY[d];
            if (nx2 < 0 || nx2 >= MAP_WIDTH || ny2 < 0 || ny2 >= MAP_HEIGHT) continue;
            uint8_t t = world->tiles[ny2][nx2];
            // Only chase high-value items (crown=100, diamond=1000, scepter=50+)
            if (t == 0x9A || t == 0x6D || t == 0x95 || t == 0x96 || t == 0x73) {
                cpu_last_dir[p] = d;
                return DIR_FLAGS[d];
            }
        }
    }

    // ===== 1b. MONSTER THREAT: survive monster encounters =====
    // Monsters are actors[num_players..num_actors-1]. Skip own clones.
    // Enemy clones are treated as hostile — attack them.
    {
        int nearest_mdist = 999, nearest_mx = 0, nearest_my = 0;
        (void)0;
        for (int m = world->num_players; m < world->num_actors; m++) {
            if (world->actors[m].is_dead || !world->actors[m].is_active) continue;
            if (world->actors[m].kind == ACTOR_CLONE && world->actors[m].clone_owner == p) continue;
            int mx = world->actors[m].pos.x / 10;
            int my = (world->actors[m].pos.y - 30) / 10;
            int md = abs(mx - cx) + abs(my - cy);
            if (md < nearest_mdist) {
                nearest_mdist = md; nearest_mx = mx; nearest_my = my;
            }
        }

        if (nearest_mdist <= 3) {
            // Monster/enemy clone is very close — fight or flight
            // Enemy clones: always fight aggressively (they're dangerous and worth killing)
            // Regular monsters: fight if possible, flee if not

            // Ranged weapons in line of sight
            bool mlos = cpu_line_of_sight(world, cx, cy, nearest_mx, nearest_my);
            if (mlos && nearest_mdist >= 2) {
                uint8_t face = cpu_face_toward(actor, cx, cy, nearest_mx, nearest_my);
                if (face) return face;
                // Try any available weapon
                uint8_t act = cpu_use_any_weapon(app, actor, p);
                if (act == NET_INPUT_ACTION) return cpu_place_bomb(p);
                if (act) return act;
            }

            // Adjacent: drop weapon and flee
            if (nearest_mdist <= 1) {
                uint8_t act = cpu_use_any_weapon(app, actor, p);
                if (act == NET_INPUT_ACTION) { cpu_flee_mode[p] = true; return NET_INPUT_ACTION; }
                if (act) return act;
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

    // ===== 1c. UNSTICK: if near another CPU player, escape the corridor =====
    {
        int stuck_cpu = -1;
        int stuck_ox = 0, stuck_oy = 0;
        for (int i = 0; i < world->num_players; i++) {
            if (i == p || world->actors[i].is_dead || !is_cpu_player(app, i)) continue;
            int ox = world->actors[i].pos.x / 10, oy = (world->actors[i].pos.y - 30) / 10;
            if (abs(ox - cx) + abs(oy - cy) <= 2) {
                stuck_cpu = i; stuck_ox = ox; stuck_oy = oy; break;
            }
        }
        if (stuck_cpu >= 0) {
            // Use player index as tiebreaker: lower index goes "away", higher tries perpendicular
            bool i_yield = (p > stuck_cpu);

            // First: try walkable direction away from the other CPU
            int best = -1, best_score = -999;
            for (int d = 0; d < 4; d++) {
                int nx = cx + DDX[d], ny = cy + DDY[d];
                if (nx < 0 || nx >= MAP_WIDTH || ny < 0 || ny >= MAP_HEIGHT) continue;
                bool occupied = false;
                for (int j = 0; j < world->num_players; j++) {
                    if (j == p || world->actors[j].is_dead) continue;
                    if (world->actors[j].pos.x / 10 == nx && (world->actors[j].pos.y - 30) / 10 == ny)
                        occupied = true;
                }
                if (occupied) continue;
                int dist = abs(nx - stuck_ox) + abs(ny - stuck_oy);
                bool perpendicular = (DDX[d] != 0 && stuck_oy == cy) || (DDY[d] != 0 && stuck_ox == cx);
                int score = dist * 10;
                // Yielding CPU prefers perpendicular directions (dig sideways to make bypass)
                if (i_yield && perpendicular) score += 50;
                if (cpu_walkable(world->tiles[ny][nx])) score += 20;
                else if (cpu_diggable(world->tiles[ny][nx])) score += 10;
                else if (cpu_bombable(world->tiles[ny][nx]) && has_bombs && safe_to_bomb) score += 5;
                else continue;
                if (score > best_score) { best_score = score; best = d; }
            }
            if (best >= 0) {
                int nx = cx + DDX[best], ny = cy + DDY[best];
                // If the best direction requires bombing, do it
                if (!cpu_walkable(world->tiles[ny][nx]) && !cpu_diggable(world->tiles[ny][nx])
                    && cpu_bombable(world->tiles[ny][nx]) && has_bombs && safe_to_bomb) {
                    int bw = cpu_worth_bombing(world, nx, ny) ? cpu_pick_bomb_from(app, world, p, cx, cy, nx, ny) : -1;
                    if (bw >= 0) { uint8_t sw = cpu_select(app, actor, p, bw); return sw ? sw : cpu_place_bomb(p); }
                }
                cpu_last_dir[p] = best;
                return DIR_FLAGS[best];
            }
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
        AStarResult r = cpu_astar(world, cx, cy, goal_medikit, NULL, drill, has_bombs, search_limit, p);
        if (r.dir) {
            if (r.needs_bomb) {
                int tx = cx, ty = cy;
                for (int d = 0; d < 4; d++) if (DIR_FLAGS[d] == r.dir) { tx=cx+DDX[d]; ty=cy+DDY[d]; }
                int bw = cpu_worth_bombing(world, tx, ty) ? cpu_pick_bomb_from(app, world, p, cx, cy, tx, ty) : -1;
                if (bw >= 0 && safe_to_bomb) { uint8_t sw = cpu_select(app, actor, p, bw); return sw ? sw : cpu_place_bomb(p); }
            } else {
                return r.dir;
            }
        }
    }

    // ===== 2. COMBAT: hunt enemies (players, clones, and monsters) =====
    {
        int best_e = -1, best_score = -999, bex = 0, bey = 0, best_d = 999;
        // Target players
        for (int i = 0; i < world->num_players; i++) {
            if (i == p || world->actors[i].is_dead) continue;
            int ex = world->actors[i].pos.x / 10, ey = (world->actors[i].pos.y - 30) / 10;
            int d = abs(ex - cx) + abs(ey - cy);
            int value = (int)(app->player_cash[i] / 5) + (int)(world->cash_earned[i] / 3);
            if (!app->options.win_by_money) value += 50;
            int score = value - d;
            if (score > best_score || (score == best_score && d < best_d)) {
                best_score = score; best_e = i; bex = ex; bey = ey; best_d = d;
            }
        }
        // Target monsters (clones, furries, grenadiers, etc.) — they're dangerous
        for (int m = world->num_players; m < world->num_actors; m++) {
            if (world->actors[m].is_dead || !world->actors[m].is_active) continue;
            if (world->actors[m].kind == ACTOR_CLONE && world->actors[m].clone_owner == p) continue;
            int ex = world->actors[m].pos.x / 10, ey = (world->actors[m].pos.y - 30) / 10;
            int d = abs(ex - cx) + abs(ey - cy);
            // Clones: medium value. Other monsters: low value but still worth killing if close
            int value = (world->actors[m].kind == ACTOR_CLONE) ? 30 : 15;
            int score = value - d;
            if (score > best_score || (score == best_score && d < best_d)) {
                best_score = score; best_e = m; bex = ex; bey = ey; best_d = d;
            }
        }
        if (best_e >= 0) {
            bool los = cpu_line_of_sight(world, cx, cy, bex, bey);

            // ON the enemy or adjacent: drop bomb and flee
            if (best_d <= 1) {
                uint8_t act = cpu_use_any_weapon(app, actor, p);
                if (act == NET_INPUT_ACTION) { cpu_flee_mode[p] = true; return NET_INPUT_ACTION; }
                if (act) return act; // cycling to a weapon
            }

            // Ranged weapons in line of sight
            if (los && best_d <= 6) {
                // Face toward enemy first
                uint8_t face = cpu_face_toward(actor, cx, cy, bex, bey);
                if (face) return face;
                // Flamethrower (range 6)
                if (app->player_inventory[p][EQUIP_FLAMETHROWER] > 0) {
                    bool ok; uint8_t sw = cpu_switch_to(app, actor, p, EQUIP_FLAMETHROWER, &ok);
                    return (ok && sw) ? sw : (ok ? cpu_place_bomb(p) : 0);
                }
                // Grenade (range 2-5)
                if (best_d >= 2 && best_d <= 5 && app->player_inventory[p][EQUIP_GRENADE] > 0) {
                    bool ok; uint8_t sw = cpu_switch_to(app, actor, p, EQUIP_GRENADE, &ok);
                    return (ok && sw) ? sw : (ok ? cpu_place_bomb(p) : 0);
                }
            }

            // Medium range (2-4): place bomb if safe and in LOS, or bomb through thin wall
            if (best_d <= 4 && safe_to_bomb) {
                if (los) {
                    // Place bomb in the enemy's path
                    uint8_t act = cpu_use_any_weapon(app, actor, p);
                    if (act == NET_INPUT_ACTION) return cpu_place_bomb(p);
                    if (act) return act;
                }
                // Bomb through thin wall
                for (int d = 0; d < 4; d++) {
                    int wx = cx + DDX[d], wy = cy + DDY[d];
                    if (wx < 0 || wx >= MAP_WIDTH || wy < 0 || wy >= MAP_HEIGHT) continue;
                    if (!cpu_bombable(world->tiles[wy][wx])) continue;
                    int fx = wx + DDX[d], fy = wy + DDY[d];
                    if (fx < 0 || fx >= MAP_WIDTH || fy < 0 || fy >= MAP_HEIGHT) continue;
                    if (abs(fx - bex) + abs(fy - bey) <= 1) {
                        uint8_t face = cpu_face_toward(actor, cx, cy, wx, wy);
                        if (face) return face;
                        int bw = cpu_worth_bombing(world, wx, wy) ? cpu_pick_bomb_from(app, world, p, cx, cy, wx, wy) : -1;
                        if (bw >= 0) {
                            bool ok; uint8_t sw = cpu_switch_to(app, actor, p, bw, &ok);
                            return (ok && sw) ? sw : (ok ? cpu_place_bomb(p) : 0);
                        }
                    }
                }
            }

            // Medium range without LOS: only bomb if adjacent wall is between us and enemy
            // (don't waste bombs in open space when enemy is behind a far wall)
            if (best_d <= 4 && safe_to_bomb && has_bombs && !los) {
                // Check if there's a bombable wall in the direction of the enemy
                // AND the enemy is just beyond that wall
                for (int d = 0; d < 4; d++) {
                    int wx = cx + DDX[d], wy = cy + DDY[d];
                    if (wx < 0 || wx >= MAP_WIDTH || wy < 0 || wy >= MAP_HEIGHT) continue;
                    if (!cpu_bombable(world->tiles[wy][wx])) continue;
                    // Check: would clearing this wall get us closer to the enemy?
                    int fx = wx + DDX[d], fy = wy + DDY[d];
                    if (fx >= 0 && fx < MAP_WIDTH && fy >= 0 && fy < MAP_HEIGHT
                        && (abs(fx - bex) + abs(fy - bey)) < best_d) {
                        uint8_t face = cpu_face_toward(actor, cx, cy, wx, wy);
                        if (face) return face;
                        int bw = cpu_worth_bombing(world, wx, wy) ? cpu_pick_bomb_from(app, world, p, cx, cy, wx, wy) : -1;
                        if (bw >= 0) { uint8_t sw = cpu_select(app, actor, p, bw); return sw ? sw : cpu_place_bomb(p); }
                    }
                }
            }
            // Push bombs/barrels/boulders toward enemy
            for (int d = 0; d < 4; d++) {
                int nx = cx + DDX[d], ny = cy + DDY[d];
                if (nx < 0 || nx >= MAP_WIDTH || ny < 0 || ny >= MAP_HEIGHT) continue;
                uint8_t tile = world->tiles[ny][nx];
                int bx = nx + DDX[d], by = ny + DDY[d];
                if (bx < 0 || bx >= MAP_WIDTH || by < 0 || by >= MAP_HEIGHT) continue;
                if (!is_passable(world->tiles[by][bx])) continue;
                // Check no actor blocking push destination
                bool blocked = false;
                for (int ai = 0; ai < world->num_actors; ai++) {
                    if (world->actors[ai].is_dead) continue;
                    if (world->actors[ai].pos.x / 10 == bx && (world->actors[ai].pos.y - 30) / 10 == by)
                        { blocked = true; break; }
                }
                if (blocked) continue;
                bool toward = (DDX[d] != 0 && bey == ny && (bex - nx) * DDX[d] > 0)
                           || (DDY[d] != 0 && bex == nx && (bey - ny) * DDY[d] > 0);
                if (toward && ((cpu_is_bomb(tile) && world->timer[ny][nx] > 30) || tile == TILE_BARREL))
                    return DIR_FLAGS[d];
            }
            // Tactical: place bomb to cut off enemy escape routes
            // If enemy has limited escape (1-2 open adjacent tiles), bomb near them
            if (best_d <= 5 && safe_to_bomb && has_bombs) {
                int enemy_exits = 0;
                for (int d = 0; d < 4; d++) {
                    int enx = bex + DDX[d], eny = bey + DDY[d];
                    if (enx >= 0 && enx < MAP_WIDTH && eny >= 0 && eny < MAP_HEIGHT
                        && cpu_walkable(world->tiles[eny][enx]) && cpu_danger(world, enx, eny) == 0)
                        enemy_exits++;
                }
                if (enemy_exits <= 2 && los) {
                    // Enemy is cornered — place bomb to seal them in
                    uint8_t act = cpu_use_any_weapon(app, actor, p);
                    if (act == NET_INPUT_ACTION) return cpu_place_bomb(p);
                    if (act) return act;
                }
            }
            // Path toward enemy — always pursue (killing = cash in both modes)
            {
                EnemyGoalCtx eg = {bex, bey};
                AStarResult r = cpu_astar(world, cx, cy, goal_near_enemy, &eg, drill, has_bombs, search_limit, p);
                if (r.dir) {
                    if (r.needs_bomb) {
                        int tx = cx, ty = cy;
                        for (int d = 0; d < 4; d++) if (DIR_FLAGS[d] == r.dir) { tx = cx+DDX[d]; ty = cy+DDY[d]; }
                        int bw = cpu_worth_bombing(world, tx, ty) ? cpu_pick_bomb_from(app, world, p, cx, cy, tx, ty) : -1;
                        if (bw >= 0 && safe_to_bomb) { uint8_t sw = cpu_select(app, actor, p, bw); return sw ? sw : cpu_place_bomb(p); }
                        // Bombing failed — don't walk into the wall, skip to next section
                    } else {
                        for (int d = 0; d < 4; d++) if (DIR_FLAGS[d] == r.dir) cpu_last_dir[p] = d;
                        return r.dir;
                    }
                }
            }
        }
    }

    // ===== 3. TREASURE: find and collect =====
    {
        AStarResult r = cpu_astar(world, cx, cy, goal_treasure, NULL, drill, has_bombs, search_limit, p);
        if (r.dir) {
            if (r.needs_bomb) {
                int tx = cx, ty = cy;
                for (int d = 0; d < 4; d++) if (DIR_FLAGS[d] == r.dir) { tx = cx+DDX[d]; ty = cy+DDY[d]; }
                int bw = cpu_worth_bombing(world, tx, ty) ? cpu_pick_bomb_from(app, world, p, cx, cy, tx, ty) : -1;
                if (bw >= 0 && safe_to_bomb) { uint8_t sw = cpu_select(app, actor, p, bw); return sw ? sw : cpu_place_bomb(p); }
                // Bombing failed — don't walk into wall
            } else {
                for (int d = 0; d < 4; d++) if (DIR_FLAGS[d] == r.dir) cpu_last_dir[p] = d;
                return r.dir;
            }
        }
    }

    // ===== 4. EXPLORE: visit unvisited areas =====
    {
        ExploreCtx ec = {p};
        AStarResult r = cpu_astar(world, cx, cy, goal_explore, &ec, drill, has_bombs, search_limit, p);
        if (r.dir) {
            if (r.needs_bomb) {
                int tx = cx, ty = cy;
                for (int d = 0; d < 4; d++) if (DIR_FLAGS[d] == r.dir) { tx = cx+DDX[d]; ty = cy+DDY[d]; }
                int bw = cpu_worth_bombing(world, tx, ty) ? cpu_pick_bomb_from(app, world, p, cx, cy, tx, ty) : -1;
                if (bw >= 0 && safe_to_bomb) { uint8_t sw = cpu_select(app, actor, p, bw); return sw ? sw : cpu_place_bomb(p); }
            } else {
                for (int d = 0; d < 4; d++) if (DIR_FLAGS[d] == r.dir) cpu_last_dir[p] = d;
                return r.dir;
            }
        }
    }

    // ===== 4b. PENALTY-FREE PATHFIND to treasure, enemies, or unexplored =====
    {
        AStarResult r = cpu_astar(world, cx, cy, goal_treasure, NULL, drill, has_bombs, ASTAR_MAX, -1);
        if (r.dir) {
            if (r.needs_bomb && safe_to_bomb) {
                int tx = cx, ty = cy;
                for (int d = 0; d < 4; d++) if (DIR_FLAGS[d] == r.dir) { tx = cx+DDX[d]; ty = cy+DDY[d]; }
                int bw = cpu_worth_bombing(world, tx, ty) ? cpu_pick_bomb_from(app, world, p, cx, cy, tx, ty) : -1;
                if (bw >= 0) { uint8_t sw = cpu_select(app, actor, p, bw); return sw ? sw : cpu_place_bomb(p); }
            } else if (!r.needs_bomb) {
                for (int d = 0; d < 4; d++) if (DIR_FLAGS[d] == r.dir) cpu_last_dir[p] = d;
                return r.dir;
            }
        }
        ExploreCtx ec = {p};
        r = cpu_astar(world, cx, cy, goal_explore, &ec, drill, has_bombs, ASTAR_MAX, -1);
        if (r.dir) {
            if (r.needs_bomb && safe_to_bomb) {
                int tx = cx, ty = cy;
                for (int d = 0; d < 4; d++) if (DIR_FLAGS[d] == r.dir) { tx = cx+DDX[d]; ty = cy+DDY[d]; }
                int bw = cpu_worth_bombing(world, tx, ty) ? cpu_pick_bomb_from(app, world, p, cx, cy, tx, ty) : -1;
                if (bw >= 0) { uint8_t sw = cpu_select(app, actor, p, bw); return sw ? sw : cpu_place_bomb(p); }
            } else if (!r.needs_bomb) {
                for (int d = 0; d < 4; d++) if (DIR_FLAGS[d] == r.dir) cpu_last_dir[p] = d;
                return r.dir;
            }
        }
    }

    // ===== 5. MOMENTUM: keep moving, try all options =====
    // Helper: can we actually move into this tile in direction d?
    // Excludes immovable pushables (boulder/bomb against wall)
    #define CAN_ENTER(d, nx, ny) ({ \
        bool _ok = false; \
        uint8_t _t = world->tiles[ny][nx]; \
        if (cpu_walkable(_t) || cpu_diggable(_t)) { \
            _ok = true; \
            /* Check: is it a pushable stuck against a wall? */ \
            if (_t == TILE_BOULDER) { \
                int _bx = (nx) + DDX[d], _by = (ny) + DDY[d]; \
                if (_bx < 0 || _bx >= MAP_WIDTH || _by < 0 || _by >= MAP_HEIGHT \
                    || !is_passable(world->tiles[_by][_bx])) _ok = false; \
            } \
        } \
        _ok; })
    {
        int reverse = -1;
        if (cpu_last_dir[p] >= 0) {
            static const int rev[] = {1, 0, 3, 2};
            reverse = rev[cpu_last_dir[p]];
            // Try continuing forward
            int fd = cpu_last_dir[p];
            int nx = cx + DDX[fd], ny = cy + DDY[fd];
            if (nx >= 0 && nx < MAP_WIDTH && ny >= 0 && ny < MAP_HEIGHT && CAN_ENTER(fd, nx, ny))
                return DIR_FLAGS[fd];
        }
        // Try all non-reverse directions: walk/dig, then bomb
        int start = rand() % 4;
        for (int pass = 0; pass < 2; pass++) {
            for (int i = 0; i < 4; i++) {
                int d = (start + i) % 4;
                if (d == reverse) continue;
                int nx = cx + DDX[d], ny = cy + DDY[d];
                if (nx < 0 || nx >= MAP_WIDTH || ny < 0 || ny >= MAP_HEIGHT) continue;
                if (pass == 0 && CAN_ENTER(d, nx, ny)) {
                    cpu_last_dir[p] = d;
                    return DIR_FLAGS[d];
                }
                if (pass == 1 && (cpu_bombable(world->tiles[ny][nx]) || world->tiles[ny][nx] == TILE_BOULDER)
                    && has_bombs && safe_to_bomb) {
                    int bw = cpu_worth_bombing(world, nx, ny) ? cpu_pick_bomb_from(app, world, p, cx, cy, nx, ny) : -1;
                    if (bw >= 0) {
                        cpu_last_dir[p] = d;
                        uint8_t sw = cpu_select(app, actor, p, bw);
                        return sw ? sw : cpu_place_bomb(p);
                    }
                }
            }
        }
        // All non-reverse failed — try reverse (walk/dig/bomb)
        if (reverse >= 0) {
            int nx = cx + DDX[reverse], ny = cy + DDY[reverse];
            if (nx >= 0 && nx < MAP_WIDTH && ny >= 0 && ny < MAP_HEIGHT) {
                if (CAN_ENTER(reverse, nx, ny)) { cpu_last_dir[p] = reverse; return DIR_FLAGS[reverse]; }
                if ((cpu_bombable(world->tiles[ny][nx]) || world->tiles[ny][nx] == TILE_BOULDER)
                    && has_bombs && safe_to_bomb) {
                    int bw = cpu_worth_bombing(world, nx, ny) ? cpu_pick_bomb_from(app, world, p, cx, cy, nx, ny) : -1;
                    if (bw >= 0) {
                        cpu_last_dir[p] = reverse;
                        uint8_t sw = cpu_select(app, actor, p, bw);
                        return sw ? sw : cpu_place_bomb(p);
                    }
                }
                cpu_last_dir[p] = reverse;
                return DIR_FLAGS[reverse]; // last resort: try anyway
            }
        }
        // Truly stuck — pick any traversable direction
        for (int d = 0; d < 4; d++) {
            int nx = cx + DDX[d], ny = cy + DDY[d];
            if (nx >= 0 && nx < MAP_WIDTH && ny >= 0 && ny < MAP_HEIGHT
                && (cpu_walkable(world->tiles[ny][nx]) || cpu_diggable(world->tiles[ny][nx]))) {
                cpu_last_dir[p] = d;
                return DIR_FLAGS[d];
            }
        }
    }
    #undef CAN_ENTER

    // Absolute last resort: cycle weapons or stop
    return has_bombs ? NET_INPUT_CYCLE : NET_INPUT_STOP;
}
