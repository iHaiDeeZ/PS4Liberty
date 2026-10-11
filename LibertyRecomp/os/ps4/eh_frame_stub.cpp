// PS4 .eh_frame bracket symbols.
//
// libunwind (in the OpenOrbis libc++ archives) references __eh_frame_start/end
// and __eh_frame_hdr_start/end. The canonical OpenOrbis link.x defines all four
// unconditionally around KEEP(*(.eh_frame)) / KEEP(*(.eh_frame_hdr)), so no
// definitions are needed here. This translation unit is intentionally empty and
// is kept only because the PS4 source list in LibertyRecomp/CMakeLists.txt
// names it; add weak fallbacks here if the build ever moves off link.x.
