# Credits

PS4Liberty is a thin PS4 layer on top of other people's work. Thank you to everyone below.

## The PS4 port

**ShiroKlein** ([iHaiDeeZ](https://github.com/iHaiDeeZ)) ported Liberty Recompiled to the PS4: the
PS4 runtime and platform layer, the RADV fixes, the performance mode and all testing on real
hardware.

## Liberty Recompiled

**[Liberty Recompiled](https://github.com/OZORDI/LibertyRecomp)** by **Prince Ozordi
([OZORDI](https://github.com/OZORDI))** and contributors is the static recompilation of GTA IV
that this port runs, licensed GPL-3.0 (see [`COPYING`](../COPYING)). It is a fork of
**[MarathonRecomp](https://github.com/sonicnext-dev/MarathonRecomp)** by the sonicnext-dev team.
Its contributors as GitHub lists them, most active first:

[hyperbx](https://github.com/hyperbx) · [blueskythlikesclouds](https://github.com/blueskythlikesclouds) · [OZORDI](https://github.com/OZORDI) · [IsaacMarovitz](https://github.com/IsaacMarovitz) · [ga2mer](https://github.com/ga2mer) · [squidbus](https://github.com/squidbus) · [DarioSamo](https://github.com/DarioSamo) · [ReimousTH](https://github.com/ReimousTH) · [DeaTh-G](https://github.com/DeaTh-G) · [Sajidur78](https://github.com/Sajidur78) · [RagdollClash](https://github.com/RagdollClash) · [thesupersonic16](https://github.com/thesupersonic16) · [NextinMono](https://github.com/NextinMono) · [BlueSpeedsterYT](https://github.com/BlueSpeedsterYT) · [AL2009man](https://github.com/AL2009man) · [dcvz](https://github.com/dcvz) · [jamessturges](https://github.com/jamessturges) · [PTKay](https://github.com/PTKay) · [JaceCear](https://github.com/JaceCear) · [Refragg](https://github.com/Refragg) · [Kitzuku](https://github.com/Kitzuku) · [Kuid0us](https://github.com/Kuid0us) · [Lorsoen](https://github.com/Lorsoen) · [niltonperimneto](https://github.com/niltonperimneto) · [Pollot](https://github.com/Pollot) · [RadiantDerg](https://github.com/RadiantDerg) · [dakrk](https://github.com/dakrk) · [got4n](https://github.com/got4n)

## ReXGlue SDK

**[ReXGlue](https://github.com/rexglue/rexglue-sdk)** by **Tom Clay ([tomcl7](https://github.com/tomcl7))**
and contributors converts the Xbox 360 PowerPC code into C++ and provides the runtime
([sonicnext-dev fork](https://github.com/sonicnext-dev/rexglue-sdk), BSD-style licence in
[`glue/rexglue-sdk-main/LICENSE`](../glue/rexglue-sdk-main/LICENSE)). Contributors:

[tomcl7](https://github.com/tomcl7) · [Graine25](https://github.com/Graine25) · [Loreaxe](https://github.com/Loreaxe) · [YoshiCrystal9](https://github.com/YoshiCrystal9) · [sanjay900](https://github.com/sanjay900) · [Mystixor](https://github.com/Mystixor) · [helgot](https://github.com/helgot) · [birabittoh](https://github.com/birabittoh) · [Creesic](https://github.com/Creesic) · [twist84](https://github.com/twist84) · [mrcmunir](https://github.com/mrcmunir) · [goldislead](https://github.com/goldislead) · [SolarCookies](https://github.com/SolarCookies) · [josephl70](https://github.com/josephl70) · [catuhana](https://github.com/catuhana) · [jnackmclain](https://github.com/jnackmclain) · [ihatecompvir](https://github.com/ihatecompvir) · [etonedemid](https://github.com/etonedemid) · [TbyDtch](https://github.com/TbyDtch) · [Azkali](https://github.com/Azkali) · [Roxxsen](https://github.com/Roxxsen) · [kronflux](https://github.com/kronflux) · [rapidsamphire](https://github.com/rapidsamphire) · [Natan822](https://github.com/Natan822) · [Minositu](https://github.com/Minositu) · [Fancy2209](https://github.com/Fancy2209) · [CradleKing24](https://github.com/CradleKing24)

Portions of ReXGlue are derived from **[Xenia](https://xenia.jp)** by Ben Vanik and the Xenia
project contributors.

Static recompilation in this space was inspired by **[N64: Recompiled](https://github.com/N64Recomp/N64Recomp)**.

## Libraries and tools

| Project | Used for | Licence |
|---|---|---|
| [Mesa](https://mesa3d.org) (RADV, ACO) | Vulkan driver on the PS4 GPU, via [DolphinPS4](https://github.com/iHaiDeeZ/DolphinPS4)'s PS4 port of it, plus [`patches/mesa-aco-mubuf-addr64.patch`](patches/mesa-aco-mubuf-addr64.patch) | MIT and per-file |
| [OpenOrbis PS4 Toolchain](https://github.com/OpenOrbis/OpenOrbis-PS4-Toolchain) | C runtime, PS4 headers and packaging tools | see project |
| [PacBrew](https://github.com/PacBrew) | The PS4 toolchain distribution used for builds | see project |
| [LLVM](https://llvm.org) | clang, lld and libc++ | Apache-2.0 with LLVM exception |
| [dlmalloc](https://gee.cs.oswego.edu/dl/html/malloc.html) by Doug Lea | Memory allocator (`os/ps4/runtime/third_party/dlmalloc`) | CC0 |
| [FFmpeg](https://ffmpeg.org) | XMA audio decoding | LGPL-2.1+ |
| [SDL](https://libsdl.org) | Input and windowing | zlib |
| [GoldHEN](https://github.com/GoldHEN/GoldHEN) | Homebrew enabler the app runs under | see project |

The other libraries Liberty Recompiled and ReXGlue use are listed in their own repositories and
`thirdparty/` folders.

## Disclaimer

This is a fan-made project with no ties to Rockstar Games, Take-Two Interactive, Microsoft or
Sony Interactive Entertainment. Grand Theft Auto is a trademark of Take-Two Interactive;
PlayStation and PS4 are trademarks of Sony Interactive Entertainment. No game files are included;
please only play games you own.
