#ifndef MENUS_H
#define MENUS_H
#include "app.h"

typedef enum { MENU_NEW_GAME, MENU_OPTIONS, MENU_INFO, MENU_QUIT } SelectedMenu;

void render_main_menu(App* app, ApplicationContext* ctx, SelectedMenu selected);
void update_shovel(App* app, ApplicationContext* ctx, SelectedMenu previous, SelectedMenu selected);
SelectedMenu menu_next(SelectedMenu current);
SelectedMenu menu_prev(SelectedMenu current);

void app_run_options(App* app, ApplicationContext* ctx);
void app_run_info_menu(App* app, ApplicationContext* ctx);
void app_run_sound_test(App* app, ApplicationContext* ctx);
void app_run_level_test(App* app, ApplicationContext* ctx);
void app_run_music_debugger(App* app, ApplicationContext* ctx);

#endif
