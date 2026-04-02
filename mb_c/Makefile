CC = gcc
SDL2_DIR = SDL2-2.28.5/x86_64-w64-mingw32
SDL2_MIXER_DIR = SDL2_mixer-2.6.3/x86_64-w64-mingw32
S3MPLAY_DIR = ../s3mplay/src/libs3m
ENET_DIR = enet_src

# MB_NET enables networking (enet). Remove -DMB_NET to build without network
# support; net.c and the enet source files will be excluded automatically.
CFLAGS = -Wall -Wextra -O2 -DMB_NET -I. -I$(SDL2_DIR)/include -I$(SDL2_DIR)/include/SDL2 -I$(SDL2_MIXER_DIR)/include -I$(SDL2_MIXER_DIR)/include/SDL2 -I$(S3MPLAY_DIR) -I$(ENET_DIR)/include
LDFLAGS = -L$(SDL2_DIR)/lib -L$(SDL2_MIXER_DIR)/lib -lmingw32 -lSDL2main -lSDL2 -lSDL2_mixer -lws2_32 -lwinmm

HDRS = context.h args.h images.h error.h app.h fonts.h glyphs.h input.h game.h net.h \
       compat.h persist.h shop.h editor.h playersel.h campaign.h menus.h netgame.h cpu.h \
       $(S3MPLAY_DIR)/s3m.h $(S3MPLAY_DIR)/s3m_intern.h $(S3MPLAY_DIR)/channel.h $(S3MPLAY_DIR)/pattern.h

SRCS = main.c args.c context.c error.c images.c fonts.c glyphs.c app.c game.c input.c net.c \
       persist.c shop.c editor.c playersel.c campaign.c menus.c netgame.c cpu.c \
       $(S3MPLAY_DIR)/s3m.c $(S3MPLAY_DIR)/s3m_file.c $(S3MPLAY_DIR)/s3m_sound.c \
       $(S3MPLAY_DIR)/channel.c $(S3MPLAY_DIR)/pattern.c $(S3MPLAY_DIR)/s3m_info.c \
       $(ENET_DIR)/callbacks.c $(ENET_DIR)/compress.c $(ENET_DIR)/host.c $(ENET_DIR)/list.c \
       $(ENET_DIR)/packet.c $(ENET_DIR)/peer.c $(ENET_DIR)/protocol.c $(ENET_DIR)/win32.c
OBJS = main.o args.o context.o error.o images.o fonts.o glyphs.o app.o game.o input.o net.o \
       persist.o shop.o editor.o playersel.o campaign.o menus.o netgame.o cpu.o \
       s3m.o s3m_file.o s3m_sound.o channel.o pattern.o s3m_info.o \
       enet_callbacks.o enet_compress.o enet_host.o enet_list.o \
       enet_packet.o enet_peer.o enet_protocol.o enet_win32.o
TARGET = mb_c.exe

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(OBJS) -o $(TARGET) $(LDFLAGS)

# Force everything to depend on all headers for safety
%.o: %.c $(HDRS)
	$(CC) $(CFLAGS) -c $< -o $@

s3m.o: $(S3MPLAY_DIR)/s3m.c $(HDRS)
	$(CC) $(CFLAGS) -c $< -o $@

s3m_file.o: $(S3MPLAY_DIR)/s3m_file.c $(HDRS)
	$(CC) $(CFLAGS) -c $< -o $@

s3m_sound.o: $(S3MPLAY_DIR)/s3m_sound.c $(HDRS)
	$(CC) $(CFLAGS) -c $< -o $@

channel.o: $(S3MPLAY_DIR)/channel.c $(HDRS)
	$(CC) $(CFLAGS) -c $< -o $@

pattern.o: $(S3MPLAY_DIR)/pattern.c $(HDRS)
	$(CC) $(CFLAGS) -c $< -o $@

s3m_info.o: $(S3MPLAY_DIR)/s3m_info.c $(HDRS)
	$(CC) $(CFLAGS) -c $< -o $@

enet_callbacks.o: $(ENET_DIR)/callbacks.c $(HDRS)
	$(CC) $(CFLAGS) -c $< -o $@

enet_compress.o: $(ENET_DIR)/compress.c $(HDRS)
	$(CC) $(CFLAGS) -c $< -o $@

enet_host.o: $(ENET_DIR)/host.c $(HDRS)
	$(CC) $(CFLAGS) -c $< -o $@

enet_list.o: $(ENET_DIR)/list.c $(HDRS)
	$(CC) $(CFLAGS) -c $< -o $@

enet_packet.o: $(ENET_DIR)/packet.c $(HDRS)
	$(CC) $(CFLAGS) -c $< -o $@

enet_peer.o: $(ENET_DIR)/peer.c $(HDRS)
	$(CC) $(CFLAGS) -c $< -o $@

enet_protocol.o: $(ENET_DIR)/protocol.c $(HDRS)
	$(CC) $(CFLAGS) -c $< -o $@

enet_win32.o: $(ENET_DIR)/win32.c $(HDRS)
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	del /f /q *.o $(TARGET)
