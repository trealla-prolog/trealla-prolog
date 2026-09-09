#pragma once

#include "parser.h"
#include "builtins.h"

typedef struct {
	int sep, quote;
	unsigned arity;
	bool trim, numbers, use_strings;
	const char *functor;
} csv;

query *query_create(module *m);
query *query_create_threaded(module *m);
query *query_create_subquery(query *q, cell *instr);
query *query_create_task(query *q, cell *instr);
void query_destroy(query *q);
void release_oom_reserve(query *q);

bool push_choice(query *q);
bool push_barrier(query *q);
bool push_succeed_on_retry_with_barrier(query *q, pl_idx skip);
bool push_succeed_on_retry(query *q, pl_idx skip);
bool push_fail_on_retry_with_barrier(query *q);
bool push_reset_handler(query *q);
bool push_catcher(query *q, enum q_retry type);

bool do_retract(query *q, cell *p1, pl_ctx p1_ctx, enum clause_type is_retract);
bool do_read_term(query *q, stream *str, cell *p1, pl_ctx p1_ctx, cell *p2, pl_ctx p2_ctx, char *src);
query *query_create_task_rebased(query *q, cell *instr, unsigned num_vars);

bool do_yield(query *q, int msecs);
bool do_yield_on_stream(query *q, stream *str, bool is_write);
bool do_yield_now(query *q);
void do_yield_at(query *q, unsigned int time_in_ms);
void sched_destroy(thread *t);
void sched_wake(thread *t);
void sched_promote(query *task);
void sched_release(query *q);
bool register_task(query *q);
void unregister_task(query *q);
query *find_task_by_id(query *q, uint64_t task_id);
thread *find_task_owner(query *q, uint64_t task_id);
void tasks_destroy(thread *t);
void drain_mailbox(query *q);

bool check_slot(query *q, unsigned cnt);
bool check_trail(query *q);
trail *get_trail(query *q, pl_idx idx);

// The common backtracking path stays inline and normally only decrements a
// cached pointer; it visits the previous page at a boundary.
static inline trail *pop_trail(query *q)
{
	assert(q->st.tp);
	q->st.tp--;
	trail *tr = --q->trail_next;

	if ((tr == q->trail_current->entries) && q->st.tp) {
		q->trail_current = q->trail_current->prev;
		q->trail_next = q->trail_current->entries + q->trail_current->page_size;
	}

	return tr;
}

char *url_encode(const char *src, int len, char *dstbuf, size_t dstlen);
char *url_decode(const char *src, char *dstbuf);
bool query_redo(query *q);
bool has_next_key(query *q);
void cut(query *q);
bool execute(query *q, cell *cells, unsigned num_vars);
void undo_me(query *q);
int retry_choice(query *q);
void assign_vars(parser *p, unsigned start, bool rebase);
bool start(query *q);
bool match_rule(query *q, cell *p1, pl_ctx p1_ctx, enum clause_type is_retract);
bool match_clause(query *q, cell *p1, pl_ctx p1_ctx, cell **body, enum clause_type retract);
void call_attrs(query *q, cell *attrs);
bool check_redo(query *q);
const char *cycle_slot_name(query *q, unsigned n, char tmpbuf[256]);
void dump_vars(query *q, bool partial);
int check_interrupt(query *q);
bool has_expired_alarm(query *q);
bool next_alarm_delay(query *q, unsigned *ms);
bool make_slice(query *q, cell *d, const cell *orig, size_t off, size_t n);
void check_pressure(query *q);
cell *prepare_call(query *q, bool noskip, cell *p1, pl_ctx p1_ctx, unsigned extras);
bool call_check(query *q, cell *tmp2, bool *status, bool calln);
bool make_slice(query *q, cell *d, const cell *orig, size_t off, size_t n);
bool match_head(query *q);
bool check_frame(query *q, unsigned max_vars);

enum undo_item {UNDO_BBOARD, UNDO_CELLS, UNDO_RULE};
bool undo_on_backtrack(query *q, void *v, enum undo_item type);

bool throw_error(query *q, cell *c, pl_ctx c_ctx, const char *err_type, const char *expected);
bool throw_error3(query *q, cell *c, pl_ctx c_ctx, const char *err_type, const char *expected, cell *goal);
bool throw_error2(query *q, cell *c, pl_ctx c_ctx, const char *err_type, const char *expected, cell *goal);

size_t scan_is_chars_list2(query *q, cell *l, pl_ctx l_ctx, bool allow_codes, bool *has_var, bool *is_partial, cell **);
size_t scan_is_chars_list(query *q, cell *l, pl_ctx l_ctx, bool allow_codes);
char *chars_list_to_string(query *q, cell *p_chars, pl_ctx p_chars_ctx);
cell *string_to_chars_list(query *q, cell *p);

int create_vars(query *q, unsigned cnt);
cell *skip_max_list(query *q, cell *head, pl_ctx *head_ctx, pl_int max, pl_int *skip, cell *tmp);
cell *term_next(query *q, cell *c, pl_ctx *c_ctx, bool *done);
bool is_cyclic_term(query *q, cell *p1, pl_ctx p1_ctx);
bool is_acyclic_term(query *q, cell *p1, pl_ctx p1_ctx);
bool do_format(query *q, cell *str, pl_ctx str_ctx, cell *p1, pl_ctx p1_ctx, cell *p2, pl_ctx p2_ctx);
size_t slicecpy(char *dst, size_t dstlen, const char *src, size_t len);
int new_stream(prolog *pl);
int get_stream(query *q, cell *p1);
int get_named_stream(prolog *pl, const char *name, size_t len);
void do_cleanup(query *q, cell *p1, pl_ctx c_ctx);
bool drop_barrier(query *q, pl_idx cp);
void collect_vars(query *q, cell *p1, pl_ctx p1_ctx);
bool check_list(query *q, cell *p1, pl_ctx p1_ctx, bool *is_partial, pl_int *skip);
bool parse_write_params(query *q, cell *c, pl_ctx c_ctx, cell **vnames, pl_ctx *vnames_ctx);
bool has_vars(query *q, cell *p1, pl_ctx p1_ctx);
bool add_trail(query *q, pl_ctx c_ctx, unsigned c_var_nbr, cell *attrs);
void reset_var(query *q, const cell *c, pl_ctx c_ctx, cell *v, pl_ctx v_ctx);
void undo_var(query *q, const cell *c, pl_ctx c_ctx);
bool valid_list(query *q, cell *c, pl_ctx c_ctx);
void make_call(query *q, cell *tmp);
void make_call_redo(query *q, cell *tmp);
void make_call_engine(query *q, cell *tmp, cell* c);
bool do_post_unify_hook(query *q, bool is_builtin);
bool any_attributed(query *q);
bool do_load_file(query *q, cell *p1, pl_ctx p1_ctx);
bool stream_close(query *q, int n);

// Release a stream slot handed out by new_stream() but never finished
// being built - see the comment on the definition in bif_streams.c.
void unwind_stream(query *q, int n);
bool throw_stream_gone(query *q, stream *str);
void leave_predicate(query *q, predicate *pr, bool is_final);
void leave_predicate_and_drop(query *q, predicate *pr, bool is_final);
void drop_choice(query *q);

#if USE_THREADS
bool do_signal(query *q, void *thread_ptr);
#endif

int compare(query *q, cell *p1, pl_ctx p1_ctx, cell *p2, pl_ctx p2_ctx);
bool unify(query *q, cell *p1, pl_ctx p1_ctx, cell *p2, pl_ctx p2_ctx);

bool print_term(query *q, FILE *fp, cell *c, pl_ctx c_ctx, int running);
bool print_term_to_stream(query *q, stream *str, cell *c, pl_ctx c_ctx, int running);
bool find_exception_handler(query *q, char *ball);
char *print_term_to_strbuf(query *q, cell *c, pl_ctx c_ctx, int running);
void clear_write_options(query *q);

// The query's scratch parser, built on first use; NULL if it cannot be.
parser *query_parser(query *q);

// Reserve a generated-variable name number, and ask whether one is taken.
// Reserving past MAX_IGNORES, or failing to grow, is silently ignored: the
// cost is a generated name that could collide with a source one, which is
// cosmetic, and varunformat() cannot reach that far anyway.
void ignore_name(query *q, unsigned n);
bool name_is_ignored(const query *q, unsigned n);
void partial_clear_write_options(query *q);

bool print_canonical(query *q, FILE *fp, cell *c, pl_ctx c_ctx, int running);
char *print_canonical_to_strbuf(query *q, cell *c, pl_ctx c_ctx, int running);
bool print_canonical_to_stream(query *q, stream *str, cell *c, pl_ctx c_ctx, int running);

void dump_term(query *q, const char *s, const cell *c);

bool bif_iso_close_1(query *q);
bool bif_iso_true_0(query *q);
bool bif_iso_fail_0(query *q);
bool bif_iso_call_1(query *q);
bool bif_iso_conjunction_2(query *q);
bool bif_iso_qualify_2(query *q);
bool bif_iso_cut_0(query *q);
bool bif_iso_unify_2(query *q);
bool bif_iso_if_then_2(query *q);
bool bif_soft_if_then_2(query *q);
bool bif_sys_call_check_1(query *q);
bool bif_sys_succeed_on_retry_1(query *q);
bool bif_sys_succeed_on_retry_2(query *q);
bool bif_sys_fail_on_retry_1(query *q);
bool bif_sys_drop_barrier_1(query *q);
bool bif_sys_reset_handler_1(query *q);
bool bif_sys_call_cleanup_3(query *q);
bool bif_sys_queue_1(query *q);
bool bif_sys_get_level_1(query *q);
bool bif_sys_set_if_var_2(query *q);
bool bif_sys_jump_1(query *q);
bool bif_sys_jump_if_nil_2(query *q);
bool bif_sys_cut_1(query *q);
bool bif_parse_csv_file_2(query *q);
bool bif_parse_csv_line_3(query *q);
bool bif_parse_csv_line_2(query *q);
bool bif_sre_compile_2(query *q);
bool bif_sre_matchp_4(query *q);
bool bif_sre_match_4(query *q);
bool bif_sre_substp_4(query *q);
bool bif_sre_subst_4(query *q);
bool bif_call_0(query *q, cell *p1, pl_ctx p1_ctx);
bool bif_statistics_0(query *q);
bool bif_sys_module_1(query *q);
bool bif_sys_create_var_1(query *q);
bool bif_sys_list_iterate_3(query *q);
bool bif_iso_halt_0(query *q);

void save_db(FILE *fp, query *q, int logging);
char *uuid_to_buf(const uuid *u, char *buf, size_t buflen);
bool do_abolish(query *q, cell *c_orig, cell *c_pi, bool hard);

enum log_type { LOG_ASSERTA=1, LOG_ASSERTZ=2, LOG_ERASE=3 };

int uuid_from_buf(const char *s, uuid *u);
builtins *get_fn_ptr(void *fn);

#define FEOF(str) feof(str->fp) && !str->ungetch

#ifdef _WIN32
#include <io.h>
#endif

#define DUMP_TERM(s,c,c_ctx,running) { \
	q->nl = true; q->quoted = true; \
	print_term(q, stderr, c, c_ctx, running); \
	q->nl = false; q->quoted = false; \
}

// Turning a pending timeout into an exception CONSUMES it. Leaving the
// flag set makes start() raise a SECOND time_limit_exceeded at whatever
// runs next - which is typically the very cleanup the first exception
// was unwinding into. That is how every fired call_with_time_limit/2
// leaked its timer: the '$alarm'(0, Timer) in its recovery goal was
// killed by a spurious second timeout before it could free anything.

inline static bool throw_timeout(query *q)
{
	thread *self = q->thread_ptr ? q->thread_ptr : q->pl->main_thread;
	self->timedout = 0;
	return throw_error(q, q->st.instr, q->st.cur_ctx, "time_limit_exceeded", "timed_out");
}

inline static bool interrupt_pending(query *q)
{
	thread *self = q->thread_ptr ? q->thread_ptr : q->pl->main_thread;

	if (!self->timedout && self->alarms && has_expired_alarm(q))
		self->timedout = 1;

	return g_tpl_interrupt || self->timedout;
}

#define CHECK_INTERRUPT() \
	if (interrupt_pending(q)) { \
		if (check_interrupt(q)) \
			break; \
	}

inline static bool is_a_rule(const cell *c)
{
	return is_interned(c) && (get_arity(c) == 2) && (c->val_off == g_neck_s);
}

inline static cell *get_head(cell *c)
{
	return is_a_rule(c) ? c + 1 : c;
}

inline static cell *get_body(cell *c)
{
	if (is_a_rule(c)) {
		cell *h = c + 1;
		cell *b = h + h->num_cells;

		if (is_end(b))
			return NULL;

		return b;
	}

	return NULL;
}

inline static pl_idx get_ordered_slot_num(const query *q, const frame *f, unsigned var_num)
{
	return (f->idx * 100) + var_num;
}

inline static pl_idx get_actual_slot_num(const query *q, const frame *f, unsigned var_num)
{
	return get_slot(q, f, var_num) - q->slots;
}

#ifdef _WIN32
typedef intptr_t ssize_t;
#endif
