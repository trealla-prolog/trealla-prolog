// Functions returning C bool for ../ffi_bool_return.sh.

#include <stdbool.h>
#include <stdint.h>

struct pt { int32_t x, y; };

bool is_even(int32_t n) { return n % 2 == 0; }
bool is_odd(int32_t n) { return n % 2 != 0; }
bool always(void) { return true; }
bool never(void) { return false; }
bool halve(int32_t n, int32_t *out) { *out = n / 2; return n % 2 == 0; }
bool same_pt(struct pt a, struct pt b) { return a.x == b.x && a.y == b.y; }
bool flip(bool b) { return !b; }
