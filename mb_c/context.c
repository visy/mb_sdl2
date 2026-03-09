#include "context.h"
#include "error.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

static void build_path(const char* dir, const char* filename, char* out_path, size_t max_len) {
#ifdef _WIN32
    snprintf(out_path, max_len, "%s\\%s", dir, filename);
#else
    snprintf(out_path, max_len, "%s/%s", dir, filename);
#endif
}

void context_init(ApplicationContext* ctx, const char* game_dir) {
    memset(ctx, 0, sizeof(ApplicationContext));
    strncpy(ctx->game_dir, game_dir, MAX_PATH - 1);
    ctx->game_dir[MAX_PATH - 1] = '\0';

    printf("Initializing SDL...\n"); fflush(stdout);
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO) < 0) {
        report_sdl_error("Failed to initialize SDL");
    }

    ctx->music = (s3m_t*)calloc(1, sizeof(s3m_t));
    if (!ctx->music) {
        report_error("Failed to allocate memory for music player");
    }
    s3m_initialize(ctx->music, 44100);
    ctx->music_loaded = false;

    printf("Creating window...\n"); fflush(stdout);
    ctx->window = SDL_CreateWindow(
        "MineBombers Reloaded",
        SDL_WINDOWPOS_CENTERED,
        SDL_WINDOWPOS_CENTERED,
        640, 480,
        SDL_WINDOW_ALLOW_HIGHDPI | SDL_WINDOW_SHOWN
    );

    if (!ctx->window) {
        report_sdl_error("Failed to create window");
    }

    ctx->renderer = SDL_CreateRenderer(ctx->window, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_TARGETTEXTURE);
    if (!ctx->renderer) {
        report_sdl_error("Failed to create renderer");
    }

    ctx->buffer = SDL_CreateTexture(
        ctx->renderer,
        SDL_PIXELFORMAT_RGBA8888,
        SDL_TEXTUREACCESS_TARGET,
        640, 480
    );

    printf("Opening audio device...\n"); fflush(stdout);
    if (Mix_OpenAudio(44100, AUDIO_S16LSB, 2, 2048) < 0) {
        report_sdl_error("Failed to open audio");
    }
    
    int freq; uint16_t format; int channels;
    if (Mix_QuerySpec(&freq, &format, &channels)) {
        printf("Audio spec: %dHz, fmt %04x, %d chn\n", freq, format, channels);
    }
    fflush(stdout);
}

void context_destroy(ApplicationContext* ctx) {
    Mix_HookMusic(NULL, NULL);
    if (ctx->music) {
        if (ctx->music_loaded) {
            s3m_stop(ctx->music);
            s3m_unload(ctx->music);
        }
        free(ctx->music);
    }
    if (ctx->buffer) SDL_DestroyTexture(ctx->buffer);
    if (ctx->renderer) SDL_DestroyRenderer(ctx->renderer);
    if (ctx->window) SDL_DestroyWindow(ctx->window);
    Mix_CloseAudio();
    SDL_Quit();
}

bool context_play_music(ApplicationContext* ctx, const char* filename) {
    if (!ctx->music) return false;
    Mix_HookMusic(NULL, NULL);
    if (ctx->music_loaded) {
        s3m_stop(ctx->music);
        s3m_unload(ctx->music);
        ctx->music_loaded = false;
    }

    char path[MAX_PATH];
    build_path(ctx->game_dir, filename, path, sizeof(path));
    printf("Loading music: %s\n", filename); fflush(stdout);

    if (s3m_load(ctx->music, path) == 0) {
        if (ctx->music->header->mastervol == 0) ctx->music->header->mastervol = 64;
        s3m_play(ctx->music);
        ctx->music_loaded = true;
        Mix_HookMusic(s3m_sound_callback, ctx->music);
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

void context_wait_frame(ApplicationContext* ctx) {
    (void)ctx;
    SDL_Delay(16);
}

bool context_poll_events(ApplicationContext* ctx) {
    (void)ctx;
    SDL_Event e;
    while (SDL_PollEvent(&e)) {
        if (e.type == SDL_QUIT) return false;
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

void context_render_texture(ApplicationContext* ctx, SDL_Texture* texture) {
    SDL_SetRenderTarget(ctx->renderer, ctx->buffer);
    SDL_RenderCopy(ctx->renderer, texture, NULL, NULL);
    SDL_SetRenderTarget(ctx->renderer, NULL);
}

void context_animate(ApplicationContext* ctx, Animation animation, int steps) {
    int total = (steps + 1) * 4;
    for (int i = 0; i <= total; ++i) {
        SDL_SetRenderDrawColor(ctx->renderer, 0, 0, 0, 255);
        SDL_RenderClear(ctx->renderer);
        int alpha = (255 * i) / total;
        if (animation == ANIMATION_FADE_DOWN) alpha = 255 - alpha;
        SDL_SetTextureBlendMode(ctx->buffer, SDL_BLENDMODE_BLEND);
        SDL_SetTextureAlphaMod(ctx->buffer, (Uint8)alpha);
        SDL_RenderCopy(ctx->renderer, ctx->buffer, NULL, NULL);
        SDL_RenderPresent(ctx->renderer);
        SDL_Delay(16);
    }
}

void context_present(ApplicationContext* ctx) {
    SDL_SetTextureBlendMode(ctx->buffer, SDL_BLENDMODE_NONE);
    SDL_SetTextureAlphaMod(ctx->buffer, 255);
    SDL_RenderCopy(ctx->renderer, ctx->buffer, NULL, NULL);
    SDL_RenderPresent(ctx->renderer);
}

SDL_Scancode context_wait_key_pressed(ApplicationContext* ctx) {
    (void)ctx;
    SDL_Event e;
    while (true) {
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_QUIT) return SDL_SCANCODE_ESCAPE;
            if (e.type == SDL_KEYDOWN && !e.key.repeat) return e.key.keysym.scancode;
        }
        SDL_Delay(1);
    }
}
