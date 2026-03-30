#ifndef PERSIST_H
#define PERSIST_H
#include "app.h"

void options_load(GameOptions* o, const char* game_dir);
void options_save(const GameOptions* o, const char* game_dir);
void roster_load(PlayerRoster* roster, const char* game_dir);
void roster_save(const PlayerRoster* roster, const char* game_dir);
void identities_load(PlayerRoster* roster, const char* game_dir);
void identities_save(const PlayerRoster* roster, const char* game_dir);
void roster_update_stats(RosterInfo* dest, const GameStats* src, bool tournament_win);
void highscores_load(HighScoreEntry* hs, const char* game_dir);
void highscores_save(const HighScoreEntry* hs, const char* game_dir);
int highscores_insert(HighScoreEntry* hs, const char* name, int level, uint32_t money);
void load_registered(const char* game_dir, char* out_buf, size_t max_len);

#endif
