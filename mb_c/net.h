#ifndef NET_H
#define NET_H

#ifdef MB_NET

#include <stdbool.h>
#include <stdint.h>
#include <enet/enet.h>

#define NET_PORT 7777
#define NET_MAX_PEERS 3
#define NET_MAX_PLAYERS 4
#define NET_PLAYER_NAME_LEN 16

typedef enum {
    NET_MSG_SERVER_INFO,
    NET_MSG_PLAYER_JOIN,
    NET_MSG_PLAYER_LEAVE,
    NET_MSG_PLAYER_READY,
    NET_MSG_READY_UPDATE,
    NET_MSG_GAME_START,
    NET_MSG_SHOP_ACTION,
    NET_MSG_SHOP_STATE,
    NET_MSG_SHOP_CURSOR,
    NET_MSG_SHOP_READY,
    NET_MSG_SHOP_ALL_READY,
    NET_MSG_GAME_INPUT,
    NET_MSG_GAME_TICK
} NetMessageType;

typedef enum {
    SHOP_ACT_BUY,
    SHOP_ACT_SELL
} ShopActionType;

// Input flags for netgame lockstep
#define NET_INPUT_UP      1
#define NET_INPUT_DOWN    2
#define NET_INPUT_LEFT    3
#define NET_INPUT_RIGHT   4
#define NET_INPUT_STOP    5
#define NET_INPUT_DIR_MASK  0x07
#define NET_INPUT_ACTION    0x08
#define NET_INPUT_CYCLE     0x10
#define NET_INPUT_REMOTE    0x20
#define NET_INPUT_QUIT      0x40
#define NET_INPUT_GOD       0x80

// --- Individual message structs ---

typedef struct {
    uint16_t cash;
    uint8_t treasures;
    uint16_t rounds;
    uint16_t round_time_secs;
    uint8_t players;
    uint16_t speed;
    uint8_t bomb_damage;
    uint8_t flags; // darkness|free_market|selling|win_by_money packed
    char player_names[NET_MAX_PLAYERS][NET_PLAYER_NAME_LEN];
    int player_count;
    int assigned_index;
} NetServerInfo;

typedef struct {
    int player_index;
    char player_name[NET_PLAYER_NAME_LEN];
} NetPlayerJoin;

typedef struct {
    int player_index;
} NetPlayerLeave;

typedef struct {
    int player_index;
    bool is_ready;
} NetPlayerReady;

typedef struct {
    bool ready[NET_MAX_PLAYERS];
} NetReadyUpdate;

typedef struct {
    uint16_t cash;
    uint8_t treasures;
    uint16_t rounds;
    uint16_t round_time_secs;
    uint8_t players;
    uint16_t speed;
    uint8_t bomb_damage;
    uint8_t flags;
    int selected_levels[128];
    int level_count;
    uint32_t rng_seed;
} NetGameStart;

typedef struct {
    int player_index;
    ShopActionType action;
    int item_index;
} NetShopAction;

typedef struct {
    int player_index;
    uint32_t cash;
    int inventory[27]; // EQUIP_TOTAL
    int cursor;
} NetShopState;

typedef struct {
    int player_index;
    int cursor;
} NetShopCursor;

typedef struct {
    int player_index;
} NetShopReady;

typedef struct {
    int next_round;
} NetShopAllReady;

typedef struct {
    uint32_t frame;
    int player_index;
    uint8_t input;
} NetGameInput;

typedef struct {
    uint32_t frame;
    uint8_t inputs[NET_MAX_PLAYERS];
} NetGameTick;

// --- Tagged union message ---

typedef struct {
    NetMessageType type;
    union {
        NetServerInfo server_info;
        NetPlayerJoin player_join;
        NetPlayerLeave player_leave;
        NetPlayerReady player_ready;
        NetReadyUpdate ready_update;
        NetGameStart game_start;
        NetShopAction shop_action;
        NetShopState shop_state;
        NetShopCursor shop_cursor;
        NetShopReady shop_ready;
        NetShopAllReady shop_all_ready;
        NetGameInput game_input;
        NetGameTick game_tick;
    } data;
} NetMessage;

// --- Net context ---

typedef struct {
    ENetHost* host;
    ENetPeer* server_peer;              // client only: connection to server
    ENetPeer* peers[NET_MAX_PLAYERS];   // server only: connected peers (index = player slot)
    int player_count;
    int local_player;
    bool is_server;
    bool connected;
    char player_names[NET_MAX_PLAYERS][NET_PLAYER_NAME_LEN];
    bool player_ready[NET_MAX_PLAYERS];
} NetContext;

// --- API ---

bool net_init(void);
void net_shutdown(void);

bool net_host_create(NetContext* ctx, int port);
bool net_client_connect(NetContext* ctx, const char* hostname, int port);
void net_disconnect(NetContext* ctx);

// Returns number of events processed
int net_poll(NetContext* ctx, NetMessage* out_msg, ENetPeer** out_peer);

void net_send_to(ENetPeer* peer, const NetMessage* msg);
void net_broadcast(NetContext* ctx, const NetMessage* msg);

bool net_slot_active(const NetContext* ctx, int slot);

#else /* MB_NET not defined */

// Minimal stub so the rest of the project compiles without networking
typedef struct { int _unused; } NetContext;

#endif /* MB_NET */

#endif // NET_H
