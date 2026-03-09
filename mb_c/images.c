#include "images.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define SCREEN_WIDTH 640
#define SCREEN_HEIGHT 480

static bool decode_plane(size_t bitplane_len, const uint8_t** it, const uint8_t* end, uint8_t* out_image) {
    size_t out_len = 0;
    while (out_len < bitplane_len) {
        if (*it >= end) return false;
        uint8_t val = *(*it)++;
        if (val != 1) {
            out_image[out_len++] = val;
        } else {
            if (*it + 2 > end) return false;
            val = *(*it)++;
            uint8_t len = *(*it)++;
            for (uint8_t i = 0; i < len; ++i) {
                if (out_len < bitplane_len + 256) out_image[out_len++] = val;
            }
        }
    }
    return true;
}

static bool decode_spy(const uint8_t* data, size_t size, uint32_t width, uint32_t height, uint8_t** out_rgb, SDL_Color* out_palette) {
    size_t bitplane_len = (width * height) / 8;
    if (size < 768) return false;

    const uint8_t* palette = data;
    const uint8_t* it = data + 768;
    const uint8_t* end = data + size;

    uint8_t* plane0 = (uint8_t*)malloc(bitplane_len + 256);
    uint8_t* plane1 = (uint8_t*)malloc(bitplane_len + 256);
    uint8_t* plane2 = (uint8_t*)malloc(bitplane_len + 256);
    uint8_t* plane3 = (uint8_t*)malloc(bitplane_len + 256);

    if (!decode_plane(bitplane_len, &it, end, plane0) ||
        !decode_plane(bitplane_len, &it, end, plane1) ||
        !decode_plane(bitplane_len, &it, end, plane2) ||
        !decode_plane(bitplane_len, &it, end, plane3)) {
        free(plane0); free(plane1); free(plane2); free(plane3); return false;
    }

    *out_rgb = (uint8_t*)malloc(width * height * 3);
    size_t out_idx = 0;

    for (size_t idx = 0; idx < bitplane_len; ++idx) {
        for (int bit = 7; bit >= 0; --bit) {
            uint8_t color = ((plane0[idx] >> bit) & 1) |
                            (((plane1[idx] >> bit) & 1) << 1) |
                            (((plane2[idx] >> bit) & 1) << 2) |
                            (((plane3[idx] >> bit) & 1) << 3);

            (*out_rgb)[out_idx++] = palette[color * 3];
            (*out_rgb)[out_idx++] = palette[color * 3 + 1];
            (*out_rgb)[out_idx++] = palette[color * 3 + 2];
        }
    }

    free(plane0); free(plane1); free(plane2); free(plane3);

    for (int i = 0; i < 16; ++i) {
        out_palette[i].r = palette[i * 3];
        out_palette[i].g = palette[i * 3 + 1];
        out_palette[i].b = palette[i * 3 + 2];
        out_palette[i].a = 255;
    }
    return true;
}

static bool decode_ppm(const uint8_t* data, size_t size, uint32_t* out_width, uint32_t* out_height, uint8_t** out_rgb, SDL_Color* out_palette) {
    if (size < 128 + 768 + 1) return false;

    uint32_t to_y = data[10] | (data[11] << 8);
    uint32_t from_y = data[6] | (data[7] << 8);
    uint32_t width = data[0x42] | (data[0x43] << 8);
    uint32_t height = to_y - from_y;

    *out_width = width; *out_height = height;

    const uint8_t* it = data + 128;
    const uint8_t* end = data + size - 769;
    const uint8_t* palette = data + size - 768;

    *out_rgb = (uint8_t*)malloc(width * height * 3);
    size_t out_idx = 0;

    for (uint32_t y = 0; y < height; ++y) {
        uint32_t x = 0;
        while (x < width) {
            if (it >= end) { free(*out_rgb); return false; }
            uint8_t value = *it++;
            if ((value & 0xC0) == 0xC0) {
                uint32_t len = value & 0x3F;
                if (it >= end) { free(*out_rgb); return false; }
                uint8_t color = *it++;
                for (uint32_t i = 0; i < len && x < width; ++i, ++x) {
                    (*out_rgb)[out_idx++] = palette[color * 3];
                    (*out_rgb)[out_idx++] = palette[color * 3 + 1];
                    (*out_rgb)[out_idx++] = palette[color * 3 + 2];
                }
            } else {
                uint8_t color = value;
                (*out_rgb)[out_idx++] = palette[color * 3];
                (*out_rgb)[out_idx++] = palette[color * 3 + 1];
                (*out_rgb)[out_idx++] = palette[color * 3 + 2];
                x++;
            }
        }
    }

    for (int i = 0; i < 16; ++i) {
        out_palette[i].r = palette[i * 3];
        out_palette[i].g = palette[i * 3 + 1];
        out_palette[i].b = palette[i * 3 + 2];
        out_palette[i].a = 255;
    }
    return true;
}

bool load_texture(SDL_Renderer* renderer, const char* path, TextureFormat format, TexturePalette* out_palette) {
    FILE* f = fopen(path, "rb");
    if (!f) return false;
    fseek(f, 0, SEEK_END);
    size_t size = ftell(f);
    fseek(f, 0, SEEK_SET);
    uint8_t* data = (uint8_t*)malloc(size);
    if (fread(data, 1, size, f) != size) { free(data); fclose(f); return false; }
    fclose(f);

    uint32_t width = SCREEN_WIDTH, height = SCREEN_HEIGHT;
    uint8_t* rgb_data = NULL;
    bool success = (format == TEXTURE_FORMAT_SPY) ? decode_spy(data, size, width, height, &rgb_data, out_palette->palette) :
                                                    decode_ppm(data, size, &width, &height, &rgb_data, out_palette->palette);
    free(data);
    if (!success) return false;

    SDL_Texture* texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGBA8888, SDL_TEXTUREACCESS_STATIC, width, height);
    if (!texture) { free(rgb_data); return false; }

    uint32_t* rgba_data = (uint32_t*)malloc(width * height * 4);
    for (uint32_t i = 0; i < width * height; ++i) {
        rgba_data[i] = (rgb_data[i*3] << 24) | (rgb_data[i*3+1] << 16) | (rgb_data[i*3+2] << 8) | 0xFF;
    }
    SDL_UpdateTexture(texture, NULL, rgba_data, width * 4);
    free(rgb_data); free(rgba_data);
    out_palette->texture = texture;
    return true;
}

void destroy_texture_palette(TexturePalette* tp) {
    if (tp->texture) SDL_DestroyTexture(tp->texture);
    tp->texture = NULL;
}
