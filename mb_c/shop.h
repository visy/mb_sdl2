#ifndef SHOP_H
#define SHOP_H
#include "app.h"
#include "game.h"

extern const uint32_t EQUIPMENT_PRICES[];

typedef enum { PLAYER_WIN, PLAYER_LOSE, PLAYER_DRAW } PlayerResult;

void app_process_round_result(App* app, const RoundResult* result, int nplayers, bool campaign);
bool shop_try_buy(App* app, int player, int item);
bool shop_try_sell(App* app, int player, int item);
void render_shop_ui(App* app, ApplicationContext* ctx, int selected_item[], int shop_players[], int num_panels);
PlayerResult compute_score(App* app, int player_idx, int nplayers);
void app_run_victory_screen(App* app, ApplicationContext* ctx);
void app_run_shop(App* app, ApplicationContext* ctx);

#endif
