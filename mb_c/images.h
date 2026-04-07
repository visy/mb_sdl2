#ifndef IMAGES_H
#define IMAGES_H

#include <SDL2/SDL.h>
#include <stdbool.h>

typedef enum {
    TEXTURE_FORMAT_SPY,
    TEXTURE_FORMAT_PPM
} TextureFormat;

typedef struct {
    SDL_Texture* texture;
    SDL_Color palette[16];
} TexturePalette;

bool load_texture(SDL_Renderer* renderer, const char* path, TextureFormat format, TexturePalette* out_palette);
bool load_texture_keyed(SDL_Renderer* renderer, const char* path, TextureFormat format, TexturePalette* out_palette);
void destroy_texture_palette(TexturePalette* tp);

#endif // IMAGES_H
