#include "args.h"
#include "context.h"
#include "app.h"
#include <stdio.h>

int main(int argc, char** argv) {
    Args args;
    parse_args(argc, argv, &args);

    printf("Starting MineBombers Reloaded in C...\n");
    printf("Game Directory: %s\n", args.path);
    printf("Campaign Mode: %s\n", args.campaign_mode ? "true" : "false");

    ApplicationContext ctx;
    context_init(&ctx, args.path);

    App app;
    if (!app_init(&app, &ctx)) {
        fprintf(stderr, "Failed to initialize application assets.\n");
        context_destroy(&ctx);
        return 1;
    }

    app_run_main_menu(&app, &ctx, args.campaign_mode);

    app_destroy(&app);
    context_destroy(&ctx);
    return 0;
}
