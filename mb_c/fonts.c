#include "fonts.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static bool decode_font(const uint8_t* data, size_t size, uint8_t** out_image) {
    if (size != 256 * 8) return false;

    *out_image = (uint8_t*)malloc(256 * 8 * 8 * 4); // 256 chars, 8x8 pixels each, 4 channels
    if (!*out_image) return false;

    size_t out_idx = 0;
    for (int row = 0; row < 16; ++row) {
        for (int glyph_line = 0; glyph_line < 8; ++glyph_line) {
            for (int col = 0; col < 16; ++col) {
                uint8_t val = data[(row * 16 + col) * 8 + glyph_line];
                for (int bit = 0; bit < 8; ++bit) {
                    uint8_t mask = 1 << (7 - bit);
                    if ((val & mask) != 0) {
                        (*out_image)[out_idx++] = 255;
                        (*out_image)[out_idx++] = 255;
                        (*out_image)[out_idx++] = 255;
                        (*out_image)[out_idx++] = 255;
                    } else {
                        (*out_image)[out_idx++] = 0;
                        (*out_image)[out_idx++] = 0;
                        (*out_image)[out_idx++] = 0;
                        (*out_image)[out_idx++] = 0;
                    }
                }
            }
        }
    }

    return true;
}

bool load_font(SDL_Renderer* renderer, const char* path, Font* out_font) {
    FILE* f = fopen(path, "rb");
    if (!f) {
        fprintf(stderr, "Failed to open font: %s\n", path);
        return false;
    }

    fseek(f, 0, SEEK_END);
    size_t size = ftell(f);
    fseek(f, 0, SEEK_SET);

    uint8_t* data = (uint8_t*)malloc(size);
    if (!data) {
        fclose(f);
        return false;
    }

    if (fread(data, 1, size, f) != size) {
        free(data);
        fclose(f);
        return false;
    }
    fclose(f);

    uint8_t* rgba_data = NULL;
    if (!decode_font(data, size, &rgba_data)) {
        free(data);
        fprintf(stderr, "Failed to decode font: %s\n", path);
        return false;
    }
    free(data);

    SDL_Texture* texture = SDL_CreateTexture(
        renderer,
        SDL_PIXELFORMAT_RGBA32,
        SDL_TEXTUREACCESS_STATIC,
        16 * 8, // 128
        16 * 8  // 128
    );

    if (!texture) {
        free(rgba_data);
        fprintf(stderr, "Failed to create texture for font %s: %s\n", path, SDL_GetError());
        return false;
    }

    SDL_UpdateTexture(texture, NULL, rgba_data, 16 * 8 * 4);
    free(rgba_data);

    SDL_SetTextureBlendMode(texture, SDL_BLENDMODE_BLEND);
    out_font->texture = texture;
    return true;
}

void destroy_font(Font* font) {
    if (font->texture) {
        SDL_DestroyTexture(font->texture);
        font->texture = NULL;
    }
}

void render_text(SDL_Renderer* renderer, Font* font, int x, int y, SDL_Color color, const char* text) {
    if (!font->texture) return;

    SDL_SetTextureColorMod(font->texture, color.r, color.g, color.b);

    SDL_Rect source = {0, 0, 8, 8};
    SDL_Rect target = {x, y, 8, 8};

    for (const char* p = text; *p != '\0'; ++p) {
        uint8_t ch = (uint8_t)*p;
        // The original code only allows ascii but defaults to space for non-ascii
        if (ch > 127) ch = ' ';

        source.x = (ch % 16) * 8;
        source.y = (ch / 16) * 8;

        SDL_RenderCopy(renderer, font->texture, &source, &target);
        target.x += 8;
    }
}
