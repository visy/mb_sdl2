#ifndef GAME_H
#define GAME_H

#include "app.h"
#include <stdint.h>
#include <stdbool.h>

#define MAP_WIDTH 64
#define MAP_HEIGHT 45
#define TILE_SIZE 10

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
#define TILE_SMALL_BOMB1 0x57
#define TILE_BIG_BOMB1 0x58
#define TILE_DYNAMITE1 0x59
#define TILE_BLOOD 0x66
#define TILE_MEDIKIT 0x6D
#define TILE_STONE_CRACKED_LIGHT 0x70
#define TILE_STONE_CRACKED_HEAVY 0x71
#define TILE_DIAMOND 0x73
#define TILE_SMALL_BOMB2 0x77
#define TILE_SMALL_BOMB3 0x78
#define TILE_WEAPONS_CRATE 0x79
#define TILE_BIG_BOMB2 0x8B
#define TILE_BIG_BOMB3 0x8C
#define TILE_DYNAMITE2 0x8D
#define TILE_DYNAMITE3 0x8E
#define TILE_GOLD_SHIELD 0x92
#define TILE_GOLD_EGG 0x93
#define TILE_GOLD_PILE 0x94
#define TILE_GOLD_BRACELET 0x95
#define TILE_GOLD_BAR 0x96
#define TILE_GOLD_CROSS 0x97
#define TILE_GOLD_SCEPTER 0x98
#define TILE_GOLD_RUBIN 0x99
#define TILE_GOLD_CROWN 0x9A
#define TILE_TELEPORT 0x9C
#define TILE_ATOMIC1 0x9D
#define TILE_ATOMIC2 0x9E
#define TILE_ATOMIC3 0x9F
#define TILE_SLIME_CORPSE 0xAF
#define TILE_SMOKE1 0x61
#define TILE_SMOKE2 0x62
#define TILE_EXPLOSION 0x84

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
} Actor;

typedef struct {
    uint8_t tiles[MAP_HEIGHT][MAP_WIDTH];
    int16_t timer[MAP_HEIGHT][MAP_WIDTH];
    int32_t hits[MAP_HEIGHT][MAP_WIDTH];
    uint8_t burned[MAP_HEIGHT][MAP_WIDTH];
    
    Actor actors[MAX_PLAYERS];
    int num_players;
    int round_end_timer;
    bool exited;
    bool god_mode;
    uint8_t bomb_damage;
    bool darkness;
    bool fog[MAP_HEIGHT][MAP_WIDTH];
} World;

bool is_passable(uint8_t val);
bool is_stone(uint8_t val);

typedef struct {
    bool player_survived[MAX_PLAYERS];
    uint32_t player_cash_earned[MAX_PLAYERS];
} RoundResult;

void game_init_world(World* world, uint8_t* level_data, int num_players);
RoundResult game_run(App* app, ApplicationContext* ctx, uint8_t* level_data);

#endif // GAME_H
