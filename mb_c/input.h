#ifndef INPUT_H
#define INPUT_H

#include <SDL2/SDL.h>
#include <stdbool.h>

typedef enum {
    ACT_UP, ACT_DOWN, ACT_LEFT, ACT_RIGHT,
    ACT_STOP, ACT_ACTION, ACT_CYCLE, ACT_REMOTE,
    ACT_GOD,
    ACT_MAX_PLAYER
} ActionType;

typedef enum {
    BIND_NONE,
    BIND_KEY,
    BIND_BTN,
    BIND_AXIS
} BindType;

typedef struct {
    BindType type;
    int id;
    int extra; // Used for axis direction: -1 or 1
} Binding;

#define MAX_BINDINGS 8

typedef struct {
    Binding bindings[ACT_MAX_PLAYER][MAX_BINDINGS];
} PlayerInputConfig;

typedef struct {
    PlayerInputConfig p[2];
    int axis_state[2][SDL_CONTROLLER_AXIS_MAX];
} InputConfig;

void input_get_default(InputConfig* config);
bool input_load_config(InputConfig* config, const char* filename);
void input_save_config(const InputConfig* config, const char* filename);
void input_print(const InputConfig* config);

// Helper to check if an event matches an action for a player
ActionType input_map_event(const SDL_Event* e, int p_idx, InputConfig* config);

#endif // INPUT_H
