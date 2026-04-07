#ifndef CPU_H
#define CPU_H

#include "app.h"
#include "game.h"
#include <SDL.h>

// Check if a player slot is CPU-controlled
static inline bool is_cpu_player(const App* app, int p) {
    return app->roster.identities[p] == ROSTER_CPU;
}

// Initialize CPU personality for a player slot and write name into buf.
void cpu_assign(int p, char* name_buf, int name_max);

// Generate one frame of CPU input flags (NET_INPUT_* format)
uint8_t cpu_generate_input(App* app, World* world, int p);

// CPU shops autonomously: buys equipment based on strategy and level analysis.
void cpu_shop_tick(App* app, int p, int* cursor, bool* ready);

// Toggle the AI debug overlay (path + current goal verb), default OFF.
void cpu_debug_toggle(void);
bool cpu_debug_enabled(void);
void cpu_debug_set(bool on);

// Render the AI debug overlay over the game canvas. No-op if disabled.
void cpu_debug_render(SDL_Renderer* renderer, App* app, World* world);

#endif
