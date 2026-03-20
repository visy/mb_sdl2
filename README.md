# MineBombers C Port

A C port of the classic MineBombers game, using SDL2 for rendering and audio.

## Dependencies

- **GCC** (MinGW-w64 on Windows)
- **SDL2** (2.28.5) — windowing, input, rendering
- **SDL2_mixer** (2.6.3) — audio playback
- **ENet** (1.3.18, included in `enet_src/`) — UDP networking for multiplayer

The SDL2 and SDL2_mixer development libraries should be extracted alongside the source:

```
mb_c/
  SDL2-2.28.5/x86_64-w64-mingw32/
  SDL2_mixer-2.6.3/x86_64-w64-mingw32/
  enet_src/
  ...source files...
```

The S3M music player library is expected at `../s3mplay/src/libs3m/` relative to the `mb_c/` directory.

## Building

```bash
cd mb_c
mingw32-make
```

This produces `mb_c.exe`.

To do a clean rebuild:

```bash
mingw32-make clean
mingw32-make
```

### Disabling networking

All network/multiplayer code is guarded by the `MB_NET` preprocessor define. To build without ENet or any networking support, remove `-DMB_NET` from `CFLAGS` in the Makefile and remove the ENet source files from `SRCS`/`OBJS`. The game will compile and run with local multiplayer and campaign mode only.

## Running

```bash
./mb_c.exe -p <path-to-game-data>
```

**Arguments:**

| Flag | Description |
|------|-------------|
| `-w` | Run in windowed mode instead of fullscreen |

The game data directory must contain the original MineBombers 3.11 freeware version asset files (levels, graphics, sounds, music).

## Game Modes

- **Campaign** — Single-player mode. Set players to 1 in options, then start a new game.
- **Local Multiplayer** — 2-4 players on the same machine with configurable controls. Set player count in options.
- **Network Multiplayer** — Press **F1** from the main menu to host or join a LAN game over UDP (port 7777). Requires `MB_NET` build.

## Controls

Controls are configurable per player in INPUT.CFG file.

**In-game keys:**

| Key | Action |
|-----|--------|
| Esc | End round (campaign: quit to menu) |
| F10 | End match and return to menu |

## Project Structure

| File | Purpose |
|------|---------|
| `main.c` | Entry point, argument parsing |
| `context.c/h` | SDL2 initialization, rendering, audio context |
| `app.c/h` | Application state, menus, shop, lobby, game flow |
| `game.c/h` | Core gameplay: world simulation, physics, rendering |
| `input.c/h` | Input mapping and configuration |
| `net.c/h` | ENet networking wrapper and protocol (behind `MB_NET`) |
| `fonts.c/h` | Bitmap font rendering |
| `glyphs.c/h` | Sprite/glyph atlas rendering |
| `images.c/h` | PCX image loading with palette support |
| `args.c/h` | Command-line argument parsing |
| `error.c/h` | Error reporting |

## Multiplayer Networking

The netgame uses a **lockstep deterministic** model:

- All game logic uses a shared deterministic PRNG (xorshift32), seeded identically on all clients via the `GAME_START` message
- Each frame, players send their input to the server, which broadcasts a combined tick to all clients
- All clients execute the same simulation with the same inputs on the same frame, ensuring identical game state
- The shop phase uses a server-authoritative model where the server validates buy/sell actions

Protocol runs over ENet (reliable UDP) on port 7777. All messages use a tagged union format (`NetMessage`).
