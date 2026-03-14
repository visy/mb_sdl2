#ifndef APP_H
#define APP_H

#include "context.h"
#include "fonts.h"
#include "glyphs.h"

typedef enum {
    EQUIP_SMALL_BOMB,
    EQUIP_BIG_BOMB,
    EQUIP_DYNAMITE,
    EQUIP_ATOMIC_BOMB,
    EQUIP_SMALL_RADIO,
    EQUIP_LARGE_RADIO,
    EQUIP_GRENADE,
    EQUIP_MINE,
    EQUIP_FLAMETHROWER,
    EQUIP_NAPALM,
    EQUIP_BARREL,
    EQUIP_SMALL_CRUCIFIX,
    EQUIP_LARGE_CRUCIFIX,
    EQUIP_PLASTIC,
    EQUIP_EXPLOSIVE_PLASTIC,
    EQUIP_DIGGER,
    EQUIP_METAL_WALL,
    EQUIP_SMALL_PICKAXE,
    EQUIP_LARGE_PICKAXE,
    EQUIP_DRILL,
    EQUIP_TELEPORT,
    EQUIP_CLONE,
    EQUIP_BIOMASS,
    EQUIP_EXTINGUISHER,
    EQUIP_ARMOR,
    EQUIP_JUMPING_BOMB,
    EQUIP_SUPER_DRILL,
    EQUIP_TOTAL
} Equipment;

typedef struct {
    TexturePalette title;
    TexturePalette main_menu;
    TexturePalette sika; // Glyphs
    TexturePalette shop;
    TexturePalette players;
    
    TexturePalette info[4];
    TexturePalette codes;
    
    Glyphs glyphs;
    Font font;
    char registered[256];

    Mix_Chunk* sounds[64];
    char sound_names[64][32];
    int sound_count;
    Mix_Chunk* sound_kili;
    Mix_Chunk* sound_picaxe;
    Mix_Chunk* sound_explos1;
    Mix_Chunk* sound_explos2;
    Mix_Chunk* sound_explos3;
    Mix_Chunk* sound_explos4;
    Mix_Chunk* sound_explos5;
    Mix_Chunk* sound_pikkupom;

    char level_names[128][32];
    uint8_t* level_data[128];
    int level_count;

    uint32_t player_cash;
    int player_inventory[EQUIP_TOTAL];
} App;

bool app_init(App* app, ApplicationContext* ctx);
void app_destroy(App* app);

void app_run_main_menu(App* app, ApplicationContext* ctx, bool campaign_mode);

#endif // APP_H
