// Structs by value for ../ffi_struct_byvalue.sh, each with gaps the C compiler pads.

#include <stdint.h>
#include <string.h>

struct pad { uint8_t a; double b; };
struct inner { uint8_t x; double y; };
struct outer { int32_t n; struct inner in; uint8_t z; };

double pad_sum(struct pad p) { return p.a + p.b; }
int32_t pad_then_int(struct pad p, int32_t k) { return k * 10 + p.a; }
double two_pads(struct pad p, struct pad q) { return p.a + p.b + 10 * (q.a + q.b); }
struct pad make_pad(uint8_t a, double b) { struct pad p = {a, b}; return p; }
double outer_sum(struct outer o, int32_t k) { return o.n + o.in.x + o.in.y + o.z + k * 1000.0; }
void outer_out(struct outer o, int32_t *out) { *out = o.z; }
struct outer make_outer(int32_t n, uint8_t x, double y, uint8_t z) { struct outer o = {n, {x, y}, z}; return o; }

struct named { uint8_t tag; const char *name; };
int32_t named_len(struct named s) { return (int32_t)strlen(s.name) + s.tag; }
int32_t named_len_or_null(struct named s) { return s.name ? (int32_t)strlen(s.name) : -1; }
const char *greeting(void) { return "hello"; }
