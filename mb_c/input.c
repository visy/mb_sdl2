#include "input.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdbool.h>

static const char* action_names[] = {
    "UP", "DOWN", "LEFT", "RIGHT",
    "STOP", "ACTION", "CYCLE", "REMOTE", "GOD"
};

void input_get_default(InputConfig* config) {
    memset(config, 0, sizeof(InputConfig));

    // Player 1 Defaults
    config->p[0].bindings[ACT_UP][0] = (Binding){BIND_KEY, SDL_SCANCODE_UP, 0};
    config->p[0].bindings[ACT_UP][1] = (Binding){BIND_BTN, SDL_CONTROLLER_BUTTON_DPAD_UP, 0};
    config->p[0].bindings[ACT_UP][2] = (Binding){BIND_AXIS, SDL_CONTROLLER_AXIS_LEFTY, -1};

    config->p[0].bindings[ACT_DOWN][0] = (Binding){BIND_KEY, SDL_SCANCODE_DOWN, 0};
    config->p[0].bindings[ACT_DOWN][1] = (Binding){BIND_BTN, SDL_CONTROLLER_BUTTON_DPAD_DOWN, 0};
    config->p[0].bindings[ACT_DOWN][2] = (Binding){BIND_AXIS, SDL_CONTROLLER_AXIS_LEFTY, 1};

    config->p[0].bindings[ACT_LEFT][0] = (Binding){BIND_KEY, SDL_SCANCODE_LEFT, 0};
    config->p[0].bindings[ACT_LEFT][1] = (Binding){BIND_BTN, SDL_CONTROLLER_BUTTON_DPAD_LEFT, 0};
    config->p[0].bindings[ACT_LEFT][2] = (Binding){BIND_AXIS, SDL_CONTROLLER_AXIS_LEFTX, -1};

    config->p[0].bindings[ACT_RIGHT][0] = (Binding){BIND_KEY, SDL_SCANCODE_RIGHT, 0};
    config->p[0].bindings[ACT_RIGHT][1] = (Binding){BIND_BTN, SDL_CONTROLLER_BUTTON_DPAD_RIGHT, 0};
    config->p[0].bindings[ACT_RIGHT][2] = (Binding){BIND_AXIS, SDL_CONTROLLER_AXIS_LEFTX, 1};

    config->p[0].bindings[ACT_STOP][0] = (Binding){BIND_KEY, SDL_SCANCODE_X, 0};
    config->p[0].bindings[ACT_STOP][1] = (Binding){BIND_BTN, SDL_CONTROLLER_BUTTON_B, 0};

    config->p[0].bindings[ACT_ACTION][0] = (Binding){BIND_KEY, SDL_SCANCODE_Z, 0};
    config->p[0].bindings[ACT_ACTION][1] = (Binding){BIND_BTN, SDL_CONTROLLER_BUTTON_A, 0};

    config->p[0].bindings[ACT_CYCLE][0] = (Binding){BIND_KEY, SDL_SCANCODE_A, 0};
    config->p[0].bindings[ACT_CYCLE][1] = (Binding){BIND_BTN, SDL_CONTROLLER_BUTTON_X, 0};

    config->p[0].bindings[ACT_REMOTE][0] = (Binding){BIND_KEY, SDL_SCANCODE_S, 0};
    config->p[0].bindings[ACT_REMOTE][1] = (Binding){BIND_BTN, SDL_CONTROLLER_BUTTON_Y, 0};

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
}

bool input_load_config(InputConfig* config, const char* filename) {
    FILE* f = fopen(filename, "r");
    if (!f) return false;

    memset(config, 0, sizeof(InputConfig));
    char line[512];
    while (fgets(line, sizeof(line), f)) {
        if (line[0] == '#' || line[0] == '\n' || line[0] == '\r') continue;
        char key[64], val[256];
        if (sscanf(line, "%63[^=]=%255s", key, val) == 2) {
            int p_idx = -1;
            if (strncmp(key, "P1_", 3) == 0) p_idx = 0;
            else if (strncmp(key, "P2_", 3) == 0) p_idx = 1;

            if (p_idx != -1) {
                const char* act_name = key + 3;
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
    fprintf(f, "# Format: P<1|2>_<ACTION>=K:<scancode>,B:<button>,A:<axis>:<dir>\n\n");

    for (int p = 0; p < 2; ++p) {
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
    for (int p = 0; p < 2; ++p) {
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
        for (int i = 0; i < ACT_MAX_PLAYER; ++i) {
            for (int b = 0; b < MAX_BINDINGS; ++b) {
                if (pic->bindings[i][b].type == BIND_NONE) break;
                if (pic->bindings[i][b].type == BIND_BTN && e->cbutton.button == (SDL_GameControllerButton)pic->bindings[i][b].id) return (ActionType)i;
            }
        }
    } else if (e->type == SDL_CONTROLLERAXISMOTION) {
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
