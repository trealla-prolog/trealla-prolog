// Structs by value for ../ffi_struct_byvalue.sh, each with gaps the C compiler pads.

#include <stdint.h>

struct pad { uint8_t a; double b; };
struct inner { uint8_t x; double y; };
struct outer { int32_t n; struct inner in; uint8_t z; };

double pad_sum(struct pad p) { return p.a + p.b; }
int32_t pad_then_int(struct pad p, int32_t k) { return k * 10 + p.a; }
double two_pads(struct pad p, struct pad q) { return p.a + p.b + 10 * (q.a + q.b); }
struct pad make_pad(uint8_t a, double b) { struct pad p = {a, b}; return p; }
double outer_sum(struct outer o, int32_t k) { return o.n + o.in.x + o.in.y + o.z + k * 1000.0; }
void outer_out(struct outer o, int32_t *out) { *out = o.z; }
