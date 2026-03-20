#ifdef MB_NET

#include "net.h"
#include <stdio.h>
#include <string.h>

bool net_init(void) {
    if (enet_initialize() != 0) {
        fprintf(stderr, "net: enet_initialize failed\n");
        return false;
    }
    return true;
}

void net_shutdown(void) {
    enet_deinitialize();
}

bool net_host_create(NetContext* ctx, int port) {
    memset(ctx, 0, sizeof(NetContext));
    ctx->is_server = true;
    ctx->local_player = 0;
    ctx->player_count = 1;

    ENetAddress address;
    address.host = ENET_HOST_ANY;
    address.port = (enet_uint16)port;

    ctx->host = enet_host_create(&address, NET_MAX_PEERS, 1, 0, 0);
    if (!ctx->host) {
        fprintf(stderr, "net: failed to create server host\n");
        return false;
    }
    ctx->connected = true;
    return true;
}

bool net_client_connect(NetContext* ctx, const char* hostname, int port) {
    memset(ctx, 0, sizeof(NetContext));
    ctx->is_server = false;

    ctx->host = enet_host_create(NULL, 1, 1, 0, 0);
    if (!ctx->host) {
        fprintf(stderr, "net: failed to create client host\n");
        return false;
    }

    ENetAddress address;
    enet_address_set_host(&address, hostname);
    address.port = (enet_uint16)port;

    ctx->server_peer = enet_host_connect(ctx->host, &address, 1, 0);
    if (!ctx->server_peer) {
        fprintf(stderr, "net: failed to initiate connection\n");
        enet_host_destroy(ctx->host);
        ctx->host = NULL;
        return false;
    }

    // Wait up to 5 seconds for connection
    ENetEvent event;
    if (enet_host_service(ctx->host, &event, 5000) > 0 &&
        event.type == ENET_EVENT_TYPE_CONNECT) {
        ctx->connected = true;
        return true;
    }

    enet_peer_reset(ctx->server_peer);
    enet_host_destroy(ctx->host);
    ctx->host = NULL;
    ctx->server_peer = NULL;
    fprintf(stderr, "net: connection timed out\n");
    return false;
}

void net_disconnect(NetContext* ctx) {
    if (!ctx->host) return;

    // Send a reliable PLAYER_LEAVE so the remote side knows immediately
    NetMessage leave;
    memset(&leave, 0, sizeof(leave));
    leave.type = NET_MSG_PLAYER_LEAVE;

    if (ctx->is_server) {
        leave.data.player_leave.player_index = 0;
        net_broadcast(ctx, &leave);
        enet_host_flush(ctx->host);
        for (int i = 0; i < NET_MAX_PLAYERS; i++) {
            if (ctx->peers[i]) {
                enet_peer_disconnect(ctx->peers[i], 0);
                ctx->peers[i] = NULL;
            }
        }
    } else if (ctx->server_peer) {
        leave.data.player_leave.player_index = ctx->local_player;
        net_send_to(ctx->server_peer, &leave);
        enet_host_flush(ctx->host);
        enet_peer_disconnect(ctx->server_peer, 0);
    }

    // Service briefly to let ENet transmit the leave + disconnect
    ENetEvent event;
    for (int i = 0; i < 10; i++) {
        if (enet_host_service(ctx->host, &event, 50) <= 0) break;
        if (event.type == ENET_EVENT_TYPE_RECEIVE)
            enet_packet_destroy(event.packet);
        if (event.type == ENET_EVENT_TYPE_DISCONNECT)
            break;
    }

    if (ctx->server_peer) {
        enet_peer_reset(ctx->server_peer);
        ctx->server_peer = NULL;
    }
    enet_host_destroy(ctx->host);
    ctx->host = NULL;
    ctx->connected = false;
}

static void send_packet(ENetPeer* peer, const void* data, size_t len) {
    ENetPacket* packet = enet_packet_create(data, len, ENET_PACKET_FLAG_RELIABLE);
    if (packet) {
        enet_peer_send(peer, 0, packet);
    }
}

void net_send_to(ENetPeer* peer, const NetMessage* msg) {
    send_packet(peer, msg, sizeof(NetMessage));
}

void net_broadcast(NetContext* ctx, const NetMessage* msg) {
    ENetPacket* packet = enet_packet_create(msg, sizeof(NetMessage), ENET_PACKET_FLAG_RELIABLE);
    if (packet) {
        enet_host_broadcast(ctx->host, 0, packet);
    }
}

bool net_slot_active(const NetContext* ctx, int slot) {
    if (slot < 0 || slot >= NET_MAX_PLAYERS) return false;
    if (ctx->is_server) {
        if (slot == 0) return true;
        return ctx->peers[slot] != NULL;
    } else {
        return ctx->player_names[slot][0] != '\0' || slot == ctx->local_player;
    }
}

// Process exactly one network event. Returns 1 if an event was placed in out_msg, 0 if none.
// Call in a loop until it returns 0 to drain all pending events.
int net_poll(NetContext* ctx, NetMessage* out_msg, ENetPeer** out_peer) {
    if (!ctx->host) return 0;

    ENetEvent event;
    while (enet_host_service(ctx->host, &event, 0) > 0) {
        switch (event.type) {
            case ENET_EVENT_TYPE_CONNECT:
                if (ctx->is_server) {
                    // Find free player slot (slot 0 is server)
                    int slot = -1;
                    for (int i = 1; i < NET_MAX_PLAYERS; i++) {
                        if (!ctx->peers[i]) { slot = i; break; }
                    }
                    if (slot < 0) {
                        enet_peer_disconnect_now(event.peer, 0);
                        continue; // keep draining, don't return this event
                    }
                    ctx->peers[slot] = event.peer;
                    event.peer->data = (void*)(intptr_t)slot;
                    ctx->player_count++;

                    // Send SERVER_INFO to new client (name not known yet)
                    NetMessage info_msg;
                    memset(&info_msg, 0, sizeof(info_msg));
                    info_msg.type = NET_MSG_SERVER_INFO;
                    info_msg.data.server_info.player_count = ctx->player_count;
                    info_msg.data.server_info.assigned_index = slot;
                    memcpy(info_msg.data.server_info.player_names,
                           ctx->player_names, sizeof(ctx->player_names));
                    net_send_to(event.peer, &info_msg);

                    // Don't broadcast PLAYER_JOIN yet — wait for client to send name.
                    // Return a synthetic PLAYER_JOIN to caller so server UI can update.
                    if (out_msg) {
                        memset(out_msg, 0, sizeof(NetMessage));
                        out_msg->type = NET_MSG_PLAYER_JOIN;
                        out_msg->data.player_join.player_index = slot;
                        // name is empty — will be filled when client sends PLAYER_JOIN
                    }
                    if (out_peer) *out_peer = event.peer;
                    return 1;
                } else {
                    ctx->connected = true;
                    continue; // internal event, don't return to caller
                }

            case ENET_EVENT_TYPE_DISCONNECT:
                if (ctx->is_server) {
                    int slot = (int)(intptr_t)event.peer->data;
                    if (slot >= 0 && slot < NET_MAX_PLAYERS) {
                        ctx->peers[slot] = NULL;
                        ctx->player_ready[slot] = false;
                        ctx->player_names[slot][0] = '\0';
                        ctx->player_count--;

                        // Broadcast leave
                        NetMessage leave_msg;
                        memset(&leave_msg, 0, sizeof(leave_msg));
                        leave_msg.type = NET_MSG_PLAYER_LEAVE;
                        leave_msg.data.player_leave.player_index = slot;
                        net_broadcast(ctx, &leave_msg);

                        if (out_msg) *out_msg = leave_msg;
                        if (out_peer) *out_peer = event.peer;
                        return 1;
                    }
                } else {
                    ctx->connected = false;
                    if (out_msg) {
                        memset(out_msg, 0, sizeof(NetMessage));
                        out_msg->type = NET_MSG_PLAYER_LEAVE;
                        out_msg->data.player_leave.player_index = -1;
                    }
                    return 1;
                }
                continue;

            case ENET_EVENT_TYPE_RECEIVE:
                if (event.packet->dataLength >= sizeof(NetMessage)) {
                    if (out_msg) memcpy(out_msg, event.packet->data, sizeof(NetMessage));
                    if (out_peer) *out_peer = event.peer;
                    enet_packet_destroy(event.packet);
                    return 1;
                }
                enet_packet_destroy(event.packet);
                continue;

            case ENET_EVENT_TYPE_NONE:
                continue;
        }
    }

    return 0;
}

#endif /* MB_NET */
