#include "input.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdbool.h>

static const char* action_names[] = {
    "UP", "DOWN", "LEFT", "RIGHT",
    "STOP", "ACTION", "CYCLE", "REMOTE", "GOD"
};

static void set_default_pad_bindings(PlayerInputConfig* p) {
    // Find next free binding slot for each action and add gamepad bindings
    int b;
    for (b = 0; b < MAX_BINDINGS && p->bindings[ACT_UP][b].type != BIND_NONE; b++);
    if (b < MAX_BINDINGS) p->bindings[ACT_UP][b] = (Binding){BIND_BTN, SDL_CONTROLLER_BUTTON_DPAD_UP, 0};
    if (b + 1 < MAX_BINDINGS) p->bindings[ACT_UP][b + 1] = (Binding){BIND_AXIS, SDL_CONTROLLER_AXIS_LEFTY, -1};

    for (b = 0; b < MAX_BINDINGS && p->bindings[ACT_DOWN][b].type != BIND_NONE; b++);
    if (b < MAX_BINDINGS) p->bindings[ACT_DOWN][b] = (Binding){BIND_BTN, SDL_CONTROLLER_BUTTON_DPAD_DOWN, 0};
    if (b + 1 < MAX_BINDINGS) p->bindings[ACT_DOWN][b + 1] = (Binding){BIND_AXIS, SDL_CONTROLLER_AXIS_LEFTY, 1};

    for (b = 0; b < MAX_BINDINGS && p->bindings[ACT_LEFT][b].type != BIND_NONE; b++);
    if (b < MAX_BINDINGS) p->bindings[ACT_LEFT][b] = (Binding){BIND_BTN, SDL_CONTROLLER_BUTTON_DPAD_LEFT, 0};
    if (b + 1 < MAX_BINDINGS) p->bindings[ACT_LEFT][b + 1] = (Binding){BIND_AXIS, SDL_CONTROLLER_AXIS_LEFTX, -1};

    for (b = 0; b < MAX_BINDINGS && p->bindings[ACT_RIGHT][b].type != BIND_NONE; b++);
    if (b < MAX_BINDINGS) p->bindings[ACT_RIGHT][b] = (Binding){BIND_BTN, SDL_CONTROLLER_BUTTON_DPAD_RIGHT, 0};
    if (b + 1 < MAX_BINDINGS) p->bindings[ACT_RIGHT][b + 1] = (Binding){BIND_AXIS, SDL_CONTROLLER_AXIS_LEFTX, 1};

    for (b = 0; b < MAX_BINDINGS && p->bindings[ACT_STOP][b].type != BIND_NONE; b++);
    if (b < MAX_BINDINGS) p->bindings[ACT_STOP][b] = (Binding){BIND_BTN, SDL_CONTROLLER_BUTTON_B, 0};

    for (b = 0; b < MAX_BINDINGS && p->bindings[ACT_ACTION][b].type != BIND_NONE; b++);
    if (b < MAX_BINDINGS) p->bindings[ACT_ACTION][b] = (Binding){BIND_BTN, SDL_CONTROLLER_BUTTON_A, 0};

    for (b = 0; b < MAX_BINDINGS && p->bindings[ACT_CYCLE][b].type != BIND_NONE; b++);
    if (b < MAX_BINDINGS) p->bindings[ACT_CYCLE][b] = (Binding){BIND_BTN, SDL_CONTROLLER_BUTTON_X, 0};

    for (b = 0; b < MAX_BINDINGS && p->bindings[ACT_REMOTE][b].type != BIND_NONE; b++);
    if (b < MAX_BINDINGS) p->bindings[ACT_REMOTE][b] = (Binding){BIND_BTN, SDL_CONTROLLER_BUTTON_Y, 0};
}

void input_get_default(InputConfig* config) {
    memset(config, 0, sizeof(InputConfig));
    for (int i = 0; i < 4; i++) config->pad_id[i] = -1;

    // Player 1 Defaults
    config->p[0].bindings[ACT_UP][0] = (Binding){BIND_KEY, SDL_SCANCODE_UP, 0};
    config->p[0].bindings[ACT_DOWN][0] = (Binding){BIND_KEY, SDL_SCANCODE_DOWN, 0};
    config->p[0].bindings[ACT_LEFT][0] = (Binding){BIND_KEY, SDL_SCANCODE_LEFT, 0};
    config->p[0].bindings[ACT_RIGHT][0] = (Binding){BIND_KEY, SDL_SCANCODE_RIGHT, 0};
    config->p[0].bindings[ACT_STOP][0] = (Binding){BIND_KEY, SDL_SCANCODE_X, 0};
    config->p[0].bindings[ACT_ACTION][0] = (Binding){BIND_KEY, SDL_SCANCODE_Z, 0};
    config->p[0].bindings[ACT_CYCLE][0] = (Binding){BIND_KEY, SDL_SCANCODE_A, 0};
    config->p[0].bindings[ACT_REMOTE][0] = (Binding){BIND_KEY, SDL_SCANCODE_S, 0};
    config->p[0].bindings[ACT_GOD][0] = (Binding){BIND_KEY, SDL_SCANCODE_P, 0};
    config->p[0].bindings[ACT_GOD][1] = (Binding){BIND_BTN, SDL_CONTROLLER_BUTTON_RIGHTSHOULDER, 0};

    // Player 2 Defaults
    config->p[1].bindings[ACT_UP][0] = (Binding){BIND_KEY, SDL_SCANCODE_I, 0};
    config->p[1].bindings[ACT_DOWN][0] = (Binding){BIND_KEY, SDL_SCANCODE_K, 0};
    config->p[1].bindings[ACT_LEFT][0] = (Binding){BIND_KEY, SDL_SCANCODE_J, 0};
    config->p[1].bindings[ACT_RIGHT][0] = (Binding){BIND_KEY, SDL_SCANCODE_L, 0};
    config->p[1].bindings[ACT_STOP][0] = (Binding){BIND_KEY, SDL_SCANCODE_6, 0};
    config->p[1].bindings[ACT_ACTION][0] = (Binding){BIND_KEY, SDL_SCANCODE_5, 0};
    config->p[1].bindings[ACT_CYCLE][0] = (Binding){BIND_KEY, SDL_SCANCODE_7, 0};
    config->p[1].bindings[ACT_REMOTE][0] = (Binding){BIND_KEY, SDL_SCANCODE_8, 0};

    // Player 3 Defaults
    config->p[2].bindings[ACT_UP][0] = (Binding){BIND_KEY, SDL_SCANCODE_T, 0};
    config->p[2].bindings[ACT_DOWN][0] = (Binding){BIND_KEY, SDL_SCANCODE_G, 0};
    config->p[2].bindings[ACT_LEFT][0] = (Binding){BIND_KEY, SDL_SCANCODE_F, 0};
    config->p[2].bindings[ACT_RIGHT][0] = (Binding){BIND_KEY, SDL_SCANCODE_H, 0};
    config->p[2].bindings[ACT_STOP][0] = (Binding){BIND_KEY, SDL_SCANCODE_V, 0};
    config->p[2].bindings[ACT_ACTION][0] = (Binding){BIND_KEY, SDL_SCANCODE_C, 0};
    config->p[2].bindings[ACT_CYCLE][0] = (Binding){BIND_KEY, SDL_SCANCODE_B, 0};
    config->p[2].bindings[ACT_REMOTE][0] = (Binding){BIND_KEY, SDL_SCANCODE_N, 0};

    // Player 4 Defaults
    config->p[3].bindings[ACT_UP][0] = (Binding){BIND_KEY, SDL_SCANCODE_KP_8, 0};
    config->p[3].bindings[ACT_DOWN][0] = (Binding){BIND_KEY, SDL_SCANCODE_KP_5, 0};
    config->p[3].bindings[ACT_LEFT][0] = (Binding){BIND_KEY, SDL_SCANCODE_KP_4, 0};
    config->p[3].bindings[ACT_RIGHT][0] = (Binding){BIND_KEY, SDL_SCANCODE_KP_6, 0};
    config->p[3].bindings[ACT_STOP][0] = (Binding){BIND_KEY, SDL_SCANCODE_KP_2, 0};
    config->p[3].bindings[ACT_ACTION][0] = (Binding){BIND_KEY, SDL_SCANCODE_KP_1, 0};
    config->p[3].bindings[ACT_CYCLE][0] = (Binding){BIND_KEY, SDL_SCANCODE_KP_3, 0};
    config->p[3].bindings[ACT_REMOTE][0] = (Binding){BIND_KEY, SDL_SCANCODE_KP_0, 0};

    // Add default gamepad bindings for all players
    for (int i = 0; i < 4; i++) set_default_pad_bindings(&config->p[i]);
}

bool input_load_config(InputConfig* config, const char* filename) {
    FILE* f = fopen(filename, "r");
    if (!f) return false;

    memset(config, 0, sizeof(InputConfig));
    for (int i = 0; i < 4; i++) config->pad_id[i] = -1;
    char line[512];
    while (fgets(line, sizeof(line), f)) {
        if (line[0] == '#' || line[0] == '\n' || line[0] == '\r') continue;
        char key[64], val[256];
        if (sscanf(line, "%63[^=]=%255s", key, val) == 2) {
            int p_idx = -1;
            if (strncmp(key, "P1_", 3) == 0) p_idx = 0;
            else if (strncmp(key, "P2_", 3) == 0) p_idx = 1;
            else if (strncmp(key, "P3_", 3) == 0) p_idx = 2;
            else if (strncmp(key, "P4_", 3) == 0) p_idx = 3;

            if (p_idx != -1) {
                const char* act_name = key + 3;

                // Handle gamepad assignment: Px_PAD=<0-based index>
                if (strcmp(act_name, "PAD") == 0) {
                    int pad_index = atoi(val);
                    if (pad_index >= 0 && pad_index < 4)
                        config->pad_id[p_idx] = pad_index; // store index, resolved to instance ID later
                    continue;
                }

                int act_idx = -1;
                for (int i = 0; i < ACT_MAX_PLAYER; ++i) {
                    if (strcmp(act_name, action_names[i]) == 0) {
                        act_idx = i;
                        break;
                    }
                }

                if (act_idx != -1) {
                    char* token = strtok(val, ",");
                    int b_idx = 0;
                    while (token && b_idx < MAX_BINDINGS) {
                        Binding b = {BIND_NONE, 0, 0};
                        if (strncmp(token, "K:", 2) == 0) {
                            b.type = BIND_KEY;
                            b.id = atoi(token + 2);
                        } else if (strncmp(token, "B:", 2) == 0) {
                            b.type = BIND_BTN;
                            b.id = atoi(token + 2);
                        } else if (strncmp(token, "A:", 2) == 0) {
                            b.type = BIND_AXIS;
                            sscanf(token + 2, "%d:%d", &b.id, &b.extra);
                        }
                        if (b.type != BIND_NONE) {
                            config->p[p_idx].bindings[act_idx][b_idx++] = b;
                        }
                        token = strtok(NULL, ",");
                    }
                }
            }
        }
    }
    fclose(f);
    return true;
}

void input_save_config(const InputConfig* config, const char* filename) {
    FILE* f = fopen(filename, "w");
    if (!f) return;

    fprintf(f, "# MineBombers Input Configuration\n");
    fprintf(f, "# Format: P<1-4>_<ACTION>=K:<scancode>,B:<button>,A:<axis>:<dir>\n");
    fprintf(f, "# Gamepad assignment: P<1-4>_PAD=<0-3> (0=first pad, 1=second, etc.)\n\n");

    for (int p = 0; p < 4; ++p) {
        fprintf(f, "P%d_PAD=%d\n", p + 1, p);
        for (int i = 0; i < ACT_MAX_PLAYER; ++i) {
            fprintf(f, "P%d_%s=", p + 1, action_names[i]);
            bool first = true;
            for (int b = 0; b < MAX_BINDINGS; ++b) {
                Binding bind = config->p[p].bindings[i][b];
                if (bind.type == BIND_NONE) break;
                if (!first) fprintf(f, ",");
                if (bind.type == BIND_KEY) fprintf(f, "K:%d", bind.id);
                else if (bind.type == BIND_BTN) fprintf(f, "B:%d", bind.id);
                else if (bind.type == BIND_AXIS) fprintf(f, "A:%d:%d", bind.id, bind.extra);
                first = false;
            }
            fprintf(f, "\n");
        }
        fprintf(f, "\n");
    }
    fclose(f);
}

void input_print(const InputConfig* config) {
    printf("--- Input Mappings ---\n");
    for (int p = 0; p < 4; ++p) {
        printf("Player %d:\n", p + 1);
        for (int i = 0; i < ACT_MAX_PLAYER; ++i) {
            printf("  %-8s: ", action_names[i]);
            bool first = true;
            for (int b = 0; b < MAX_BINDINGS; ++b) {
                Binding bind = config->p[p].bindings[i][b];
                if (bind.type == BIND_NONE) break;
                if (!first) printf(", ");
                if (bind.type == BIND_KEY) printf("Key %d", bind.id);
                else if (bind.type == BIND_BTN) printf("Btn %d", bind.id);
                else if (bind.type == BIND_AXIS) printf("Axis %d(%d)", bind.id, bind.extra);
                first = false;
            }
            printf("\n");
        }
    }
    printf("----------------------\n");
    fflush(stdout);
}

void input_assign_pads(InputConfig* config, SDL_GameController** pads, int num_pads) {
    for (int p = 0; p < 4; p++) {
        int idx = (int)config->pad_id[p]; // stored as pad index during load, or -1
        if (idx >= 0 && idx < num_pads && pads[idx]) {
            SDL_Joystick* joy = SDL_GameControllerGetJoystick(pads[idx]);
            config->pad_id[p] = joy ? SDL_JoystickInstanceID(joy) : -1;
        } else if (idx == -1 && p < num_pads && pads[p]) {
            // Default: assign pad N to player N
            SDL_Joystick* joy = SDL_GameControllerGetJoystick(pads[p]);
            config->pad_id[p] = joy ? SDL_JoystickInstanceID(joy) : -1;
        } else {
            config->pad_id[p] = -1;
        }
    }
}

ActionType input_map_event(const SDL_Event* e, int p_idx, InputConfig* config) {
    PlayerInputConfig* pic = &config->p[p_idx];

    if (e->type == SDL_KEYDOWN) {
        for (int i = 0; i < ACT_MAX_PLAYER; ++i) {
            for (int b = 0; b < MAX_BINDINGS; ++b) {
                if (pic->bindings[i][b].type == BIND_NONE) break;
                if (pic->bindings[i][b].type == BIND_KEY && e->key.keysym.scancode == (SDL_Scancode)pic->bindings[i][b].id) return (ActionType)i;
            }
        }
    } else if (e->type == SDL_CONTROLLERBUTTONDOWN) {
        // Only match if this event came from this player's assigned pad
        if (config->pad_id[p_idx] < 0 || e->cbutton.which != config->pad_id[p_idx])
            return ACT_MAX_PLAYER;
        for (int i = 0; i < ACT_MAX_PLAYER; ++i) {
            for (int b = 0; b < MAX_BINDINGS; ++b) {
                if (pic->bindings[i][b].type == BIND_NONE) break;
                if (pic->bindings[i][b].type == BIND_BTN && e->cbutton.button == (SDL_GameControllerButton)pic->bindings[i][b].id) return (ActionType)i;
            }
        }
    } else if (e->type == SDL_CONTROLLERAXISMOTION) {
        if (config->pad_id[p_idx] < 0 || e->caxis.which != config->pad_id[p_idx])
            return ACT_MAX_PLAYER;
        int state = 0;
        if (e->caxis.value < -16000) state = -1;
        else if (e->caxis.value > 16000) state = 1;

        if (state != config->axis_state[p_idx][e->caxis.axis]) {
            config->axis_state[p_idx][e->caxis.axis] = state;
            if (state != 0) {
                for (int i = 0; i < ACT_MAX_PLAYER; ++i) {
                    for (int b = 0; b < MAX_BINDINGS; ++b) {
                        if (pic->bindings[i][b].type == BIND_NONE) break;
                        if (pic->bindings[i][b].type == BIND_AXIS && (SDL_GameControllerAxis)pic->bindings[i][b].id == e->caxis.axis && pic->bindings[i][b].extra == state) return (ActionType)i;
                    }
                }
            }
        }
    }

    return ACT_MAX_PLAYER;
}

bool input_action_held(InputConfig* config, int p_idx, ActionType action) {
    const Uint8* keys = SDL_GetKeyboardState(NULL);
    PlayerInputConfig* pic = &config->p[p_idx];
    for (int b = 0; b < MAX_BINDINGS; b++) {
        Binding* bind = &pic->bindings[action][b];
        if (bind->type == BIND_NONE) break;
        if (bind->type == BIND_KEY && keys[bind->id]) return true;
        if (bind->type == BIND_AXIS && config->axis_state[p_idx][bind->id] == bind->extra) return true;
        if (bind->type == BIND_BTN) {
            // Check if any controller has this button pressed for this player's pad
            SDL_JoystickID jid = config->pad_id[p_idx];
            if (jid >= 0) {
                SDL_GameController* gc = SDL_GameControllerFromInstanceID(jid);
                if (gc && SDL_GameControllerGetButton(gc, (SDL_GameControllerButton)bind->id)) return true;
            }
        }
    }
    return false;
}
