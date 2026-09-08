#ifndef VERSION
#define VERSION "v0.0.0"
#endif

const char *g_version = VERSION;

// For current_prolog_flag(compiled_at, When). The link rule rebuilds this
// object every time, so the stamp is the time of the build that produced
// the binary, not of whenever this file last changed.

const char *g_compiled_at = __DATE__ ", " __TIME__;
