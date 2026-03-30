#ifndef NETGAME_H
#define NETGAME_H
#include "app.h"

#ifdef MB_NET
void app_run_netgame(App* app, ApplicationContext* ctx);
#endif

#endif
