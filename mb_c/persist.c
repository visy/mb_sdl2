#include "persist.h"
#include "compat.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void options_load(GameOptions* o, const char* game_dir) {
    char path[MAX_PATH];
    snprintf(path, sizeof(path), "%s%cOPTIONS.CFG", game_dir, PATH_SEP);
    FILE* f = fopen(path, "rb");
    if (!f) return;
    uint8_t buf[17];
    if (fread(buf, 1, 17, f) != 17) { fclose(f); return; }
    fclose(f);
    o->players = buf[0];
    o->treasures = buf[1];
    o->rounds = buf[2] | (buf[3] << 8);
    o->cash = buf[4] | (buf[5] << 8);
    uint32_t ticks = buf[6] | (buf[7] << 8) | (buf[8] << 16) | (buf[9] << 24);
    o->round_time_secs = (uint16_t)(ticks * 10 / 182);
    o->speed = buf[10] | (buf[11] << 8);
    o->darkness = buf[12] != 0;
    o->free_market = buf[13] != 0;
    o->selling = buf[14] != 0;
    o->win_by_money = buf[15] == 0;
    o->bomb_damage = buf[16];
    if (o->players > 4) o->players = 2;
    if (o->bomb_damage > 100) o->bomb_damage = 100;
    if (o->rounds > 55) o->rounds = 55;
    if (o->treasures > 75) o->treasures = 75;
    if (o->cash > 2650) o->cash = 2650;
    if (o->speed < 50) o->speed = 50;
    if (o->speed > 200) o->speed = 200;
}

void options_save(const GameOptions* o, const char* game_dir) {
    char path[MAX_PATH];
    snprintf(path, sizeof(path), "%s%cOPTIONS.CFG", game_dir, PATH_SEP);
    uint8_t buf[17];
    buf[0] = o->players;
    buf[1] = o->treasures;
    buf[2] = o->rounds & 0xFF;
    buf[3] = (o->rounds >> 8) & 0xFF;
    buf[4] = o->cash & 0xFF;
    buf[5] = (o->cash >> 8) & 0xFF;
    uint32_t ticks = (uint32_t)o->round_time_secs * 182 / 10;
    buf[6] = ticks & 0xFF;
    buf[7] = (ticks >> 8) & 0xFF;
    buf[8] = (ticks >> 16) & 0xFF;
    buf[9] = (ticks >> 24) & 0xFF;
    buf[10] = o->speed & 0xFF;
    buf[11] = (o->speed >> 8) & 0xFF;
    buf[12] = o->darkness ? 1 : 0;
    buf[13] = o->free_market ? 1 : 0;
    buf[14] = o->selling ? 1 : 0;
    buf[15] = o->win_by_money ? 0 : 1;
    buf[16] = o->bomb_damage;
    FILE* f = fopen(path, "wb");
    if (!f) return;
    fwrite(buf, 1, 17, f);
    fclose(f);
}

// ==================== Player Roster (PLAYERS.DAT) ====================

void roster_load(PlayerRoster* roster, const char* game_dir) {
    memset(roster, 0, sizeof(PlayerRoster));
    for (int i = 0; i < MAX_PLAYERS; i++) roster->identities[i] = -1;

    char path[MAX_PATH];
    snprintf(path, sizeof(path), "%s%cPLAYERS.DAT", game_dir, PATH_SEP);
    FILE* f = fopen(path, "rb");
    if (!f) return;
    uint8_t data[ROSTER_MAX * ROSTER_RECORD_SIZE];
    size_t n = fread(data, 1, sizeof(data), f);
    fclose(f);
    if (n != sizeof(data)) return;

    for (int i = 0; i < ROSTER_MAX; i++) {
        const uint8_t* rec = &data[i * ROSTER_RECORD_SIZE];
        if (rec[0] != 0) continue; // non-zero = empty
        roster->entries[i].active = true;
        int len = rec[1]; if (len > 24) len = 24;
        memcpy(roster->entries[i].name, &rec[2], len);
        roster->entries[i].name[len] = '\0';
        const uint8_t* s = &rec[26];
        roster->entries[i].tournaments        = s[0]  | (s[1]<<8)  | (s[2]<<16)  | (s[3]<<24);
        roster->entries[i].tournaments_wins   = s[4]  | (s[5]<<8)  | (s[6]<<16)  | (s[7]<<24);
        roster->entries[i].rounds             = s[8]  | (s[9]<<8)  | (s[10]<<16) | (s[11]<<24);
        roster->entries[i].rounds_wins        = s[12] | (s[13]<<8) | (s[14]<<16) | (s[15]<<24);
        roster->entries[i].treasures_collected= s[16] | (s[17]<<8) | (s[18]<<16) | (s[19]<<24);
        roster->entries[i].total_money        = s[20] | (s[21]<<8) | (s[22]<<16) | (s[23]<<24);
        roster->entries[i].bombs_bought       = s[24] | (s[25]<<8) | (s[26]<<16) | (s[27]<<24);
        roster->entries[i].bombs_dropped      = s[28] | (s[29]<<8) | (s[30]<<16) | (s[31]<<24);
        roster->entries[i].deaths             = s[32] | (s[33]<<8) | (s[34]<<16) | (s[35]<<24);
        roster->entries[i].meters_ran         = s[36] | (s[37]<<8) | (s[38]<<16) | (s[39]<<24);
        memcpy(roster->entries[i].history, &rec[66], ROSTER_HISTORY_SIZE);
    }
}

void roster_save(const PlayerRoster* roster, const char* game_dir) {
    uint8_t data[ROSTER_MAX * ROSTER_RECORD_SIZE];
    memset(data, 0, sizeof(data));
    for (int i = 0; i < ROSTER_MAX; i++) {
        uint8_t* rec = &data[i * ROSTER_RECORD_SIZE];
        if (!roster->entries[i].active) { rec[0] = 1; continue; }
        rec[0] = 0;
        int len = (int)strlen(roster->entries[i].name); if (len > 24) len = 24;
        rec[1] = (uint8_t)len;
        memcpy(&rec[2], roster->entries[i].name, len);
        uint8_t* s = &rec[26];
        const RosterInfo* e = &roster->entries[i];
        uint32_t vals[] = { e->tournaments, e->tournaments_wins, e->rounds, e->rounds_wins,
                            e->treasures_collected, e->total_money, e->bombs_bought,
                            e->bombs_dropped, e->deaths, e->meters_ran };
        for (int v = 0; v < 10; v++) {
            s[v*4+0] = vals[v] & 0xFF;
            s[v*4+1] = (vals[v] >> 8) & 0xFF;
            s[v*4+2] = (vals[v] >> 16) & 0xFF;
            s[v*4+3] = (vals[v] >> 24) & 0xFF;
        }
        memcpy(&rec[66], e->history, ROSTER_HISTORY_SIZE);
    }
    char path[MAX_PATH];
    snprintf(path, sizeof(path), "%s%cPLAYERS.DAT", game_dir, PATH_SEP);
    FILE* f = fopen(path, "wb");
    if (!f) return;
    fwrite(data, 1, sizeof(data), f);
    fclose(f);
}

void identities_load(PlayerRoster* roster, const char* game_dir) {
    char path[MAX_PATH];
    snprintf(path, sizeof(path), "%s%cIDENTIFY.DAT", game_dir, PATH_SEP);
    FILE* f = fopen(path, "rb");
    if (!f) return;
    uint8_t buf[4];
    if (fread(buf, 1, 4, f) == 4) {
        for (int i = 0; i < MAX_PLAYERS; i++)
            roster->identities[i] = buf[i] == 0 ? -1 : (int8_t)(buf[i] - 1);
    }
    fclose(f);
}

void identities_save(const PlayerRoster* roster, const char* game_dir) {
    uint8_t buf[4];
    for (int i = 0; i < MAX_PLAYERS; i++)
        buf[i] = roster->identities[i] < 0 ? 0 : (uint8_t)(roster->identities[i] + 1);
    char path[MAX_PATH];
    snprintf(path, sizeof(path), "%s%cIDENTIFY.DAT", game_dir, PATH_SEP);
    FILE* f = fopen(path, "wb");
    if (!f) return;
    fwrite(buf, 1, 4, f);
    fclose(f);
}

void roster_update_stats(RosterInfo* dest, const GameStats* src, bool tournament_win) {
    if (src->rounds == 0) return;
    uint32_t hlen = ROSTER_HISTORY_SIZE;
    uint32_t hist_idx = dest->tournaments % hlen;
    uint32_t last_idx = (dest->tournaments + hlen - 1) % hlen;
    uint8_t hval = dest->history[last_idx] / 2 + (uint8_t)((129 * src->rounds_wins / src->rounds) / 2);
    dest->tournaments += 1;
    dest->tournaments_wins += tournament_win ? 1 : 0;
    dest->rounds += src->rounds;
    dest->rounds_wins += src->rounds_wins;
    dest->treasures_collected += src->treasures_collected;
    dest->total_money += src->total_money;
    dest->bombs_bought += src->bombs_bought;
    dest->bombs_dropped += src->bombs_dropped;
    dest->deaths += src->deaths;
    dest->meters_ran += src->meters_ran;
    dest->history[hist_idx] = hval;
}

// ==================== High Scores (HIGHSCOR.DAT) ====================

void highscores_load(HighScoreEntry* hs, const char* game_dir) {
    memset(hs, 0, HIGHSCORE_MAX * sizeof(HighScoreEntry));
    char path[MAX_PATH];
    snprintf(path, sizeof(path), "%s%cHIGHSCOR.DAT", game_dir, PATH_SEP);
    FILE* f = fopen(path, "rb");
    if (!f) return;
    uint8_t data[HIGHSCORE_MAX * HIGHSCORE_RECORD_SIZE];
    if (fread(data, 1, sizeof(data), f) != sizeof(data)) { fclose(f); return; }
    fclose(f);
    for (int i = 0; i < HIGHSCORE_MAX; i++) {
        const uint8_t* rec = &data[i * HIGHSCORE_RECORD_SIZE];
        int len = rec[0]; if (len > HIGHSCORE_NAME_LEN) len = HIGHSCORE_NAME_LEN;
        if (len == 0) continue;
        memcpy(hs[i].name, &rec[1], len);
        hs[i].name[len] = '\0';
        hs[i].level = rec[21];
        hs[i].money = rec[22] | (rec[23] << 8) | (rec[24] << 16) | (rec[25] << 24);
    }
}

void highscores_save(const HighScoreEntry* hs, const char* game_dir) {
    uint8_t data[HIGHSCORE_MAX * HIGHSCORE_RECORD_SIZE];
    memset(data, 0, sizeof(data));
    for (int i = 0; i < HIGHSCORE_MAX; i++) {
        uint8_t* rec = &data[i * HIGHSCORE_RECORD_SIZE];
        int len = (int)strlen(hs[i].name); if (len > HIGHSCORE_NAME_LEN) len = HIGHSCORE_NAME_LEN;
        rec[0] = (uint8_t)len;
        memcpy(&rec[1], hs[i].name, len);
        rec[21] = hs[i].level;
        rec[22] = hs[i].money & 0xFF;
        rec[23] = (hs[i].money >> 8) & 0xFF;
        rec[24] = (hs[i].money >> 16) & 0xFF;
        rec[25] = (hs[i].money >> 24) & 0xFF;
    }
    char path[MAX_PATH];
    snprintf(path, sizeof(path), "%s%cHIGHSCOR.DAT", game_dir, PATH_SEP);
    FILE* f = fopen(path, "wb");
    if (!f) return;
    fwrite(data, 1, sizeof(data), f);
    fclose(f);
}

// Insert a new score. Returns rank (0-based) or -1 if not a high score.
int highscores_insert(HighScoreEntry* hs, const char* name, int level, uint32_t money) {
    // Score: higher level wins, then higher money breaks ties
    int pos = HIGHSCORE_MAX;
    for (int i = HIGHSCORE_MAX - 1; i >= 0; i--) {
        if (hs[i].name[0] == '\0' || level > hs[i].level ||
            (level == hs[i].level && money > hs[i].money))
            pos = i;
        else break;
    }
    if (pos >= HIGHSCORE_MAX) return -1;
    // Shift down
    for (int i = HIGHSCORE_MAX - 1; i > pos; i--) hs[i] = hs[i - 1];
    memset(&hs[pos], 0, sizeof(HighScoreEntry));
    snprintf(hs[pos].name, HIGHSCORE_NAME_LEN + 1, "%s", name);
    hs[pos].level = (uint8_t)level;
    hs[pos].money = money;
    return pos;
}

void load_registered(const char* game_dir, char* out_buf, size_t max_len) {
    out_buf[0] = '\0';
    char path[MAX_PATH];
#ifdef _WIN32
    snprintf(path, sizeof(path), "%s\\REGISTER.DAT", game_dir);
#else
    snprintf(path, sizeof(path), "%s/REGISTER.DAT", game_dir);
#endif

    FILE* f = fopen(path, "rb");
    if (!f) return;

    uint8_t len;
    if (fread(&len, 1, 1, f) == 1) {
        if (len < 26 && len < max_len) {
            fread(out_buf, 1, len, f);
            out_buf[len] = '\0';
        }
    }
    fclose(f);
}
