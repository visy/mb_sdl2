CC = gcc
SDL2_DIR = SDL2-2.28.5/x86_64-w64-mingw32
SDL2_MIXER_DIR = SDL2_mixer-2.6.3/x86_64-w64-mingw32
S3MPLAY_DIR = ../s3mplay/src/libs3m

CFLAGS = -Wall -Wextra -O2 -I. -I$(SDL2_DIR)/include -I$(SDL2_DIR)/include/SDL2 -I$(SDL2_MIXER_DIR)/include -I$(SDL2_MIXER_DIR)/include/SDL2 -I$(S3MPLAY_DIR)
LDFLAGS = -L$(SDL2_DIR)/lib -L$(SDL2_MIXER_DIR)/lib -lmingw32 -lSDL2main -lSDL2 -lSDL2_mixer

HDRS = context.h args.h images.h error.h app.h fonts.h glyphs.h $(S3MPLAY_DIR)/s3m.h $(S3MPLAY_DIR)/s3m_intern.h $(S3MPLAY_DIR)/channel.h $(S3MPLAY_DIR)/pattern.h

SRCS = main.c args.c context.c error.c images.c fonts.c glyphs.c app.c \
       $(S3MPLAY_DIR)/s3m.c $(S3MPLAY_DIR)/s3m_file.c $(S3MPLAY_DIR)/s3m_sound.c \
       $(S3MPLAY_DIR)/channel.c $(S3MPLAY_DIR)/pattern.c $(S3MPLAY_DIR)/s3m_info.c
OBJS = main.o args.o context.o error.o images.o fonts.o glyphs.o app.o game.o \
       s3m.o s3m_file.o s3m_sound.o channel.o pattern.o s3m_info.o
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

clean:
	del /f /q *.o $(TARGET)
