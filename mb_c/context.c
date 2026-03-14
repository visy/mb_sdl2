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
        1280, 960,
        SDL_WINDOW_ALLOW_HIGHDPI | SDL_WINDOW_SHOWN
    );

    if (!ctx->window) report_sdl_error("Failed to create window");

    ctx->renderer = SDL_CreateRenderer(ctx->window, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_TARGETTEXTURE);
    if (!ctx->renderer) report_sdl_error("Failed to create renderer");

    ctx->buffer = SDL_CreateTexture(ctx->renderer, SDL_PIXELFORMAT_RGBA8888, SDL_TEXTUREACCESS_TARGET, 640, 480);

    printf("Opening audio device (44100Hz, S16LSB)...\n"); fflush(stdout);
    if (Mix_OpenAudio(44100, AUDIO_S16LSB, 2, 2048) < 0) {
        printf("Mix_OpenAudio Error: %s\n", Mix_GetError());
        report_sdl_error("Failed to open audio");
    }
    Mix_AllocateChannels(32);
    Mix_Volume(-1,16);
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
    return context_play_music_at(ctx, filename, 0);
}

bool context_play_music_at(ApplicationContext* ctx, const char* filename, int order_idx) {
    if (!ctx->music) return false;
    
    Mix_HookMusic(NULL, NULL);
    if (ctx->music_loaded) {
        s3m_stop(ctx->music);
        s3m_unload(ctx->music);
        ctx->music_loaded = false;
    }

    char path[MAX_PATH];
    build_path(ctx->game_dir, filename, path, sizeof(path));
    
    printf("Loading music: %s (at order %d) from path: %s\n", filename, order_idx, path); fflush(stdout);

    if (s3m_load(ctx->music, path) == 0) {
        if (ctx->music->header->master_vol == 0) ctx->music->header->master_vol = 64;
        s3m_play_at(ctx->music, order_idx);
        ctx->music_loaded = true;
        strncpy(ctx->current_music, filename, 63);
        Mix_HookMusic(s3m_sound_callback, ctx->music);
        return true;
    } else {
        printf("FAILED to load S3M: %s\n", path); fflush(stdout);
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
        ctx->current_music[0] = '\0';
    }
}

Mix_Chunk* context_load_sample(ApplicationContext* ctx, const char* filename) {
    char path[MAX_PATH];
    build_path(ctx->game_dir, filename, path, sizeof(path));
    
    FILE* f = fopen(path, "rb");
    if (!f) return NULL;

    fseek(f, 0, SEEK_END);
    uint32_t pcm_len = (uint32_t)ftell(f);
    fseek(f, 0, SEEK_SET);

    if (pcm_len == 0) {
        fclose(f);
        return NULL;
    }

    uint8_t* raw_data = malloc(pcm_len);
    fread(raw_data, 1, pcm_len, f);
    fclose(f);

    SDL_AudioCVT cvt;
    if (SDL_BuildAudioCVT(&cvt, AUDIO_U8, 1, 11025, AUDIO_S16LSB, 2, 44100) < 0) {
        free(raw_data);
        return NULL;
    }

    cvt.len = pcm_len;
    cvt.buf = malloc(pcm_len * cvt.len_mult);
    memcpy(cvt.buf, raw_data, pcm_len);
    free(raw_data);

    if (SDL_ConvertAudio(&cvt) < 0) {
        free(cvt.buf);
        return NULL;
    }

    Mix_Chunk* chunk = Mix_QuickLoad_RAW(cvt.buf, cvt.len_cvt);
    return chunk;
}

void context_play_sample(Mix_Chunk* sample) {
    if (sample) Mix_PlayChannel(-1, sample, 0);
}

void context_play_sample_freq(Mix_Chunk* sample, int frequency) {
    (void)frequency;
    if (sample) Mix_PlayChannel(-1, sample, 0);
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
