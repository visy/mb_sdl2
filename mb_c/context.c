#include "context.h"
#include "error.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

static void build_path(const char* dir, const char* filename, char* out, size_t size) {
#ifdef _WIN32
    snprintf(out, size, "%s\\%s", dir, filename);
#else
    snprintf(out, size, "%s/%s", dir, filename);
#endif
}

void context_init(ApplicationContext* ctx, const char* game_dir) {
    memset(ctx, 0, sizeof(ApplicationContext));
    strncpy(ctx->game_dir, game_dir, MAX_PATH - 1);

    printf("Initializing SDL...\n"); fflush(stdout);
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_GAMECONTROLLER | SDL_INIT_JOYSTICK) < 0) {
        report_sdl_error("Failed to initialize SDL");
    }

    ctx->window = SDL_CreateWindow("Mine Bombers", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, 1280, 960, SDL_WINDOW_SHOWN);
    if (!ctx->window) report_sdl_error("Failed to create window");

    ctx->renderer = SDL_CreateRenderer(ctx->window, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_TARGETTEXTURE);
    if (!ctx->renderer) report_sdl_error("Failed to create renderer");

    SDL_RenderSetLogicalSize(ctx->renderer, 640, 480);

    ctx->buffer = SDL_CreateTexture(ctx->renderer, SDL_PIXELFORMAT_RGBA8888, SDL_TEXTUREACCESS_TARGET, 640, 480);
    
    ctx->pad = NULL;
    for (int i = 0; i < SDL_NumJoysticks(); ++i) {
        if (SDL_IsGameController(i)) {
            ctx->pad = SDL_GameControllerOpen(i);
            if (ctx->pad) {
                printf("Gamepad detected: %s\n", SDL_GameControllerName(ctx->pad));
                break;
            }
        }
    }
    if (!ctx->pad) printf("No gamepad detected at startup.\n");
    fflush(stdout);

    printf("Opening audio device (44100Hz, S16LSB)...\n"); fflush(stdout);
    if (Mix_OpenAudio(44100, AUDIO_S16LSB, 2, 2048) < 0) {
        printf("Mix_OpenAudio Error: %s\n", Mix_GetError());
        report_sdl_error("Failed to open audio");
    }
}

void context_destroy(ApplicationContext* ctx) {
    if (ctx->music_loaded) {
        s3m_stop(ctx->music);
        s3m_unload(ctx->music);
    }
    if (ctx->music) {
        free(ctx->music);
    }
    if (ctx->buffer) SDL_DestroyTexture(ctx->buffer);
    if (ctx->renderer) SDL_DestroyRenderer(ctx->renderer);
    if (ctx->window) SDL_DestroyWindow(ctx->window);
    if (ctx->pad) SDL_GameControllerClose(ctx->pad);
    Mix_CloseAudio();
    SDL_Quit();
}

void context_wait_frame(ApplicationContext* ctx) {
    (void)ctx;
    SDL_Delay(16);
}

bool context_poll_events(ApplicationContext* ctx) {
    SDL_Event e;
    while (SDL_PollEvent(&e)) {
        if (e.type == SDL_QUIT) return false;
        if (e.type == SDL_CONTROLLERDEVICEADDED && !ctx->pad) {
            if (SDL_IsGameController(e.cdevice.which)) {
                ctx->pad = SDL_GameControllerOpen(e.cdevice.which);
            }
        } else if (e.type == SDL_CONTROLLERDEVICEREMOVED && ctx->pad) {
            SDL_Joystick* joy = SDL_GameControllerGetJoystick(ctx->pad);
            if (joy && SDL_JoystickInstanceID(joy) == e.cdevice.which) {
                SDL_GameControllerClose(ctx->pad);
                ctx->pad = NULL;
            }
        }
    }
    return true;
}

bool context_load_spy(ApplicationContext* ctx, const char* filename, TexturePalette* out_palette) {
    char path[MAX_PATH];
    build_path(ctx->game_dir, filename, path, sizeof(path));
    return load_texture(ctx->renderer, path, TEXTURE_FORMAT_SPY, out_palette);
}

bool context_load_ppm(ApplicationContext* ctx, const char* filename, TexturePalette* out_palette) {
    char path[MAX_PATH];
    build_path(ctx->game_dir, filename, path, sizeof(path));
    return load_texture(ctx->renderer, path, TEXTURE_FORMAT_PPM, out_palette); 
}

void context_animate(ApplicationContext* ctx, Animation animation, int steps) {
    int total_frames = (steps + 1) * 4;
    for (int i = 0; i <= total_frames; i++) {
        SDL_Event e;
        while (SDL_PollEvent(&e)) { if (e.type == SDL_QUIT) exit(0); }

        int alpha = (255 * i) / total_frames;
        if (animation == ANIMATION_FADE_DOWN) alpha = 255 - alpha;

        SDL_SetRenderTarget(ctx->renderer, NULL);
        SDL_SetRenderDrawColor(ctx->renderer, 0, 0, 0, 255);
        SDL_RenderClear(ctx->renderer);

        SDL_SetTextureBlendMode(ctx->buffer, SDL_BLENDMODE_BLEND);
        SDL_SetTextureAlphaMod(ctx->buffer, (Uint8)alpha);
        SDL_RenderCopy(ctx->renderer, ctx->buffer, NULL, NULL);
        SDL_RenderPresent(ctx->renderer);
        SDL_Delay(16);
    }
    SDL_SetTextureAlphaMod(ctx->buffer, 255);
}

void context_present(ApplicationContext* ctx) {
    SDL_SetRenderTarget(ctx->renderer, NULL);
    SDL_RenderCopy(ctx->renderer, ctx->buffer, NULL, NULL);
    SDL_RenderPresent(ctx->renderer);
}

void context_render_texture(ApplicationContext* ctx, SDL_Texture* texture) {
    SDL_SetRenderTarget(ctx->renderer, ctx->buffer);
    SDL_RenderCopy(ctx->renderer, texture, NULL, NULL);
    SDL_SetRenderTarget(ctx->renderer, NULL);
}

SDL_Scancode context_wait_key_pressed(ApplicationContext* ctx) {
    SDL_Event e;
    while (true) {
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_QUIT) return SDL_SCANCODE_ESCAPE;
            if (e.type == SDL_KEYDOWN && !e.key.repeat) return e.key.keysym.scancode;
            
            if (e.type == SDL_CONTROLLERDEVICEADDED && !ctx->pad) {
                if (SDL_IsGameController(e.cdevice.which)) {
                    ctx->pad = SDL_GameControllerOpen(e.cdevice.which);
                }
            } else if (e.type == SDL_CONTROLLERDEVICEREMOVED && ctx->pad) {
                SDL_Joystick* joy = SDL_GameControllerGetJoystick(ctx->pad);
                if (joy && SDL_JoystickInstanceID(joy) == e.cdevice.which) {
                    SDL_GameControllerClose(ctx->pad);
                    ctx->pad = NULL;
                }
            }
            
            if (e.type == SDL_CONTROLLERBUTTONDOWN) {
                switch (e.cbutton.button) {
                    case SDL_CONTROLLER_BUTTON_DPAD_UP: return SDL_SCANCODE_UP;
                    case SDL_CONTROLLER_BUTTON_DPAD_DOWN: return SDL_SCANCODE_DOWN;
                    case SDL_CONTROLLER_BUTTON_DPAD_LEFT: return SDL_SCANCODE_LEFT;
                    case SDL_CONTROLLER_BUTTON_DPAD_RIGHT: return SDL_SCANCODE_RIGHT;
                    case SDL_CONTROLLER_BUTTON_A: return SDL_SCANCODE_Z;
                    case SDL_CONTROLLER_BUTTON_B: return SDL_SCANCODE_X;
                    case SDL_CONTROLLER_BUTTON_X: return SDL_SCANCODE_A;
                    case SDL_CONTROLLER_BUTTON_Y: return SDL_SCANCODE_S;
                    case SDL_CONTROLLER_BUTTON_START: return SDL_SCANCODE_RETURN;
                    case SDL_CONTROLLER_BUTTON_BACK: return SDL_SCANCODE_ESCAPE;
                }
            }
        }
        SDL_Delay(1);
    }
}

bool context_play_music(ApplicationContext* ctx, const char* filename) {
    return context_play_music_at(ctx, filename, 0);
}

bool context_play_music_at(ApplicationContext* ctx, const char* filename, int order_idx) {
    if (!ctx->music) {
        ctx->music = (s3m_t*)malloc(sizeof(s3m_t));
        s3m_initialize(ctx->music, 44100);
    }
    
    Mix_HookMusic(NULL, NULL);
    if (ctx->music_loaded) {
        s3m_stop(ctx->music);
        s3m_unload(ctx->music);
        ctx->music_loaded = false;
    }

    char path[MAX_PATH];
    build_path(ctx->game_dir, filename, path, sizeof(path));
    
    if (s3m_load(ctx->music, path) == 0) {
        if (ctx->music->header->master_vol == 0) ctx->music->header->master_vol = 64;
        s3m_play_at(ctx->music, order_idx);
        ctx->music_loaded = true;
        strncpy(ctx->current_music, filename, 63);
        Mix_HookMusic((void(*)(void*, uint8_t*, int))s3m_sound_callback, ctx->music);
        return true;
    }
    return false;
}

void context_stop_music(ApplicationContext* ctx) {
    if (!ctx->music) return;
    Mix_HookMusic(NULL, NULL);
    if (ctx->music_loaded) {
        s3m_stop(ctx->music);
        s3m_unload(ctx->music);
        ctx->music_loaded = false;
    }
}

Mix_Chunk* context_load_sample(ApplicationContext* ctx, const char* filename) {
    char path[MAX_PATH];
    build_path(ctx->game_dir, filename, path, sizeof(path));
    return Mix_LoadWAV(path);
}

void context_play_sample(Mix_Chunk* sample) {
    if (sample) Mix_PlayChannel(-1, sample, 0);
}

void context_play_sample_freq(Mix_Chunk* sample, int frequency) {
    if (sample) {
        int channel = Mix_PlayChannel(-1, sample, 0);
        if (channel != -1) {
            Mix_SetPanning(channel, 255, 255);
        }
    }
    (void)frequency; 
}
