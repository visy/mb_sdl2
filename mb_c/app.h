#ifndef APP_H
#define APP_H

#include "context.h"
#include "fonts.h"
#include "glyphs.h"

typedef struct {
    TexturePalette title;
    TexturePalette main_menu;
    TexturePalette sika; // Glyphs
    
    TexturePalette info[4];
    TexturePalette codes;
    
    Glyphs glyphs;
    Font font;
    char registered[256];
} App;

bool app_init(App* app, ApplicationContext* ctx);
void app_destroy(App* app);

void app_run_main_menu(App* app, ApplicationContext* ctx, bool campaign_mode);

#endif // APP_H
