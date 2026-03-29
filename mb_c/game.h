#ifndef GAME_H
#define GAME_H

#include "app.h"
#include <stdint.h>
#include <stdbool.h>

#define MAP_WIDTH 64
#define MAP_HEIGHT 45
#define TILE_SIZE 10
#define MAX_ACTORS 128

// Tile Definitions
#define TILE_PASSAGE 0x30
#define TILE_WALL 0x31
#define TILE_SAND1 0x32
#define TILE_SAND2 0x33
#define TILE_SAND3 0x34
#define TILE_GRAVEL_LIGHT 0x35
#define TILE_GRAVEL_HEAVY 0x36
#define TILE_STONE_TOP_LEFT 0x37
#define TILE_STONE_TOP_RIGHT 0x38
#define TILE_STONE_BOTTOM_RIGHT 0x39
#define TILE_STONE_BOTTOM_LEFT 0x41
#define TILE_BOULDER 0x42
#define TILE_STONE1 0x43
#define TILE_STONE2 0x44
#define TILE_STONE3 0x45
#define TILE_STONE4 0x46

// Enemy tiles (directional variants: Right, Left, Up, Down)
#define TILE_FURRY_RIGHT 0x47
#define TILE_FURRY_LEFT 0x48
#define TILE_FURRY_UP 0x49
#define TILE_FURRY_DOWN 0x4A
#define TILE_GRENADIER_RIGHT 0x4B
#define TILE_GRENADIER_LEFT 0x4C
#define TILE_GRENADIER_UP 0x4D
#define TILE_GRENADIER_DOWN 0x4E
#define TILE_SLIME_RIGHT 0x4F
#define TILE_SLIME_LEFT 0x50
#define TILE_SLIME_UP 0x51
#define TILE_SLIME_DOWN 0x52
#define TILE_ALIEN_RIGHT 0x53
#define TILE_ALIEN_LEFT 0x54
#define TILE_ALIEN_UP 0x55
#define TILE_ALIEN_DOWN 0x56

#define TILE_SMALL_BOMB1 0x57
#define TILE_BIG_BOMB1 0x58
#define TILE_DYNAMITE1 0x59
#define TILE_SMOKE1 0x61
#define TILE_SMOKE2 0x62
#define TILE_SMALL_RADIO_BLUE 0x63
#define TILE_BIG_RADIO_BLUE 0x64
#define TILE_MINE 0x65
#define TILE_BLOOD 0x66
#define TILE_SMALL_RADIO_GREEN 0x67
#define TILE_BIG_RADIO_GREEN 0x68
#define TILE_SMALL_RADIO_YELLOW 0x69
#define TILE_BIG_RADIO_YELLOW 0x6A
#define TILE_EXIT 0x6B
#define TILE_DOOR 0x6C
#define TILE_MEDIKIT 0x6D
#define TILE_BIOMASS 0x6F
#define TILE_STONE_CRACKED_LIGHT 0x70
#define TILE_STONE_CRACKED_HEAVY 0x71
#define TILE_DIAMOND 0x73
#define TILE_SMALL_BOMB2 0x77
#define TILE_SMALL_BOMB3 0x78
#define TILE_WEAPONS_CRATE 0x79
#define TILE_NAPALM_EXTINGUISHED 0x7C
#define TILE_SMALL_BOMB_EXTINGUISHED 0x7D
#define TILE_BIG_BOMB_EXTINGUISHED 0x7E
#define TILE_NAPALM1 0x7F
#define TILE_LARGE_CRUCIFIX_BOMB 0x80
#define TILE_PLASTIC_BOMB 0x81
#define TILE_SMALL_RADIO_RED 0x82
#define TILE_BIG_RADIO_RED 0x83
#define TILE_EXPLOSION 0x84
#define TILE_MONSTER_DYING 0x85
#define TILE_MONSTER_SMOKE1 0x86
#define TILE_MONSTER_SMOKE2 0x87
#define TILE_SMALL_CRUCIFIX_BOMB 0x8A
#define TILE_BIG_BOMB2 0x8B
#define TILE_BIG_BOMB3 0x8C
#define TILE_DYNAMITE2 0x8D
#define TILE_DYNAMITE3 0x8E
#define TILE_SMALL_PICKAXE 0x8F
#define TILE_LARGE_PICKAXE 0x90
#define TILE_DRILL 0x91
#define TILE_GOLD_SHIELD 0x92
#define TILE_GOLD_EGG 0x93
#define TILE_GOLD_PILE 0x94
#define TILE_GOLD_BRACELET 0x95
#define TILE_GOLD_BAR 0x96
#define TILE_GOLD_CROSS 0x97
#define TILE_GOLD_SCEPTER 0x98
#define TILE_GOLD_RUBIN 0x99
#define TILE_GOLD_CROWN 0x9A
#define TILE_PLASTIC 0x9B
#define TILE_EXPLOSIVE_PLASTIC 0xA0
#define TILE_EXPLOSIVE_PLASTIC_BOMB 0xA1
#define TILE_DIGGER_BOMB 0xA2
#define TILE_NAPALM2 0xA3
#define TILE_TELEPORT 0x9C
#define TILE_ATOMIC1 0x9D
#define TILE_ATOMIC2 0x9E
#define TILE_ATOMIC3 0x9F
#define TILE_BARREL 0xA4
#define TILE_GRENADE_FLY_R 0xA5
#define TILE_GRENADE_FLY_L 0xA6
#define TILE_GRENADE_FLY_D 0xA7
#define TILE_GRENADE_FLY_U 0xA8
#define TILE_METAL_WALL_PLACED 0xA9
#define TILE_DYNAMITE_EXTINGUISHED 0xAA
#define TILE_JUMPING_BOMB 0xAB
#define TILE_BRICK 0xAC
#define TILE_BRICK_CRACKED_LIGHT 0xAD
#define TILE_BRICK_CRACKED_HEAVY 0xAE
#define TILE_SLIME_CORPSE 0xAF
#define TILE_SLIME_DYING 0xB0
#define TILE_SLIME_SMOKE1 0xB1
#define TILE_SLIME_SMOKE2 0xB2
#define TILE_LIFE_ITEM 0xB3
#define TILE_BUTTON_OFF 0xB4

#define BURNED_L 1
#define BURNED_R 2
#define BURNED_U 4
#define BURNED_D 8

typedef enum {
    DIR_RIGHT = 0,
    DIR_LEFT = 1,
    DIR_UP = 2,
    DIR_DOWN = 3
} Direction;

typedef enum {
    ACTOR_PLAYER,
    ACTOR_FURRY,
    ACTOR_GRENADIER,
    ACTOR_SLIME,
    ACTOR_ALIEN,
    ACTOR_CLONE
} ActorKind;

typedef struct {
    int x, y;
} Position;

typedef struct {
    Position pos;
    Direction facing;
    bool moving;
    int health;
    int max_health;
    int drilling;
    int animation;
    int animation_timer;
    bool is_digging;
    bool is_dead;
    int selected_weapon;
    ActorKind kind;
    int clone_owner; // player index for ACTOR_CLONE, -1 otherwise
    bool is_active;
    int super_drill_count;
} Actor;

typedef struct {
    uint8_t tiles[MAP_HEIGHT][MAP_WIDTH];
    int16_t timer[MAP_HEIGHT][MAP_WIDTH];
    int32_t hits[MAP_HEIGHT][MAP_WIDTH];
    uint8_t burned[MAP_HEIGHT][MAP_WIDTH];
    bool open_door[MAP_HEIGHT][MAP_WIDTH];

    Actor actors[MAX_ACTORS];
    int num_players;
    int num_actors;
    int round_end_timer;
    int round_counter;
    bool exited;
    uint8_t bomb_damage;
    bool darkness;
    bool campaign_mode;
    int lives_gained;
    uint32_t cash_earned[MAX_PLAYERS]; // per-round accumulated cash
    uint32_t treasures_collected[MAX_PLAYERS];
    uint32_t bombs_dropped[MAX_PLAYERS];
    uint32_t deaths[MAX_PLAYERS];
    uint32_t meters_ran[MAX_PLAYERS];
    bool fog[MAP_HEIGHT][MAP_WIDTH];

    // Off-screen map canvas for persistent tile + effect rendering
    SDL_Texture* canvas;
    uint8_t canvas_tiles[MAP_HEIGHT][MAP_WIDTH]; // snapshot for diffing
    bool canvas_fog[MAP_HEIGHT][MAP_WIDTH];      // snapshot for fog diffing
} World;

bool is_passable(uint8_t val);
bool is_stone(uint8_t val);

typedef enum {
    ROUND_END_NORMAL,
    ROUND_END_EXITED,
    ROUND_END_FAILED,
    ROUND_END_QUIT,
    ROUND_END_FINAL
} RoundEndType;

typedef struct {
    bool player_survived[MAX_PLAYERS];
    uint32_t player_cash_earned[MAX_PLAYERS];
    uint32_t treasures_collected[MAX_PLAYERS];
    uint32_t bombs_dropped[MAX_PLAYERS];
    uint32_t deaths[MAX_PLAYERS];
    uint32_t meters_ran[MAX_PLAYERS];
    uint32_t gold_remaining;
    RoundEndType end_type;
    int lives_gained;
} RoundResult;

void game_init_world(World* world, uint8_t* level_data, int num_players);
RoundResult game_run(App* app, ApplicationContext* ctx, uint8_t* level_data, NetContext* net);

void game_seed_rng(uint32_t seed);
int game_rand(void);

#endif // GAME_H
