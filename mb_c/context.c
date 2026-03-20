#include "context.h"
#include "error.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

// Track resampled chunks per channel so we can free them when done
static Mix_Chunk* channel_resampled[32] = {0};

static void channel_finished_callback(int channel) {
    if (channel >= 0 && channel < 32 && channel_resampled[channel]) {
        free(channel_resampled[channel]->abuf);
        free(channel_resampled[channel]);
        channel_resampled[channel] = NULL;
    }
}

static void build_path(const char* dir, const char* filename, char* out, size_t size) {
#ifdef _WIN32
    snprintf(out, size, "%s\\%s", dir, filename);
#else
    snprintf(out, size, "%s/%s", dir, filename);
#endif
}

static void compute_viewport(ApplicationContext* ctx) {
    int out_w, out_h;
    SDL_GetRendererOutputSize(ctx->renderer, &out_w, &out_h);
    int scale_x = out_w / SCREEN_WIDTH;
    int scale_y = out_h / SCREEN_HEIGHT;
    int scale = (scale_x < scale_y) ? scale_x : scale_y;
    if (scale < 1) scale = 1;
    int vw = SCREEN_WIDTH * scale;
    int vh = SCREEN_HEIGHT * scale;
    ctx->viewport.x = (out_w - vw) / 2;
    ctx->viewport.y = (out_h - vh) / 2;
    ctx->viewport.w = vw;
    ctx->viewport.h = vh;
}

void context_toggle_fullscreen(ApplicationContext* ctx) {
    Uint32 flags = SDL_GetWindowFlags(ctx->window);
    if (flags & SDL_WINDOW_FULLSCREEN_DESKTOP) {
        SDL_SetWindowFullscreen(ctx->window, 0);
        SDL_SetWindowSize(ctx->window, SCREEN_WIDTH * 2, SCREEN_HEIGHT * 2);
        SDL_SetWindowPosition(ctx->window, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED);
    } else {
        SDL_SetWindowFullscreen(ctx->window, SDL_WINDOW_FULLSCREEN_DESKTOP);
    }
    compute_viewport(ctx);
}

void context_init(ApplicationContext* ctx, const char* game_dir, bool windowed) {
    memset(ctx, 0, sizeof(ApplicationContext));
    strncpy(ctx->game_dir, game_dir, MAX_PATH - 1);

    printf("Initializing SDL...\n"); fflush(stdout);
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_GAMECONTROLLER | SDL_INIT_JOYSTICK) < 0) {
        report_sdl_error("Failed to initialize SDL");
    }

    if (windowed) {
        ctx->window = SDL_CreateWindow("Mine Bombers", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
            SCREEN_WIDTH * 2, SCREEN_HEIGHT * 2, SDL_WINDOW_SHOWN);
    } else {
        ctx->window = SDL_CreateWindow("Mine Bombers", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
            0, 0, SDL_WINDOW_FULLSCREEN_DESKTOP | SDL_WINDOW_SHOWN);
    }
    if (!ctx->window) report_sdl_error("Failed to create window");

    ctx->renderer = SDL_CreateRenderer(ctx->window, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_TARGETTEXTURE);
    if (!ctx->renderer) report_sdl_error("Failed to create renderer");

    compute_viewport(ctx);

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
    Mix_AllocateChannels(32);
    Mix_Volume(-1, 24);
    Mix_ChannelFinished(channel_finished_callback);
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
        if (e.type == SDL_KEYDOWN && e.key.keysym.scancode == SDL_SCANCODE_RETURN
            && (e.key.keysym.mod & KMOD_ALT)) {
            context_toggle_fullscreen(ctx);
            continue;
        }
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
        SDL_RenderCopy(ctx->renderer, ctx->buffer, NULL, &ctx->viewport);


        SDL_RenderPresent(ctx->renderer);
        SDL_Delay(16);
    }
    SDL_SetTextureAlphaMod(ctx->buffer, 255);
}

void context_present(ApplicationContext* ctx) {
    SDL_SetRenderTarget(ctx->renderer, NULL);
    SDL_SetRenderDrawColor(ctx->renderer, 0, 0, 0, 255);
    SDL_RenderClear(ctx->renderer);

    // Render game buffer scaled to centered viewport
    SDL_RenderCopy(ctx->renderer, ctx->buffer, NULL, &ctx->viewport);


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
            if (e.type == SDL_KEYDOWN && !e.key.repeat) {
                if (e.key.keysym.scancode == SDL_SCANCODE_RETURN && (e.key.keysym.mod & KMOD_ALT)) {
                    context_toggle_fullscreen(ctx);
                    context_present(ctx);
                    continue;
                }
                return e.key.keysym.scancode;
            }
            
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

void context_linger_music_start(ApplicationContext* ctx) {
    if (!ctx->music || !ctx->music_loaded) return;
    ctx->music->rt.sample_per_frame = 0x7FFFFFFF;
    ctx->music->rt.sample_ctr = 0x7FFFFFFF;
}

void context_linger_music_end(ApplicationContext* ctx) {
    if (!ctx->music || !ctx->music_loaded) return;
    s3m_stop(ctx->music);
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
    
    FILE* f = fopen(path, "rb");
    if (!f) return NULL;

    fseek(f, 0, SEEK_END);
    uint32_t pcm_len = (uint32_t)ftell(f);
    fseek(f, 0, SEEK_SET);

    if (pcm_len == 0) {
        fclose(f);
        return NULL;
    }

    // Create a WAV header in memory to wrap the raw 8-bit mono 11025Hz data
    uint32_t total_len = 44 + pcm_len;
    uint8_t* wav_buf = malloc(total_len);
    if (!wav_buf) {
        fclose(f);
        return NULL;
    }

    uint32_t freq = 11025;
    uint16_t channels = 1;
    uint16_t bits = 8;
    uint32_t byte_rate = freq * channels * bits / 8;
    uint16_t block_align = channels * bits / 8;

    memcpy(wav_buf + 0, "RIFF", 4);
    uint32_t chunk_size = total_len - 8;
    memcpy(wav_buf + 4, &chunk_size, 4);
    memcpy(wav_buf + 8, "WAVE", 4);
    memcpy(wav_buf + 12, "fmt ", 4);
    uint32_t fmt_size = 16;
    memcpy(wav_buf + 16, &fmt_size, 4);
    uint16_t format_tag = 1; // PCM
    memcpy(wav_buf + 20, &format_tag, 2);
    memcpy(wav_buf + 22, &channels, 2);
    memcpy(wav_buf + 24, &freq, 4);
    memcpy(wav_buf + 28, &byte_rate, 4);
    memcpy(wav_buf + 32, &block_align, 2);
    memcpy(wav_buf + 34, &bits, 2);
    memcpy(wav_buf + 36, "data", 4);
    memcpy(wav_buf + 40, &pcm_len, 4);

    fread(wav_buf + 44, 1, pcm_len, f);
    fclose(f);

    // Let SDL_mixer load it and manage the memory
    SDL_RWops* rw = SDL_RWFromMem(wav_buf, total_len);
    Mix_Chunk* chunk = Mix_LoadWAV_RW(rw, 1); // 1 = auto-close RW
    
    // We can free our temporary wav_buf because Mix_LoadWAV_RW copies the data
    free(wav_buf);

    return chunk;
}

// Limit how many channels can play the same sample simultaneously
// to avoid volume stacking/clipping (SDL_mixer adds channels together)
#define MAX_SAME_SAMPLE_CHANNELS 3

static int count_playing_sample(Mix_Chunk* sample) {
    int count = 0;
    for (int ch = 0; ch < 32; ch++) {
        if (Mix_Playing(ch) && Mix_GetChunk(ch) == sample) count++;
    }
    return count;
}

void context_play_sample(Mix_Chunk* sample) {
    if (sample && count_playing_sample(sample) < MAX_SAME_SAMPLE_CHANNELS)
        Mix_PlayChannel(-1, sample, 0);
}

void context_play_sample_freq(Mix_Chunk* sample, int frequency) {
    if (!sample) return;
    if (count_playing_sample(sample) >= MAX_SAME_SAMPLE_CHANNELS) return;
    // The samples are stored as 11025Hz WAV, converted to mixer format by SDL_mixer.
    // To simulate frequency change, we resample: ratio = frequency / 11025.0
    // ratio > 1 = higher pitch (shorter), ratio < 1 = lower pitch (longer)
    if (frequency <= 0) frequency = 11025;
    double ratio = (double)frequency / 11025.0;

    // For near-normal frequency, skip resampling
    if (ratio > 0.95 && ratio < 1.05) {
        Mix_PlayChannel(-1, sample, 0);
        return;
    }

    // Query mixer format to know sample size
    int mixer_freq, mixer_channels;
    Uint16 mixer_format;
    Mix_QuerySpec(&mixer_freq, &mixer_format, &mixer_channels);
    int bytes_per_sample = (mixer_format & 0xFF) / 8 * mixer_channels;
    if (bytes_per_sample == 0) bytes_per_sample = 4; // fallback: 16-bit stereo

    int src_samples = sample->alen / bytes_per_sample;
    int dst_samples = (int)(src_samples / ratio);
    if (dst_samples <= 0) return;

    Uint32 dst_len = dst_samples * bytes_per_sample;
    Uint8* dst_buf = malloc(dst_len);
    if (!dst_buf) return;

    // Nearest-neighbor resample
    for (int i = 0; i < dst_samples; i++) {
        int src_idx = (int)(i * ratio);
        if (src_idx >= src_samples) src_idx = src_samples - 1;
        memcpy(dst_buf + i * bytes_per_sample,
               sample->abuf + src_idx * bytes_per_sample,
               bytes_per_sample);
    }

    Mix_Chunk* resampled = malloc(sizeof(Mix_Chunk));
    if (!resampled) { free(dst_buf); return; }
    resampled->allocated = 1;
    resampled->abuf = dst_buf;
    resampled->alen = dst_len;
    resampled->volume = sample->volume;

    int channel = Mix_PlayChannel(-1, resampled, 0);
    if (channel == -1) {
        free(dst_buf);
        free(resampled);
    } else if (channel >= 0 && channel < 32) {
        // Free any previous resampled chunk on this channel
        if (channel_resampled[channel]) {
            free(channel_resampled[channel]->abuf);
            free(channel_resampled[channel]);
        }
        channel_resampled[channel] = resampled;
    }
}
