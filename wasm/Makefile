# Emscripten build for Mine Bombers (browser/WASM).
#
# Requires: emsdk activated in current shell (emcc on PATH).
# Build:    make           -> build/mb.html + mb.js + mb.wasm + mb.data
# Serve:    make serve     -> python -m http.server 8080 in build/
# Clean:    make clean
#
# Phase 1: single-player only (MB_NET undefined, ENet excluded).
# Phase 2: add net_web.c + -DMB_NET -DMB_WEB for WebRTC netplay.

SRC_DIR    = ../mb_c
S3M_DIR    = ../s3mplay/src/libs3m
DATA_DIR   = ../modern
BUILD_DIR  = build

# Source list mirrors mb_c/Makefile minus net.c + ENet + win32 deps.
SRCS = \
    $(SRC_DIR)/main.c $(SRC_DIR)/args.c $(SRC_DIR)/context.c $(SRC_DIR)/error.c \
    $(SRC_DIR)/images.c $(SRC_DIR)/fonts.c $(SRC_DIR)/glyphs.c $(SRC_DIR)/app.c \
    $(SRC_DIR)/game.c $(SRC_DIR)/input.c $(SRC_DIR)/persist.c $(SRC_DIR)/shop.c \
    $(SRC_DIR)/editor.c $(SRC_DIR)/playersel.c $(SRC_DIR)/campaign.c \
    $(SRC_DIR)/menus.c $(SRC_DIR)/netgame.c $(SRC_DIR)/cpu.c \
    $(S3M_DIR)/s3m.c $(S3M_DIR)/s3m_file.c $(S3M_DIR)/s3m_sound.c \
    $(S3M_DIR)/channel.c $(S3M_DIR)/pattern.c $(S3M_DIR)/s3m_info.c

# Phase 2 will add:
# SRCS += $(SRC_DIR)/net_web.c
# CFLAGS += -DMB_NET -DMB_WEB

CFLAGS = -Wall -Wextra -O2 \
    -I$(SRC_DIR) -I$(S3M_DIR) \
    -sUSE_SDL=2 -sUSE_SDL_MIXER=2 -sSDL2_MIXER_FORMATS='["wav"]'

# ASYNCIFY: lets blocking SDL_Delay / nested event loops yield to browser.
# Code uses many `while(running){...SDL_Delay(16);}` patterns -- refactoring
# each to a frame callback is huge. ASYNCIFY trades ~1-2MB binary + ~20% perf
# for keeping existing control flow intact.
LDFLAGS = \
    -sUSE_SDL=2 -sUSE_SDL_MIXER=2 -sSDL2_MIXER_FORMATS='["wav"]' \
    -sASYNCIFY -sASYNCIFY_STACK_SIZE=65536 \
    -sALLOW_MEMORY_GROWTH=1 \
    -sINITIAL_MEMORY=64MB \
    -sEXPORTED_RUNTIME_METHODS='["ccall","cwrap","FS","IDBFS"]' \
    -sEXPORTED_FUNCTIONS='["_main","_malloc","_free"]' \
    -lidbfs.js \
    --preload-file $(DATA_DIR)@/data \
    --shell-file shell.html

TARGET = $(BUILD_DIR)/mb.html

all: $(TARGET)

$(TARGET): $(SRCS) shell.html | $(BUILD_DIR)
	emcc $(CFLAGS) $(SRCS) $(LDFLAGS) -o $(TARGET)

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

serve: $(TARGET)
	cd $(BUILD_DIR) && python -m http.server 8080

clean:
	rm -rf $(BUILD_DIR)

.PHONY: all serve clean
