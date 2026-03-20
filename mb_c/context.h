#ifndef CONTEXT_H
#define CONTEXT_H

#include "args.h"
#include "images.h"
#include <stdbool.h>
#include <SDL2/SDL.h>
#include <SDL2/SDL_mixer.h>
#include "s3m.h"

#define SCREEN_WIDTH 640
#define SCREEN_HEIGHT 480

typedef enum {
    ANIMATION_FADE_UP,
    ANIMATION_FADE_DOWN
} Animation;

typedef struct {
    char game_dir[MAX_PATH];
    SDL_Window* window;
    SDL_Renderer* renderer;
    SDL_Texture* buffer;
    SDL_Rect viewport;          // centered scaled game area within fullscreen
    s3m_t* music;
    bool music_loaded;
    char current_music[64];
    SDL_GameController* pads[4];
    int num_pads;
} ApplicationContext;


void context_init(ApplicationContext* ctx, const char* game_dir, bool windowed);
void context_toggle_fullscreen(ApplicationContext* ctx);
void context_destroy(ApplicationContext* ctx);
void context_wait_frame(ApplicationContext* ctx);
bool context_poll_events(ApplicationContext* ctx);
bool context_load_spy(ApplicationContext* ctx, const char* filename, TexturePalette* out_palette);
bool context_load_ppm(ApplicationContext* ctx, const char* filename, TexturePalette* out_palette);

// Animation and rendering
void context_animate(ApplicationContext* ctx, Animation animation, int steps);
void context_present(ApplicationContext* ctx);
void context_render_texture(ApplicationContext* ctx, SDL_Texture* texture);

// Input
SDL_Scancode context_wait_key_pressed(ApplicationContext* ctx);

// Music
bool context_play_music(ApplicationContext* ctx, const char* filename);
bool context_play_music_at(ApplicationContext* ctx, const char* filename, int order_idx);
void context_linger_music_start(ApplicationContext* ctx);
void context_linger_music_end(ApplicationContext* ctx);
void context_stop_music(ApplicationContext* ctx);

// Sound Effects
Mix_Chunk* context_load_sample(ApplicationContext* ctx, const char* filename);
void context_play_sample(Mix_Chunk* sample);
void context_play_sample_freq(Mix_Chunk* sample, int frequency);

#endif // CONTEXT_H
