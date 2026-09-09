#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "allocator.h"
#include "trealla.h"

// Which stripe an allocation was counted against is packed into the
// top bits of size rather than given a field of its own: long double is
// 8 bytes on arm64 and every other member is 8 too, so a separate
// unsigned would take this union from 8 bytes to 16 - an extra 8 bytes
// on every allocation in the process, which is a poor trade for
// bookkeeping nobody reads.
//
// It has to be per allocation and not per thread: a block allocated on
// one thread is routinely freed on another, and crediting the free to
// the freeing thread's stripe would let stripes drift arbitrarily far
// apart, taking their peaks with them.

typedef union allocation_header_ {
	struct {
		size_t size;
	} info;
	long double align_long_double;
	void *align_pointer;
	uint64_t align_u64;
} allocation_header;

static void *default_malloc(void *context, size_t size)
{
	(void)context;
	return malloc(size);
}

static void *default_realloc(void *context, void *ptr, size_t size)
{
	(void)context;
	return realloc(ptr, size);
}

static void default_free(void *context, void *ptr)
{
	(void)context;
	free(ptr);
}

static pl_allocator s_allocator = {
	.struct_size = sizeof(pl_allocator),
	.context = NULL,
	.malloc_fn = default_malloc,
	.realloc_fn = default_realloc,
	.free_fn = default_free,
};

static int s_locked;

// The accounting below is reporting only - nothing in the engine reads
// it to decide anything - but it used to be four process-wide counters
// hit by every single malloc and free: an add to the byte count, an add
// to the allocation count, a CAS loop on the peak, and an
// unconditional store to s_locked. Atomic read-modify-writes on shared
// cache lines do not show up as lock contention, in sys time or as
// spinning; they simply make every allocation wait its turn for the
// line. On samples/skynet_mixed.pl - millions of allocations across
// several threads - that alone was worth 1.5x at four threads and 2.1x
// at ten, and was what stopped ten threads from beating one.
//
// So the counters are striped: a thread picks a stripe once and keeps
// it, and pl_get_allocator_stats() sums them. current_bytes,
// allocation_count and failure_count stay exact - a sum of stripes is
// the same number, and unsigned wraparound in a single stripe cancels
// in the sum even for the traffic that crosses stripes.
//
// peak_bytes is the one that cannot: a true process-wide peak needs the
// process-wide total on every allocation, which is exactly the read
// being removed. What is reported instead is the sum of per-stripe
// peaks - an upper bound, and an exact answer whenever only one stripe
// is in use. That covers every consumer this has: pl_get_allocator_stats()
// is called from samples/embed.c, samples/allocator.c,
// samples/freestanding.c and ports/arduino-nano-esp32, all single
// threaded. A program with enough threads to blur the peak is one with
// nothing reading it.

// Six bits of the size word name the stripe, which is why there are 64
// of them. Only where size_t is 64 bits: that leaves 2^58-1 as the
// largest single allocation, which is not a limit anything can reach,
// where 32 bits would leave 64MB, which is. A 32-bit target keeps the
// single unstriped counter it always had - those are the freestanding
// and embedded builds, single threaded, with nothing to contend.

#if USE_THREADS && (SIZE_MAX > 0xFFFFFFFFu)
#define ALLOC_STRIPE_BITS 6
#else
#define ALLOC_STRIPE_BITS 0
#endif

#define ALLOC_STRIPES (1u << ALLOC_STRIPE_BITS)
#define ALLOC_SIZE_BITS ((sizeof(size_t) * 8) - ALLOC_STRIPE_BITS)

#if ALLOC_STRIPE_BITS
#define ALLOC_MAX_SIZE ((((size_t)1) << ALLOC_SIZE_BITS) - 1)
#define ALLOC_STRIPE_PAD 128				// two cache lines on Apple silicon
#else
#define ALLOC_MAX_SIZE SIZE_MAX
#define ALLOC_STRIPE_PAD (4 * sizeof(size_t))
#endif

typedef struct {
	size_t current_bytes;
	size_t peak_bytes;
	size_t allocation_count;
	size_t failure_count;
	char pad[ALLOC_STRIPE_PAD - (4 * sizeof(size_t))];
} alloc_stripe;

static alloc_stripe s_stripes[ALLOC_STRIPES];

static size_t counter_load(const size_t *counter)
{
	return __atomic_load_n(counter, __ATOMIC_RELAXED);
}

static void counter_add(size_t *counter, size_t amount)
{
	__atomic_add_fetch(counter, amount, __ATOMIC_RELAXED);
}

static void counter_sub(size_t *counter, size_t amount)
{
	__atomic_sub_fetch(counter, amount, __ATOMIC_RELAXED);
}

// Handed out round-robin and remembered for the life of the thread, so
// a thread's own allocations stay on one line. More threads than
// stripes share, which costs contention and nothing else.

#if USE_THREADS
static unsigned s_next_stripe;

static unsigned my_stripe(void)
{
	static _Thread_local unsigned mine;			// 0 = not yet picked

	if (!mine)
		mine = (__atomic_fetch_add(&s_next_stripe, 1, __ATOMIC_RELAXED) % ALLOC_STRIPES) + 1;

	return mine - 1;
}
#else
static unsigned my_stripe(void) { return 0; }
#endif

static void update_peak(alloc_stripe *st, size_t current)
{
	size_t peak = counter_load(&st->peak_bytes);

	while ((current > peak) && !__atomic_compare_exchange_n(&st->peak_bytes,
		&peak, current, false, __ATOMIC_RELAXED, __ATOMIC_RELAXED))
		;
}

static void count_failure(void)
{
	counter_add(&s_stripes[my_stripe()].failure_count, 1);
}

static void header_set(allocation_header *header, size_t size, unsigned idx)
{
#if ALLOC_STRIPE_BITS
	header->info.size = size | (((size_t)idx) << ALLOC_SIZE_BITS);
#else
	(void)idx;
	header->info.size = size;
#endif
}

static size_t header_size(const allocation_header *header)
{
#if ALLOC_STRIPE_BITS
	return header->info.size & ALLOC_MAX_SIZE;
#else
	return header->info.size;
#endif
}

static unsigned header_stripe(const allocation_header *header)
{
#if ALLOC_STRIPE_BITS
	return (unsigned)(header->info.size >> ALLOC_SIZE_BITS);
#else
	(void)header;
	return 0;
#endif
}

// ALLOC_MAX_SIZE, not SIZE_MAX, so a size can never run into the bits
// the stripe is packed into. Nothing can request that much on a 64-bit
// host, and a request that somehow did would get the NULL and the
// counted failure it deserves rather than a corrupted header.

static bool total_size(size_t size, size_t *total)
{
	if ((size > ALLOC_MAX_SIZE) || (size > (SIZE_MAX - sizeof(allocation_header))))
		return false;

	*total = sizeof(allocation_header) + size;
	return true;
}

bool pl_set_allocator(const pl_allocator *allocator)
{
	if (__atomic_load_n(&s_locked, __ATOMIC_ACQUIRE))
		return false;

	if (!allocator) {
		s_allocator = (pl_allocator){
			.struct_size = sizeof(pl_allocator),
			.context = NULL,
			.malloc_fn = default_malloc,
			.realloc_fn = default_realloc,
			.free_fn = default_free,
		};
		return true;
	}

	if ((allocator->struct_size < sizeof(pl_allocator))
		|| !allocator->malloc_fn || !allocator->realloc_fn || !allocator->free_fn)
		return false;

	s_allocator = *allocator;
	return true;
}

void pl_get_allocator_stats(pl_allocator_stats *stats)
{
	if (!stats)
		return;

	size_t current = 0, peak = 0, allocations = 0, failures = 0;

	for (unsigned i = 0; i < ALLOC_STRIPES; i++) {
		current += counter_load(&s_stripes[i].current_bytes);
		peak += counter_load(&s_stripes[i].peak_bytes);
		allocations += counter_load(&s_stripes[i].allocation_count);
		failures += counter_load(&s_stripes[i].failure_count);
	}

	stats->current_bytes = current;
	stats->peak_bytes = peak;
	stats->allocation_count = allocations;
	stats->failure_count = failures;
}

void pl_reset_allocator_peak(void)
{
	for (unsigned i = 0; i < ALLOC_STRIPES; i++)
		__atomic_store_n(&s_stripes[i].peak_bytes,
			counter_load(&s_stripes[i].current_bytes), __ATOMIC_RELAXED);
}

void *tpl_malloc(size_t size)
{
	size_t total;

	// Tested before it is set: the flag only ever goes one way, and an
	// unconditional store put a shared cache line in the path of every
	// allocation in the process to write a value that was already there.

	if (!__atomic_load_n(&s_locked, __ATOMIC_RELAXED))
		__atomic_store_n(&s_locked, 1, __ATOMIC_RELEASE);

	if (!total_size(size, &total)) {
		count_failure();
		return NULL;
	}

	allocation_header *header = s_allocator.malloc_fn(s_allocator.context, total);

	if (!header) {
		count_failure();
		return NULL;
	}

	unsigned idx = my_stripe();
	alloc_stripe *st = &s_stripes[idx];
	header_set(header, size, idx);
	size_t current = __atomic_add_fetch(&st->current_bytes, size, __ATOMIC_RELAXED);
	counter_add(&st->allocation_count, 1);
	update_peak(st, current);
	return header + 1;
}

void *tpl_calloc(size_t count, size_t size)
{
	if (size && (count > (SIZE_MAX / size))) {
		count_failure();
		return NULL;
	}

	size_t bytes = count * size;
	void *ptr = tpl_malloc(bytes);

	if (ptr)
		memset(ptr, 0, bytes);

	return ptr;
}

void *tpl_realloc(void *ptr, size_t size)
{
	if (!ptr)
		return tpl_malloc(size);

	if (!size) {
		tpl_free(ptr);
		return NULL;
	}

	size_t total;

	if (!total_size(size, &total)) {
		count_failure();
		return NULL;
	}

	allocation_header *old_header = (allocation_header*)ptr - 1;
	size_t old_size = header_size(old_header);
	unsigned idx = header_stripe(old_header);
	allocation_header *header = s_allocator.realloc_fn(s_allocator.context,
		old_header, total);

	if (!header) {
		count_failure();
		return NULL;
	}

	// Stays on the stripe it was first counted against, wherever the
	// block itself ends up and whichever thread is doing the resizing.

	alloc_stripe *st = &s_stripes[idx];
	header_set(header, size, idx);
	size_t current;

	if (size >= old_size)
		current = __atomic_add_fetch(&st->current_bytes, size - old_size, __ATOMIC_RELAXED);
	else
		current = __atomic_sub_fetch(&st->current_bytes, old_size - size, __ATOMIC_RELAXED);

	counter_add(&st->allocation_count, 1);
	update_peak(st, current);
	return header + 1;
}

void tpl_free(void *ptr)
{
	if (!ptr)
		return;

	allocation_header *header = (allocation_header*)ptr - 1;
	counter_sub(&s_stripes[header_stripe(header)].current_bytes, header_size(header));
	s_allocator.free_fn(s_allocator.context, header);
}

char *tpl_strdup(const char *src)
{
	size_t len = strlen(src) + 1;
	char *dst = tpl_malloc(len);

	if (dst)
		memcpy(dst, src, len);

	return dst;
}

void pl_free(void *ptr)
{
	tpl_free(ptr);
}
