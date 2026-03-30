#ifdef MB_NET

#include "netgame.h"
#include "game.h"
#include "shop.h"
#include "persist.h"
#include "fonts.h"
#include "input.h"
#include "context.h"
#include "editor.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

static void pack_options_to_game_start(const GameOptions* o, NetGameStart* gs) {
    gs->cash = o->cash;
    gs->treasures = o->treasures;
    gs->rounds = o->rounds;
    gs->round_time_secs = o->round_time_secs;
    gs->players = o->players;
    gs->speed = o->speed;
    gs->bomb_damage = o->bomb_damage;
    gs->flags = (o->darkness ? 1 : 0) | (o->free_market ? 2 : 0)
              | (o->selling ? 4 : 0) | (o->win_by_money ? 8 : 0);
}

static void unpack_game_start_to_options(const NetGameStart* gs, GameOptions* o) {
    o->cash = gs->cash;
    o->treasures = gs->treasures;
    o->rounds = gs->rounds;
    o->round_time_secs = gs->round_time_secs;
    o->players = gs->players;
    o->speed = gs->speed;
    o->bomb_damage = gs->bomb_damage;
    o->darkness = (gs->flags & 1) != 0;
    o->free_market = (gs->flags & 2) != 0;
    o->selling = (gs->flags & 4) != 0;
    o->win_by_money = (gs->flags & 8) != 0;
}


// Check if all active net players satisfy a condition array (e.g. all ready)
static bool net_all_players_check(const NetContext* net, const bool flags[NET_MAX_PLAYERS]) {
    for (int s = 0; s < NET_MAX_PLAYERS; s++) {
        if (net_slot_active(net, s) && !flags[s]) return false;
    }
    return true;
}

// Count active net players
static int net_count_active(const NetContext* net) {
    int n = 0;
    for (int s = 0; s < NET_MAX_PLAYERS; s++)
        if (net_slot_active(net, s)) n++;
    return n;
}

// Send a SHOP_STATE broadcast for a given player from the server
static void net_broadcast_shop_state(App* app, NetContext* net, int pi, int cursor) {
    NetMessage msg = {0};
    msg.type = NET_MSG_SHOP_STATE;
    msg.data.shop_state.player_index = pi;
    msg.data.shop_state.cash = app->player_cash[pi];
    memcpy(msg.data.shop_state.inventory, app->player_inventory[pi], sizeof(msg.data.shop_state.inventory));
    msg.data.shop_state.cursor = cursor;
    net_broadcast(net, &msg);
}

// Send cursor position to server (client) or broadcast to all (server)
static void net_send_cursor(App* app, NetContext* net, int cursor) {
    NetMessage msg = {0};
    msg.type = NET_MSG_SHOP_CURSOR;
    msg.data.shop_cursor.player_index = net->local_player;
    msg.data.shop_cursor.cursor = cursor;
    if (net->is_server)
        net_broadcast(net, &msg);
    else
        net_send_to(net->server_peer, &msg);
    (void)app;
}

// Returns: 1=all ready, 0=disconnect, -1=F10 quit match
static int app_run_net_shop(App* app, ApplicationContext* ctx) {
    NetContext* net = &app->net;
    int local = net->local_player;
    int cursors[NET_MAX_PLAYERS] = {0};

    context_play_music_at(ctx, "OEKU.S3M", 83);

    // Build ordered player list from active slots
    int all_players[NET_MAX_PLAYERS];
    int total_players = 0;
    for (int s = 0; s < NET_MAX_PLAYERS; s++) {
        if (net_slot_active(net, s))
            all_players[total_players++] = s;
    }

    // Process shop in batches of 2 — all players see the active batch,
    // but only the active batch's players can send input.
    bool batch_ready[NET_MAX_PLAYERS] = {false};

    for (int batch_start = 0; batch_start < total_players; batch_start += 2) {
        int batch[2];
        int num_batch = 0;
        for (int i = batch_start; i < total_players && i < batch_start + 2; i++)
            batch[num_batch++] = all_players[i];
        // Swap so lower-index player is on right panel
        if (num_batch == 2) { int tmp = batch[0]; batch[0] = batch[1]; batch[1] = tmp; }

        // Is local player in this active batch?
        bool local_in_batch = false;
        for (int i = 0; i < num_batch; i++)
            if (batch[i] == local) local_in_batch = true;

        bool batch_done = false;
        bool ready_local = false;

        while (!batch_done) {
            // Render: everyone sees the active batch's shop
            int sel_arr[2];
            for (int i = 0; i < num_batch; i++)
                sel_arr[i] = cursors[batch[i]];
            render_shop_ui(app, ctx, sel_arr, batch, num_batch);

            context_present(ctx);

            // Input: only process if local player is in the active batch and not yet ready
            SDL_Event e;
            while (SDL_PollEvent(&e)) {
                if (e.type == SDL_QUIT) { return 0; }
                if (net->is_server && is_pause_event(&e, &app->input_config)) {
                    PauseChoice pc = pause_menu_net(app, ctx, PAUSE_CTX_SHOP, net);
                    if (pc == PAUSE_END_GAME) {
                        NetMessage quit_msg = {0};
                        quit_msg.type = NET_MSG_SHOP_ALL_READY;
                        quit_msg.data.shop_all_ready.next_round = -1;
                        net_broadcast(net, &quit_msg);
                        net_flush(net);
                        return -1;
                    }
                }
                if (!local_in_batch || ready_local) continue;
                ActionType act = input_map_event(&e, 0, &app->input_config);
                if (act == ACT_MAX_PLAYER) continue;
                int prev_cursor = cursors[local];
                switch (act) {
                    case ACT_UP:    if (cursors[local] >= 4) cursors[local] -= 4; break;
                    case ACT_DOWN:  if (cursors[local] + 4 <= EQUIP_TOTAL) cursors[local] += 4; else cursors[local] = EQUIP_TOTAL; break;
                    case ACT_LEFT:  cursors[local] = (cursors[local] + EQUIP_TOTAL) % (EQUIP_TOTAL + 1); break;
                    case ACT_RIGHT: cursors[local] = (cursors[local] + 1) % (EQUIP_TOTAL + 1); break;
                    case ACT_ACTION:
                        if (cursors[local] == EQUIP_TOTAL) {
                            ready_local = true;
                            if (net->is_server) {
                                batch_ready[local] = true;
                            } else {
                                NetMessage msg = {0};
                                msg.type = NET_MSG_SHOP_READY;
                                msg.data.shop_ready.player_index = local;
                                net_send_to(net->server_peer, &msg);
                            }
                        } else {
                            if (net->is_server) {
                                if (shop_try_buy(app, local, cursors[local]))
                                    net_broadcast_shop_state(app, net, local, cursors[local]);
                            } else {
                                NetMessage msg = {0};
                                msg.type = NET_MSG_SHOP_ACTION;
                                msg.data.shop_action.player_index = local;
                                msg.data.shop_action.action = SHOP_ACT_BUY;
                                msg.data.shop_action.item_index = cursors[local];
                                net_send_to(net->server_peer, &msg);
                            }
                        }
                        break;
                    case ACT_STOP:
                        if (cursors[local] != EQUIP_TOTAL) {
                            if (net->is_server) {
                                if (shop_try_sell(app, local, cursors[local]))
                                    net_broadcast_shop_state(app, net, local, cursors[local]);
                            } else {
                                NetMessage msg = {0};
                                msg.type = NET_MSG_SHOP_ACTION;
                                msg.data.shop_action.player_index = local;
                                msg.data.shop_action.action = SHOP_ACT_SELL;
                                msg.data.shop_action.item_index = cursors[local];
                                net_send_to(net->server_peer, &msg);
                            }
                        }
                        break;
                    default: break;
                }
                if (cursors[local] != prev_cursor)
                    net_send_cursor(app, net, cursors[local]);
            }

            // Server: check if all batch players are ready
            if (net->is_server) {
                bool all_batch_ready = true;
                for (int i = 0; i < num_batch; i++)
                    if (!batch_ready[batch[i]]) { all_batch_ready = false; break; }
                if (all_batch_ready) {
                    NetMessage msg = {0};
                    msg.type = NET_MSG_SHOP_ALL_READY;
                    msg.data.shop_all_ready.next_round = batch_start;
                    net_broadcast(net, &msg);
                    net_flush(net);
                    batch_done = true;
                }
            }

            // Poll network
            NetMessage net_msg;
            ENetPeer* from;
            while (net_poll(net, &net_msg, &from) > 0) {
                switch (net_msg.type) {
                    case NET_MSG_SHOP_ACTION:
                        if (net->is_server) {
                            int pi = net_msg.data.shop_action.player_index;
                            if (pi >= 0 && pi < NET_MAX_PLAYERS) {
                                if (net_msg.data.shop_action.action == SHOP_ACT_BUY)
                                    shop_try_buy(app, pi, net_msg.data.shop_action.item_index);
                                else
                                    shop_try_sell(app, pi, net_msg.data.shop_action.item_index);
                                net_broadcast_shop_state(app, net, pi, net_msg.data.shop_action.item_index);
                            }
                        }
                        break;
                    case NET_MSG_SHOP_STATE: {
                        int pi = net_msg.data.shop_state.player_index;
                        if (pi >= 0 && pi < NET_MAX_PLAYERS) {
                            app->player_cash[pi] = net_msg.data.shop_state.cash;
                            memcpy(app->player_inventory[pi], net_msg.data.shop_state.inventory, sizeof(app->player_inventory[pi]));
                            cursors[pi] = net_msg.data.shop_state.cursor;
                        }
                        break;
                    }
                    case NET_MSG_SHOP_CURSOR: {
                        int pi = net_msg.data.shop_cursor.player_index;
                        if (pi >= 0 && pi < NET_MAX_PLAYERS)
                            cursors[pi] = net_msg.data.shop_cursor.cursor;
                        if (net->is_server)
                            net_broadcast(net, &net_msg);
                        break;
                    }
                    case NET_MSG_SHOP_READY:
                        if (net->is_server) {
                            int pi = net_msg.data.shop_ready.player_index;
                            if (pi >= 0 && pi < NET_MAX_PLAYERS)
                                batch_ready[pi] = true;
                        }
                        break;
                    case NET_MSG_SHOP_ALL_READY:
                        if (net_msg.data.shop_all_ready.next_round == -1) {
                            if (net->is_server) net_broadcast(net, &net_msg);
                            return -1;
                        }
                        batch_done = true;
                        break;
                    case NET_MSG_PLAYER_LEAVE:
                        return 0;
                    default:
                        break;
                }
            }

            SDL_Delay(16);
        }
    }
    return 1;
}

void app_run_netgame(App* app, ApplicationContext* ctx) {
    NetContext* net = &app->net;
    SDL_Color white = {255, 255, 255, 255};
    SDL_Color yellow = {255, 255, 0, 255};
    SDL_Color green = {0, 255, 0, 255};
    SDL_Color gray = {128, 128, 128, 255};

    if (!net_init()) return;

    int saved_level_count = app->selected_level_count;
    GameOptions saved_options = app->options;
    char saved_name[16];
    snprintf(saved_name, sizeof(saved_name), "%s", app->player_name[0]);

    // Phase A0: Name entry
    char net_name[NET_PLAYER_NAME_LEN];
    snprintf(net_name, sizeof(net_name), "%s", app->player_name[0]);
    int name_cursor = (int)strlen(net_name);
    {
        bool entering_name = true;
        SDL_StartTextInput();
        while (entering_name) {
            SDL_SetRenderTarget(ctx->renderer, ctx->buffer);
            SDL_SetRenderDrawColor(ctx->renderer, 0, 0, 0, 255);
            SDL_RenderClear(ctx->renderer);
            render_text(ctx->renderer, &app->font, 200, 100, white, "NETGAME");
            render_text(ctx->renderer, &app->font, 160, 140, gray, "ENTER YOUR NAME:");
            char display[32];
            snprintf(display, sizeof(display), "%s_", net_name);
            render_text(ctx->renderer, &app->font, 200, 165, yellow, display);
            render_text(ctx->renderer, &app->font, 160, 220, gray, "TYPE NAME  ENTER CONFIRM  ESC BACK");
            SDL_SetRenderTarget(ctx->renderer, NULL);
            context_present(ctx);

            SDL_Event e;
            while (SDL_PollEvent(&e)) {
                if (e.type == SDL_QUIT) { SDL_StopTextInput(); net_shutdown(); return; }
                if (e.type == SDL_KEYDOWN) {
                    if (e.key.keysym.scancode == SDL_SCANCODE_ESCAPE) { SDL_StopTextInput(); net_shutdown(); return; }
                    if (e.key.keysym.scancode == SDL_SCANCODE_BACKSPACE && name_cursor > 0) {
                        net_name[--name_cursor] = '\0';
                    }
                    if (e.key.keysym.scancode == SDL_SCANCODE_RETURN || e.key.keysym.scancode == SDL_SCANCODE_KP_ENTER) {
                        if (name_cursor > 0) entering_name = false;
                    }
                }
                if (e.type == SDL_TEXTINPUT && name_cursor < NET_PLAYER_NAME_LEN - 1) {
                    char ch = e.text.text[0];
                    if (ch >= 32 && ch < 127) {
                        net_name[name_cursor++] = ch;
                        net_name[name_cursor] = '\0';
                    }
                }
            }
            SDL_Delay(16);
        }
        SDL_StopTextInput();
    }

    // Phase A: Host/Join selection
    int choice = 0; // 0=host, 1=join
    bool selecting = true;
    while (selecting) {
        SDL_SetRenderTarget(ctx->renderer, ctx->buffer);
        SDL_SetRenderDrawColor(ctx->renderer, 0, 0, 0, 255);
        SDL_RenderClear(ctx->renderer);
        render_text(ctx->renderer, &app->font, 200, 100, white, "NETGAME");
        render_text(ctx->renderer, &app->font, 200, 140, choice == 0 ? yellow : gray, "> HOST GAME");
        render_text(ctx->renderer, &app->font, 200, 160, choice == 1 ? yellow : gray, "> JOIN GAME");
        render_text(ctx->renderer, &app->font, 160, 220, gray, "UP/DOWN SELECT  ENTER CONFIRM  ESC BACK");
        SDL_SetRenderTarget(ctx->renderer, NULL);
        context_present(ctx);

        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_QUIT) { net_shutdown(); return; }
            if (e.type == SDL_KEYDOWN) {
                if (e.key.keysym.scancode == SDL_SCANCODE_ESCAPE) { net_shutdown(); return; }
                if (e.key.keysym.scancode == SDL_SCANCODE_UP) choice = 0;
                if (e.key.keysym.scancode == SDL_SCANCODE_DOWN) choice = 1;
                if (e.key.keysym.scancode == SDL_SCANCODE_RETURN || e.key.keysym.scancode == SDL_SCANCODE_KP_ENTER) selecting = false;
            }
        }
        SDL_Delay(16);
    }

    // Phase B: Connect
    if (choice == 0) {
        // Host
        if (!net_host_create(net, NET_PORT)) {
            net_shutdown();
            return;
        }
        // Pack host options into server info for joining clients
        net->host_info.cash = app->options.cash;
        net->host_info.treasures = app->options.treasures;
        net->host_info.rounds = app->options.rounds;
        net->host_info.round_time_secs = app->options.round_time_secs;
        net->host_info.speed = app->options.speed;
        net->host_info.bomb_damage = app->options.bomb_damage;
        net->host_info.flags = (app->options.darkness ? 1 : 0) | (app->options.free_market ? 2 : 0)
                             | (app->options.selling ? 4 : 0) | (app->options.win_by_money ? 8 : 0);
        snprintf(net->player_names[0], NET_PLAYER_NAME_LEN, "%s", net_name);
    } else {
        // Join - hostname entry
        char hostname[128] = "localhost";
        int cursor = (int)strlen(hostname);
        bool entering = true;
        bool connecting = false;
        SDL_StartTextInput();

        while (entering) {
            SDL_SetRenderTarget(ctx->renderer, ctx->buffer);
            SDL_SetRenderDrawColor(ctx->renderer, 0, 0, 0, 255);
            SDL_RenderClear(ctx->renderer);
            render_text(ctx->renderer, &app->font, 200, 100, white, "ENTER HOST ADDRESS:");
            char display[140];
            snprintf(display, sizeof(display), "%s_", hostname);
            render_text(ctx->renderer, &app->font, 200, 130, yellow, connecting ? "CONNECTING..." : display);
            render_text(ctx->renderer, &app->font, 160, 200, gray, "TYPE ADDRESS  ENTER CONNECT  ESC BACK");
            SDL_SetRenderTarget(ctx->renderer, NULL);
            context_present(ctx);

            if (connecting) {
                if (!net_client_connect(net, hostname, NET_PORT)) {
                    // Show failure briefly
                    SDL_SetRenderTarget(ctx->renderer, ctx->buffer);
                    SDL_SetRenderDrawColor(ctx->renderer, 0, 0, 0, 255);
                    SDL_RenderClear(ctx->renderer);
                    SDL_Color red = {255, 0, 0, 255};
                    render_text(ctx->renderer, &app->font, 200, 140, red, "CONNECTION FAILED");
                    SDL_SetRenderTarget(ctx->renderer, NULL);
                    context_present(ctx);
                    SDL_Delay(2000);
                    connecting = false;
                    continue;
                }
                SDL_StopTextInput();
                entering = false;
                break;
            }

            SDL_Event e;
            while (SDL_PollEvent(&e)) {
                if (e.type == SDL_QUIT) { SDL_StopTextInput(); net_shutdown(); return; }
                if (e.type == SDL_KEYDOWN) {
                    if (e.key.keysym.scancode == SDL_SCANCODE_ESCAPE) {
                        SDL_StopTextInput();
                        net_shutdown();
                        return;
                    }
                    if (e.key.keysym.scancode == SDL_SCANCODE_BACKSPACE && cursor > 0) {
                        hostname[--cursor] = '\0';
                    }
                    if (e.key.keysym.scancode == SDL_SCANCODE_RETURN || e.key.keysym.scancode == SDL_SCANCODE_KP_ENTER) {
                        connecting = true;
                    }
                }
                if (e.type == SDL_TEXTINPUT && cursor < 126) {
                    int len = (int)strlen(e.text.text);
                    if (cursor + len < 127) {
                        memcpy(hostname + cursor, e.text.text, len);
                        cursor += len;
                        hostname[cursor] = '\0';
                    }
                }
            }
            SDL_Delay(16);
        }
    }

    // Phase C: Lobby
    bool in_lobby = true;
    bool game_started = false;

    while (in_lobby) {
        // Render lobby
        SDL_SetRenderTarget(ctx->renderer, ctx->buffer);
        SDL_SetRenderDrawColor(ctx->renderer, 0, 0, 0, 255);
        SDL_RenderClear(ctx->renderer);

        render_text(ctx->renderer, &app->font, 260, 10, white, "LOBBY");
        char info[128];
        snprintf(info, sizeof(info), "CASH:%u ROUNDS:%u SPEED:%u", app->options.cash, app->options.rounds, app->options.speed);
        render_text(ctx->renderer, &app->font, 160, 30, gray, info);
        snprintf(info, sizeof(info), "PORT:%d", NET_PORT);
        render_text(ctx->renderer, &app->font, 250, 44, gray, info);

        // Player avatars and names
        for (int i = 0; i < NET_MAX_PLAYERS; i++) {
            int ax = 32 + 150 * i;
            if (net_slot_active(net, i)) {
                // Draw win portrait preview
                if (app->avatar_win[i].texture) {
                    SDL_Rect dest = {ax, 70, 132, 218};
                    SDL_RenderCopy(ctx->renderer, app->avatar_win[i].texture, NULL, &dest);
                }
                const char* name = net->player_names[i][0] ? net->player_names[i] : "???";
                render_text(ctx->renderer, &app->font, ax + 4, 292, net->player_ready[i] ? green : white, name);
                render_text(ctx->renderer, &app->font, ax + 4, 308,
                            net->player_ready[i] ? green : gray,
                            net->player_ready[i] ? "READY" : "waiting");
            } else {
                // Empty slot - dim placeholder
                render_text(ctx->renderer, &app->font, ax + 30, 170, gray, "---");
            }
        }

        if (net->is_server)
            render_text(ctx->renderer, &app->font, 140, 340, gray, "ENTER=TOGGLE READY  ESC=QUIT");
        else
            render_text(ctx->renderer, &app->font, 140, 340, gray, "ENTER=TOGGLE READY  ESC=DISCONNECT");

        context_present(ctx);

        // Input
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_QUIT) { net_disconnect(net); net_shutdown(); app->options = saved_options; snprintf(app->player_name[0], sizeof(app->player_name[0]), "%s", saved_name); return; }
            if (e.type == SDL_KEYDOWN) {
                if (e.key.keysym.scancode == SDL_SCANCODE_ESCAPE) {
                    net_disconnect(net);
                    net_shutdown();
                    app->options = saved_options;
                    snprintf(app->player_name[0], sizeof(app->player_name[0]), "%s", saved_name);
                    return;
                }
                if (e.key.keysym.scancode == SDL_SCANCODE_RETURN || e.key.keysym.scancode == SDL_SCANCODE_KP_ENTER) {
                    net->player_ready[net->local_player] = !net->player_ready[net->local_player];
                    if (net->is_server) {
                        // Broadcast ready update
                        NetMessage msg = {0};
                        msg.type = NET_MSG_READY_UPDATE;
                        memcpy(msg.data.ready_update.ready, net->player_ready, sizeof(msg.data.ready_update.ready));
                        net_broadcast(net, &msg);

                        // Check if all ready and we have 2+ players
                        { int nactive = net_count_active(net);
                        if (nactive >= 2 && net_all_players_check(net, net->player_ready)) {
                                // Generate level list now so it's included in GAME_START
                                app->options.players = (uint8_t)nactive;
                                if (app->selected_level_count == 0 && app->level_count > 0) {
                                    int count = app->options.rounds;
                                    if (count > 128) count = 128;
                                    int pool[128];
                                    for (int i = 0; i < app->level_count; i++) pool[i] = i;
                                    int filled = 0;
                                    while (filled < count) {
                                        for (int i = app->level_count - 1; i > 0; i--) {
                                            int j = rand() % (i + 1);
                                            int tmp = pool[i]; pool[i] = pool[j]; pool[j] = tmp;
                                        }
                                        int take = count - filled;
                                        if (take > app->level_count) take = app->level_count;
                                        for (int i = 0; i < take; i++)
                                            app->selected_levels[filled++] = pool[i] + 1;
                                    }
                                    app->selected_level_count = count;
                                }
                                // Send GAME_START with level selection
                                NetMessage gs = {0};
                                gs.type = NET_MSG_GAME_START;
                                pack_options_to_game_start(&app->options, &gs.data.game_start);
                                gs.data.game_start.level_count = app->selected_level_count;
                                gs.data.game_start.rng_seed = (uint32_t)SDL_GetTicks() ^ 0xDEADBEEF;
                                if (app->selected_level_count > 0)
                                    memcpy(gs.data.game_start.selected_levels, app->selected_levels,
                                           app->selected_level_count * sizeof(int));
                                net_broadcast(net, &gs);
                                game_seed_rng(gs.data.game_start.rng_seed);
                                game_started = true;
                                in_lobby = false;
                        } }
                    } else {
                        // Send ready to server
                        NetMessage msg = {0};
                        msg.type = NET_MSG_PLAYER_READY;
                        msg.data.player_ready.player_index = net->local_player;
                        msg.data.player_ready.is_ready = net->player_ready[net->local_player];
                        net_send_to(net->server_peer, &msg);
                    }
                }
            }
        }

        // Poll network
        NetMessage net_msg;
        ENetPeer* from;
        while (net_poll(net, &net_msg, &from) > 0) {
            switch (net_msg.type) {
                case NET_MSG_SERVER_INFO: {
                    // Client received server info — apply host's game rules
                    net->local_player = net_msg.data.server_info.assigned_index;
                    net->player_count = net_msg.data.server_info.player_count;
                    memcpy(net->player_names, net_msg.data.server_info.player_names, sizeof(net->player_names));
                    app->options.cash = net_msg.data.server_info.cash;
                    app->options.treasures = net_msg.data.server_info.treasures;
                    app->options.rounds = net_msg.data.server_info.rounds;
                    app->options.round_time_secs = net_msg.data.server_info.round_time_secs;
                    app->options.speed = net_msg.data.server_info.speed;
                    app->options.bomb_damage = net_msg.data.server_info.bomb_damage;
                    app->options.darkness = (net_msg.data.server_info.flags & 1) != 0;
                    app->options.free_market = (net_msg.data.server_info.flags & 2) != 0;
                    app->options.selling = (net_msg.data.server_info.flags & 4) != 0;
                    app->options.win_by_money = (net_msg.data.server_info.flags & 8) != 0;
                    // Set our name in the slot
                    snprintf(net->player_names[net->local_player], NET_PLAYER_NAME_LEN, "%s", net_name);
                    // Tell server our name
                    NetMessage join = {0};
                    join.type = NET_MSG_PLAYER_JOIN;
                    join.data.player_join.player_index = net->local_player;
                    snprintf(join.data.player_join.player_name, NET_PLAYER_NAME_LEN, "%s", net_name);
                    net_send_to(net->server_peer, &join);
                    break;
                }
                case NET_MSG_PLAYER_JOIN: {
                    int pi = net_msg.data.player_join.player_index;
                    if (pi >= 0 && pi < NET_MAX_PLAYERS) {
                        snprintf(net->player_names[pi], NET_PLAYER_NAME_LEN, "%s", net_msg.data.player_join.player_name);
                        if (net->is_server) {
                            // Re-broadcast to all clients so everyone knows the name
                            net_broadcast(net, &net_msg);
                        } else {
                            // Recount players from names
                            int cnt = 0;
                            for (int s = 0; s < NET_MAX_PLAYERS; s++)
                                if (net->player_names[s][0]) cnt++;
                            net->player_count = cnt;
                        }
                    }
                    break;
                }
                case NET_MSG_PLAYER_LEAVE: {
                    int pi = net_msg.data.player_leave.player_index;
                    if (pi >= 0 && pi < NET_MAX_PLAYERS) {
                        net->player_names[pi][0] = '\0';
                        net->player_ready[pi] = false;
                        if (!net->is_server) {
                            int cnt = 0;
                            for (int s = 0; s < NET_MAX_PLAYERS; s++)
                                if (net->player_names[s][0]) cnt++;
                            net->player_count = cnt;
                        }
                    }
                    if (!net->is_server && pi == -1) {
                        // Server disconnected
                        net_disconnect(net);
                        net_shutdown();
                        app->options = saved_options;
                        snprintf(app->player_name[0], sizeof(app->player_name[0]), "%s", saved_name);
                        return;
                    }
                    break;
                }
                case NET_MSG_PLAYER_READY:
                    if (net->is_server) {
                        int pi = net_msg.data.player_ready.player_index;
                        if (pi >= 0 && pi < NET_MAX_PLAYERS) {
                            net->player_ready[pi] = net_msg.data.player_ready.is_ready;
                            // Broadcast ready update
                            NetMessage ru = {0};
                            ru.type = NET_MSG_READY_UPDATE;
                            memcpy(ru.data.ready_update.ready, net->player_ready, sizeof(ru.data.ready_update.ready));
                            net_broadcast(net, &ru);
                            // Check if all ready
                            { int nactive = net_count_active(net);
                            if (nactive >= 2 && net_all_players_check(net, net->player_ready)) {
                                app->options.players = (uint8_t)nactive;
                                if (app->selected_level_count == 0 && app->level_count > 0) {
                                    int count = app->options.rounds;
                                    if (count > 128) count = 128;
                                    int pool[128];
                                    for (int i = 0; i < app->level_count; i++) pool[i] = i;
                                    int filled = 0;
                                    while (filled < count) {
                                        for (int i = app->level_count - 1; i > 0; i--) {
                                            int j = rand() % (i + 1);
                                            int tmp = pool[i]; pool[i] = pool[j]; pool[j] = tmp;
                                        }
                                        int take = count - filled;
                                        if (take > app->level_count) take = app->level_count;
                                        for (int i = 0; i < take; i++)
                                            app->selected_levels[filled++] = pool[i] + 1;
                                    }
                                    app->selected_level_count = count;
                                }
                                NetMessage gs = {0};
                                gs.type = NET_MSG_GAME_START;
                                pack_options_to_game_start(&app->options, &gs.data.game_start);
                                gs.data.game_start.level_count = app->selected_level_count;
                                gs.data.game_start.rng_seed = (uint32_t)SDL_GetTicks() ^ 0xDEADBEEF;
                                if (app->selected_level_count > 0)
                                    memcpy(gs.data.game_start.selected_levels, app->selected_levels,
                                           app->selected_level_count * sizeof(int));
                                net_broadcast(net, &gs);
                                game_seed_rng(gs.data.game_start.rng_seed);
                                game_started = true;
                                in_lobby = false;
                            } }
                        }
                    }
                    break;
                case NET_MSG_READY_UPDATE:
                    memcpy(net->player_ready, net_msg.data.ready_update.ready, sizeof(net->player_ready));
                    break;
                case NET_MSG_GAME_START: {
                    unpack_game_start_to_options(&net_msg.data.game_start, &app->options);
                    app->selected_level_count = net_msg.data.game_start.level_count;
                    if (app->selected_level_count > 0)
                        memcpy(app->selected_levels, net_msg.data.game_start.selected_levels,
                               app->selected_level_count * sizeof(int));
                    game_seed_rng(net_msg.data.game_start.rng_seed);
                    game_started = true;
                    in_lobby = false;
                    break;
                }
                default:
                    break;
            }
        }

        SDL_Delay(16);
    }

    if (!game_started) {
        net_disconnect(net);
        net_shutdown();
        app->options = saved_options;
        snprintf(app->player_name[0], sizeof(app->player_name[0]), "%s", saved_name);
        return;
    }

    // Initialize game state for net play — force multiplayer mode
    app->options.players = (uint8_t)net->player_count;
    app->current_round = 0;
    memset(app->game_stats, 0, sizeof(app->game_stats));
    for (int p = 0; p < MAX_PLAYERS; ++p) {
        app->player_cash[p] = app->options.cash;
        app->player_rounds_won[p] = 0;
        memset(app->player_inventory[p], 0, sizeof(app->player_inventory[p]));
    }
    // Copy net player names into app player slots
    for (int p = 0; p < NET_MAX_PLAYERS; p++) {
        if (net->player_names[p][0])
            snprintf(app->player_name[p], sizeof(app->player_name[p]), "%s", net->player_names[p]);
    }

    // Run net shop + gameplay rounds
    bool disconnected = false;
    bool show_victory = true;
    for (int round = 0; round < app->options.rounds && !disconnected; round++) {
        app->current_round = round;
        int shop_result = app_run_net_shop(app, ctx);
        if (shop_result == 0) { disconnected = true; break; }   // disconnect
        if (shop_result == -1) break;                            // F10 quit — show victory
        if (!net->connected) { disconnected = true; break; }

        // Determine level
        if (app->level_count > 0) {
            int lvl_idx;
            if (app->selected_level_count > 0 && round < app->selected_level_count) {
                int sel = app->selected_levels[round];
                if (sel == 0) lvl_idx = round % app->level_count;
                else lvl_idx = sel - 1;
            } else {
                lvl_idx = round % app->level_count;
            }

            context_animate(ctx, ANIMATION_FADE_DOWN, 7);
            RoundResult result = game_run(app, ctx, app->level_data[lvl_idx], net);

            app_process_round_result(app, &result, app->options.players, false);

            context_linger_music_start(ctx);
            context_animate(ctx, ANIMATION_FADE_DOWN, 7);
            context_linger_music_end(ctx);

            if (result.end_type == ROUND_END_FINAL) break;  // F10 during game — show victory
            if (!net->connected) { disconnected = true; break; }
        }
    }

    // Victory screen (unless disconnected)
    if (!disconnected && show_victory) {
        context_stop_music(ctx);
        app_run_victory_screen(app, ctx);
    }

    // Update roster stats for net players
    bool completed = app->current_round >= app->options.rounds;
    for (int p = 0; p < app->options.players; p++) {
        int8_t ri = app->roster.identities[p];
        if (ri >= 0 && app->roster.entries[ri].active) {
            bool won = completed && compute_score(app, p, app->options.players) == PLAYER_WIN;
            roster_update_stats(&app->roster.entries[ri], &app->game_stats[p], won);
        }
    }
    roster_save(&app->roster, ctx->game_dir);

    app->selected_level_count = saved_level_count;
    app->options = saved_options;
    snprintf(app->player_name[0], sizeof(app->player_name[0]), "%s", saved_name);
    net_disconnect(net);
    net_shutdown();
}
#endif /* MB_NET */
