// WebRTC DataChannel backend for Mine Bombers networking.
// Replaces net.c when built with -DMB_NET -DMB_WEB (Emscripten target).
//
// Mirrors the net.c API exactly so netgame.c compiles unchanged. Bridges to
// JS via window.MBNet (see wasm/netplay.js). Star topology: slot 0 = host,
// 1..3 = clients. Reliable+ordered DataChannel mimics ENet reliable channel.
//
// Connection events: WebRTC DataChannel doesn't push CONNECT/DISCONNECT as
// messages, so net_poll synthesizes them by diffing JS-reported per-slot
// connection state against last-known state.

#if defined(MB_NET) && defined(MB_WEB)

#include "net.h"
#include <emscripten.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct NetWebPeer {
    int slot;
    void* data;   // mirrors ENetPeer->data semantics used in net.c
};

struct NetWebHost {
    int dummy;
};

static struct NetWebPeer g_peers[NET_MAX_PLAYERS];
static struct NetWebHost g_host;
static bool g_last_connected[NET_MAX_PLAYERS];

// --- JS bridge (defined in wasm/netplay.js as window.MBNet.*) ---
EM_JS(void, mbnet_host,     (const char* room), { window.MBNet.host(UTF8ToString(room)); });
EM_JS(void, mbnet_join,     (const char* room), { window.MBNet.join(UTF8ToString(room)); });
EM_JS(int,  mbnet_local,    (void),             { return window.MBNet.localSlot(); });
EM_JS(int,  mbnet_conn,     (int slot),         { return window.MBNet.isConnected(slot); });
EM_JS(int,  mbnet_poll_raw, (int ptr, int max), { return window.MBNet.poll(ptr, max); });
EM_JS(int,  mbnet_send_raw, (int slot, int ptr, int len), { return window.MBNet.sendTo(slot, ptr, len); });
EM_JS(int,  mbnet_bcast_raw,(int ptr, int len), { return window.MBNet.broadcast(ptr, len); });

bool net_init(void) {
    memset(g_peers, 0, sizeof(g_peers));
    memset(g_last_connected, 0, sizeof(g_last_connected));
    for (int i = 0; i < NET_MAX_PLAYERS; i++) g_peers[i].slot = i;
    return true;
}

void net_shutdown(void) {}

// Room name = explicit user input. If empty/NULL, fall back to port-derived.
static void resolve_room(const char* room_name, int port, char* out, size_t cap) {
    if (room_name && room_name[0]) {
        snprintf(out, cap, "%s", room_name);
    } else {
        snprintf(out, cap, "mb-%d", port);
    }
}

bool net_host_create(NetContext* ctx, int port, const char* room_name) {
    memset(ctx, 0, sizeof(*ctx));
    ctx->is_server = true;
    ctx->local_player = 0;
    ctx->player_count = 1;
    ctx->host = &g_host;
    char room[64]; resolve_room(room_name, port, room, sizeof(room));
    mbnet_host(room);
    // Yield a few ticks so signaling WebSocket opens before caller starts
    // expecting state. Real readiness is per-peer via mbnet_conn().
    for (int i = 0; i < 20; i++) emscripten_sleep(50);
    ctx->connected = true;
    return true;
}

bool net_client_connect(NetContext* ctx, const char* hostname, int port, const char* room_name) {
    (void)hostname; // ignored on web: WebRTC handshake goes through signaling server
    memset(ctx, 0, sizeof(*ctx));
    ctx->is_server = false;
    ctx->host = &g_host;
    char room[64]; resolve_room(room_name, port, room, sizeof(room));
    mbnet_join(room);

    // Spin up to 10s waiting for DataChannel to slot 0 (host) to open.
    for (int i = 0; i < 200; i++) {
        if (mbnet_conn(0)) {
            ctx->server_peer = &g_peers[0];
            ctx->connected = true;
            ctx->local_player = mbnet_local();
            g_last_connected[0] = true;
            return true;
        }
        emscripten_sleep(50);
    }
    fprintf(stderr, "net_web: connect timed out\n");
    return false;
}

void net_disconnect(NetContext* ctx) {
    if (!ctx->host) return;

    NetMessage leave;
    memset(&leave, 0, sizeof(leave));
    leave.type = NET_MSG_PLAYER_LEAVE;

    if (ctx->is_server) {
        leave.data.player_leave.player_index = 0;
        net_broadcast(ctx, &leave);
    } else if (ctx->server_peer) {
        leave.data.player_leave.player_index = ctx->local_player;
        net_send_to(ctx->server_peer, &leave);
    }
    // Give browser a tick to flush.
    emscripten_sleep(100);

    ctx->host = NULL;
    ctx->server_peer = NULL;
    memset(ctx->peers, 0, sizeof(ctx->peers));
    ctx->connected = false;
    memset(g_last_connected, 0, sizeof(g_last_connected));
}

void net_send_to(ENetPeer* peer, const NetMessage* msg) {
    if (!peer) return;
    mbnet_send_raw(peer->slot, (int)(intptr_t)msg, (int)sizeof(NetMessage));
}

void net_broadcast(NetContext* ctx, const NetMessage* msg) {
    (void)ctx;
    mbnet_bcast_raw((int)(intptr_t)msg, (int)sizeof(NetMessage));
}

void net_flush(NetContext* ctx) { (void)ctx; }

bool net_slot_active(const NetContext* ctx, int slot) {
    if (slot < 0 || slot >= NET_MAX_PLAYERS) return false;
    if (ctx->is_server) {
        if (slot == 0) return true;
        return ctx->peers[slot] != NULL;
    } else {
        return ctx->player_names[slot][0] != '\0' || slot == ctx->local_player;
    }
}

// Synthesize one connect/disconnect event by diffing per-slot state.
// Returns 1 if an event was placed in out_msg, 0 if none.
static int poll_state_changes(NetContext* ctx, NetMessage* out_msg, ENetPeer** out_peer) {
    for (int slot = 0; slot < NET_MAX_PLAYERS; slot++) {
        if (slot == ctx->local_player) continue;
        bool now = mbnet_conn(slot) != 0;
        if (now == g_last_connected[slot]) continue;
        g_last_connected[slot] = now;

        if (now) {
            // CONNECT
            if (ctx->is_server) {
                ctx->peers[slot] = &g_peers[slot];
                ctx->player_count++;

                // Send SERVER_INFO mirroring net.c behavior.
                NetMessage info_msg;
                memset(&info_msg, 0, sizeof(info_msg));
                info_msg.type = NET_MSG_SERVER_INFO;
                info_msg.data.server_info = ctx->host_info;
                info_msg.data.server_info.player_count = ctx->player_count;
                info_msg.data.server_info.assigned_index = slot;
                memcpy(info_msg.data.server_info.player_names,
                       ctx->player_names, sizeof(ctx->player_names));
                net_send_to(&g_peers[slot], &info_msg);

                if (out_msg) {
                    memset(out_msg, 0, sizeof(NetMessage));
                    out_msg->type = NET_MSG_PLAYER_JOIN;
                    out_msg->data.player_join.player_index = slot;
                }
                if (out_peer) *out_peer = &g_peers[slot];
                return 1;
            } else {
                // Client side: connect to host already handled in net_client_connect.
                ctx->connected = true;
                continue;
            }
        } else {
            // DISCONNECT
            if (ctx->is_server) {
                if (ctx->peers[slot]) {
                    ctx->peers[slot] = NULL;
                    ctx->player_ready[slot] = false;
                    ctx->player_names[slot][0] = '\0';
                    if (ctx->player_count > 0) ctx->player_count--;

                    NetMessage leave_msg;
                    memset(&leave_msg, 0, sizeof(leave_msg));
                    leave_msg.type = NET_MSG_PLAYER_LEAVE;
                    leave_msg.data.player_leave.player_index = slot;
                    net_broadcast(ctx, &leave_msg);

                    if (out_msg) *out_msg = leave_msg;
                    if (out_peer) *out_peer = &g_peers[slot];
                    return 1;
                }
            } else if (slot == 0) {
                ctx->connected = false;
                if (out_msg) {
                    memset(out_msg, 0, sizeof(NetMessage));
                    out_msg->type = NET_MSG_PLAYER_LEAVE;
                    out_msg->data.player_leave.player_index = -1;
                }
                return 1;
            }
        }
    }
    return 0;
}

int net_poll(NetContext* ctx, NetMessage* out_msg, ENetPeer** out_peer) {
    if (!ctx || !ctx->host) return 0;

    // 1. Synthesize CONNECT/DISCONNECT from per-slot state diff.
    if (poll_state_changes(ctx, out_msg, out_peer) > 0) return 1;

    // 2. Drain one data packet from JS inbox.
    NetMessage tmp;
    int packed = mbnet_poll_raw((int)(intptr_t)&tmp, (int)sizeof(NetMessage));
    if (packed <= 0) return 0;

    int slot = (packed >> 16) & 0xFFFF;
    int len  = packed & 0xFFFF;
    if (len < (int)sizeof(NetMessage)) return 0;

    if (out_msg) memcpy(out_msg, &tmp, sizeof(NetMessage));
    if (slot >= 0 && slot < NET_MAX_PLAYERS) {
        if (out_peer) *out_peer = &g_peers[slot];
    }
    return 1;
}

#endif /* MB_NET && MB_WEB */
