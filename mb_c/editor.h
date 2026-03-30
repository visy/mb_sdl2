#ifndef EDITOR_H
#define EDITOR_H
#include "app.h"

#define TEXT_UPPER    1
#define TEXT_FILENAME 2

void app_run_editor(App* app, ApplicationContext* ctx);
bool text_entry_dialog(App* app, ApplicationContext* ctx, char* out_buf, int max_len,
                       const char* prompt, const char* initial, int flags);

#endif
