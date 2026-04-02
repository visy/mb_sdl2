#ifndef APP_H
#define APP_H

#include "context.h"
#include "fonts.h"
#include "glyphs.h"
#include "input.h"
#include "net.h"

typedef enum {
    EQUIP_SMALL_BOMB,
    EQUIP_BIG_BOMB,
    EQUIP_DYNAMITE,
    EQUIP_ATOMIC_BOMB,
    EQUIP_SMALL_RADIO,
    EQUIP_LARGE_RADIO,
    EQUIP_GRENADE,
    EQUIP_MINE,
    EQUIP_FLAMETHROWER,
    EQUIP_NAPALM,
    EQUIP_BARREL,
    EQUIP_SMALL_CRUCIFIX,
    EQUIP_LARGE_CRUCIFIX,
    EQUIP_PLASTIC,
    EQUIP_EXPLOSIVE_PLASTIC,
    EQUIP_DIGGER,
    EQUIP_METAL_WALL,
    EQUIP_SMALL_PICKAXE,
    EQUIP_LARGE_PICKAXE,
    EQUIP_DRILL,
    EQUIP_TELEPORT,
    EQUIP_CLONE,
    EQUIP_BIOMASS,
    EQUIP_EXTINGUISHER,
    EQUIP_ARMOR,
    EQUIP_JUMPING_BOMB,
    EQUIP_SUPER_DRILL,
    EQUIP_TOTAL
} Equipment;

#define MAX_PLAYERS 4
#define ROSTER_CPU -2  // special identity value for CPU players
#define ROSTER_MAX 32
#define ROSTER_NAME_MAX 25
#define ROSTER_RECORD_SIZE 101
#define ROSTER_HISTORY_SIZE 34

typedef struct {
    bool active;
    char name[ROSTER_NAME_MAX];
    uint32_t tournaments;
    uint32_t tournaments_wins;
    uint32_t rounds;
    uint32_t rounds_wins;
    uint32_t treasures_collected;
    uint32_t total_money;
    uint32_t bombs_bought;
    uint32_t bombs_dropped;
    uint32_t deaths;
    uint32_t meters_ran;
    uint8_t history[ROSTER_HISTORY_SIZE];
} RosterInfo;

typedef struct {
    RosterInfo entries[ROSTER_MAX];
    int8_t identities[MAX_PLAYERS]; // roster index per player slot, -1 = none
} PlayerRoster;

#define HIGHSCORE_MAX 10
#define HIGHSCORE_NAME_LEN 20
#define HIGHSCORE_RECORD_SIZE 26

typedef struct {
    char name[HIGHSCORE_NAME_LEN + 1];
    uint8_t level;
    uint32_t money;
} HighScoreEntry;

// Per-game stats accumulator (reset each tournament, merged at end)
typedef struct {
    uint32_t rounds;
    uint32_t rounds_wins;
    uint32_t treasures_collected;
    uint32_t total_money;
    uint32_t bombs_bought;
    uint32_t bombs_dropped;
    uint32_t deaths;
    uint32_t meters_ran;
} GameStats;

typedef struct {
    uint16_t cash;
    uint8_t treasures;
    uint16_t rounds;
    uint16_t round_time_secs;
    uint8_t players;
    uint16_t speed;
    uint8_t bomb_damage;
    bool darkness;
    bool free_market;
    bool selling;
    bool win_by_money;
} GameOptions;

typedef struct {
    TexturePalette title;
    TexturePalette main_menu;
    TexturePalette sika; // Glyphs
    TexturePalette shop;
    TexturePalette players;
    
    TexturePalette info[4];
    TexturePalette codes;
    TexturePalette options_menu;
    TexturePalette level_select;
    TexturePalette final_screen;
    TexturePalette avatar_win[MAX_PLAYERS];
    TexturePalette avatar_lose[MAX_PLAYERS];
    TexturePalette avatar_draw[MAX_PLAYERS];
    TexturePalette game_over;
    TexturePalette congratu;
    TexturePalette halloffa;
    TexturePalette select_players;
    TexturePalette edit_help;
    TexturePalette edit_panel;

    Glyphs glyphs;
    Font font;
    char registered[256];

    Mix_Chunk* sounds[64];
    char sound_names[64][32];
    int sound_count;
    Mix_Chunk* sound_kili;
    Mix_Chunk* sound_picaxe;
    Mix_Chunk* sound_explos1;
    Mix_Chunk* sound_explos2;
    Mix_Chunk* sound_explos3;
    Mix_Chunk* sound_explos4;
    Mix_Chunk* sound_explos5;
    Mix_Chunk* sound_pikkupom;
    Mix_Chunk* sound_aargh;
    Mix_Chunk* sound_urethan;
    Mix_Chunk* sound_applause;

    char level_names[128][32];
    uint8_t* level_data[128];
    int level_count;

    uint32_t player_cash[MAX_PLAYERS];
    uint32_t player_rounds_won[MAX_PLAYERS];
    int player_inventory[MAX_PLAYERS][EQUIP_TOTAL];
    char player_name[MAX_PLAYERS][16];
    int selected_levels[128];
    int selected_level_count;

    uint8_t* campaign_levels[15];
    int campaign_level_count;
    int player_lives;

    PlayerRoster roster;
    GameStats game_stats[MAX_PLAYERS];
    HighScoreEntry highscores[HIGHSCORE_MAX];

    InputConfig input_config;
    GameOptions options;
    int current_round;

    NetContext net;
} App;

bool app_init(App* app, ApplicationContext* ctx);
void app_destroy(App* app);

void app_run_main_menu(App* app, ApplicationContext* ctx, bool campaign_mode);

// Pause menu
typedef enum {
    PAUSE_NONE = -1,
    PAUSE_EXIT_LEVEL = 0,
    PAUSE_END_GAME,
    PAUSE_EDITOR,
    PAUSE_ED_NEW,
    PAUSE_ED_LOAD,
    PAUSE_ED_SAVE,
    PAUSE_ED_SAVE_QUIT,
    PAUSE_ED_QUIT
} PauseChoice;

typedef enum {
    PAUSE_CTX_GAMEPLAY,
    PAUSE_CTX_SHOP,
    PAUSE_CTX_MAINMENU,
    PAUSE_CTX_EDITOR
} PauseContext;

void app_handle_hotplug(App* app, ApplicationContext* ctx, const SDL_Event* e);
bool is_pause_event(const SDL_Event* e, InputConfig* config);
PauseChoice pause_menu(App* app, ApplicationContext* ctx, PauseContext pctx);
PauseChoice pause_menu_net(App* app, ApplicationContext* ctx, PauseContext pctx, NetContext* net);

#endif // APP_H
