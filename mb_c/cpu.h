#ifndef CPU_H
#define CPU_H

#include "app.h"
#include "game.h"

// Check if a player slot is CPU-controlled
static inline bool is_cpu_player(const App* app, int p) {
    return app->roster.identities[p] == ROSTER_CPU;
}

// Generate one frame of CPU input flags (NET_INPUT_* format)
uint8_t cpu_generate_input(App* app, World* world, int p);

// CPU shops autonomously: buys equipment based on strategy and level analysis.
void cpu_shop_tick(App* app, int p, int* cursor, bool* ready);

#endif
