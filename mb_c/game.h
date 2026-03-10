#ifndef GAME_H
#define GAME_H

#include "app.h"
#include <stdint.h>
#include <stdbool.h>

#define MAP_WIDTH 64
#define MAP_HEIGHT 45
#define TILE_SIZE 10

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
} Actor;

typedef struct {
    uint8_t tiles[MAP_HEIGHT][MAP_WIDTH];
    int16_t timer[MAP_HEIGHT][MAP_WIDTH];
    int32_t hits[MAP_HEIGHT][MAP_WIDTH];
    
    Actor player;
    bool exited;
} World;

void game_init_world(World* world, uint8_t* level_data);
void game_run(App* app, ApplicationContext* ctx, uint8_t* level_data);

#endif // GAME_H
