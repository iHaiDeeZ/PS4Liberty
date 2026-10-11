# PS4Liberty — Liberty Recompiled on PS4

An experimental port of **[Liberty Recompiled](https://github.com/OZORDI/LibertyRecomp)** (the
static recompilation of the Xbox 360 version of Grand Theft Auto IV) to jailbroken PS4 consoles
running GoldHEN. It is built with the OpenOrbis toolchain and renders through Mesa's RADV Vulkan
driver on the PS4 GPU.

> **Status: discontinued (October 2026).** The game boots, installs, loads and is playable in
> story mode on real hardware, but it runs at **about 5–6 fps in gameplay** (60 fps on loading
> screens). This code is published as a reference for anyone who wants to continue. Credits for
> everyone this builds on are in [CREDITS.md](CREDITS.md).

**This project does not include any game files.** You need your own copy of the game (USA Xbox
360 disc + title update 8).

## What works

- Boot, first-run install of the game files from `/data/LibertyRecomp/source/`, loading screens,
  intro cutscenes and free-roam gameplay (tested up to the "Drive to Roman's place" mission).
- Audio (XMA decoding, can stutter at this frame rate) and DualShock 4 input.
- Colours match the PC build.

## What doesn't

- **Performance.** The renderer translates every Xbox 360 draw into Vulkan on one render thread
  and spends about 90 µs of CPU time per draw on the PS4's 1.6 GHz Jaguar cores. A gameplay frame
  has about 2,000–2,600 draws, so the render thread alone needs about 180 ms per frame. The
  game's own command-building thread runs at about 80% of a core as well, which would cap the game
  near 10 fps even with an infinitely fast render thread. The PC build runs 15–30 fps on a desktop
  CPU about 4–5× faster per core, so 5–6 fps is roughly what the hardware ratio predicts.
  Reaching 25–30 fps would need a much cheaper per-draw translation path (months of renderer
  work), not settings.
- Closing the app can crash.

## Install

1. Install the PKG with GoldHEN's package installer.
2. Copy your own game files to the console over FTP into `/data/LibertyRecomp/source/`:
   - `game.iso` — the **USA** disc image (or `game/` with the extracted disc), and
   - `default.xexp` — **title update 8** (or `update/` with the title update package).
   - Optional: `tlad/`, `tbogt/` for the episodes.
3. Start the app. On first launch it validates the sources and installs them to
   `/data/LibertyRecomp/game` (needs roughly twice the game's size free).

   Alternatively, upload a tree already installed by the PC build with
   [`scripts/upload-ps4-game.py`](scripts/upload-ps4-game.py).

## Settings without rebuilding: `/data/LibertyRecomp/env.txt`

`NAME=value` lines, read at start-up. Any cvar can be set as `REX_<UPPERCASE CVAR NAME>`. See
[`env.txt.example`](env.txt.example). The performance switches added for the PS4:

| Setting | Default on PS4 | Effect |
|---|---|---|
| `REX_GTA4_NATIVE_SHADOW_UPDATE_INTERVAL` | 2 | Redraw shadow cascades every N frames (0 = never) |
| `REX_GTA4_NATIVE_REFLECTION_UPDATE_INTERVAL` | 2 | Redraw water / environment / interior / mirror reflections every N frames |
| `REX_GTA4_NATIVE_PIPELINE_PREWARM` | true | `false` skips a duplicate per-draw pipeline lookup (no benefit on one thread) |

## Diagnostics

All files are in `/data/LibertyRecomp/`:

- `boot-trace.log` — start-up milestones, heap growth, `abort()` callers.
- `crash.log` — fatal signals with code addresses relative to the ELF's `.text`. Symbolize with
  `llvm-symbolizer --obj=<unstripped ELF>`.
- `memory.log` — every 5 s: memory, sleep / file read / wait counters, rendered frames and their CPU
  time (`frame-us`), commands skipped by the update intervals (`dropped`), draws per frame
  (`draws`), render-worker idle time, and the busiest threads with their CPU share (`threads:`).
- Create an empty file `dump-stacks` (over FTP) and every thread's stack is appended to
  `stacks.txt` within a second. Repeat it to sample where the time goes.
- With the profiler enabled in `env.txt` (see the example), an empty `profile-start` file starts
  a native renderer capture into `Diagnostics/`.

## Building (WSL Ubuntu)

Prerequisites:

- [PacBrew](https://github.com/PacBrew)'s OpenOrbis at `/opt/pacbrew/ps4/openorbis`, plus clang/lld 21.
- A [DolphinPS4](https://github.com/iHaiDeeZ/DolphinPS4) build tree in `~/dolphinps4-build`: its
  LLVM 21 libc++ for the PS4, its libc header overlay and its Mesa RADV port.
- Mesa: a checkout of DolphinPS4's Mesa with [`patches/mesa-aco-mubuf-addr64.patch`](patches/mesa-aco-mubuf-addr64.patch)
  applied, configured with DolphinPS4's meson cross file and built into `~/libertyrecomp-ps4-build/mesa-build`
  (only the static libraries listed in `cmake/PS4Runtime.cmake` are needed). On GFX7, ACO reaches
  global memory with FLAT instructions, which hang the PS4 GPU; the patch uses MUBUF addr64
  instead, as on GFX6.
- The source tree at `~/libertyrecomp-ps4-build/src/LibertyRecomp` (this repository, branch
  `ps4-port`, with submodules). Apply [`patches/rexglue-ffmpeg-config-ps4.patch`](patches/rexglue-ffmpeg-config-ps4.patch)
  inside `glue/rexglue-sdk-main/thirdparty/FFmpeg`.

Then:

```bash
bash ps4/scripts/setup-toolchain.sh        # once: overlay toolchain, host shader tools, DXC
bash ps4/scripts/build-ps4.sh              # configure + build
bash ps4/scripts/package-ps4.sh 01.00      # -> fake PKG (bump the version for every install)
```

## How the port works (pointers for whoever continues)

- **PS4 runtime** (`os/ps4/runtime/`): process start-up, `malloc` on dlmalloc in 512 MiB of
  flexible memory plus overflow heaps in direct memory, crash handler and heartbeat, thread
  registry and stack dumps, activity counters (`ps4_stackdump.cpp`), libc gaps (`fma` on a CPU
  without FMA, and others).
- **Platform layer in the SDK**: `*_ps4.cpp` files under `glue/rexglue-sdk-main/src/` for memory
  mapping (4.5 GiB guest address space, 16 KiB pages), threads, files, logging, exceptions,
  dynamic libraries and the Vulkan WSI on the PS4's video output (`vulkan_wsi_ps4.cpp`, which also
  hides the launch splash after the first flip).
- **Guest address offset**: on the PS4 build, physical addresses at or above `0xE0000000` get the
  same 0x1000 offset as on Windows and macOS (`gta4_init.h`, `xmemory.h`). Without it, XMA audio
  contexts read empty memory and the game stalls on the loading screen.
- **Performance mode** (`graphics_system.cpp`): `DropForUpdateInterval()` drops shadow and
  reflection pass commands on skipped frames, right after their shader-constant deltas are
  applied. The passes are identified by the game's GPU pass markers, which are always on in PS4
  builds (`gta4_gpu_pass_context.h`).
- The next steps that looked worthwhile: a shorter draw distance (fewer draws for both busy
  threads), cheaper command allocation, and splitting the render worker's intake and recording
  across two threads. See the measurements above for why none of them reaches 30 fps alone.
