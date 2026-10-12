#ifndef JGR_DIAGNOSTICS_H
#define JGR_DIAGNOSTICS_H
/* The game Makefile disables detailed performance instrumentation for normal
   play. SELECT's optional FPS/status overlay and coarse frame clocks remain
   available in every build; they must not be gated by this setting.
   Standalone diagnostic harnesses retain their existing behavior unless
   explicitly testing the diagnostics-off build. */
#ifndef JGR_DIAGNOSTICS
#define JGR_DIAGNOSTICS 1
#endif
/* Release packages do not probe development fixture files on Memory Stick.
   Isolated regression builds may explicitly enable them without enabling
   per-frame performance instrumentation. The SELECT overlay is independent. */
#ifndef JGR_TEST_FIXTURES
#define JGR_TEST_FIXTURES (JGR_DIAGNOSTICS || !JGR_PORTABLE_ASSETS)
#endif
#if JGR_DIAGNOSTICS
#define JGR_PROFILE_CLOCK() sceKernelGetSystemTimeLow()
#else
#define JGR_PROFILE_CLOCK() 0u
#endif
#endif
