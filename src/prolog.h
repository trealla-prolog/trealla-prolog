#pragma once

#include "internal.h"

builtins *get_builtin(prolog *pl, const char *name, size_t len, unsigned arity, bool *found, bool *evaluable);
builtins *get_help(prolog *pl, const char *name, unsigned arity, bool *found, bool *evaluable);
module *find_module(prolog *pl, const char *name);
pl_idx new_atom(prolog *pl, const char *name);
bool is_multifile_in_db(prolog *pl, const char *mod, const char *name, unsigned arity);
void load_builtins(prolog *pl);
void uuid_gen(prolog *pl, uuid *u);
void fake_free(const void *key, const void *val, const void *p);
void ptrfree(const void *key, const void *val, const void *p);
void thread_initialize(prolog *pl);
void thread_deinitialize(prolog *pl);
void thread_cancel_all(prolog *pl);
bool is_closed_stream(prolog *pl, cell *p1);

extern pl_idx g_empty_s, g_dot_s, g_cut_s, g_nil_s, g_true_s, g_fail_s;
extern pl_idx g_anon_s, g_neck_s, g_eof_s, g_lt_s, g_gt_s, g_eq_s, g_false_s;
extern pl_idx g_sys_elapsed_s, g_sys_queue_s, g_braces_s, g_call_s, g_braces_s;
extern pl_idx g_sys_stream_property_s, g_unify_s, g_on_s, g_off_s, g_sys_var_s;
extern pl_idx g_plus_s, g_minus_s, g_once_s, g_post_unify_hook_s, g_sys_record_key_s;
extern pl_idx g_conjunction_s, g_disjunction_s, g_at_s, g_sys_ne_s, g_sys_incr_s;
extern pl_idx g_dcg_s, g_throw_s, g_sys_block_catcher_s, g_sys_drop_barrier_s;
extern pl_idx g_sys_catch_s, g_sys_catch_exit_s;
extern pl_idx g_if_then_s, g_soft_cut_s, g_negation_s, g_none_s;
extern pl_idx g_error_s, g_slash_s, g_sys_cleanup_if_det_s;
extern pl_idx g_goal_expansion_s, g_term_expansion_s, g_tm_s, g_float_s;
extern pl_idx g_sys_cut_if_det_s, g_as_s, g_colon_s, g_member_s;
extern pl_idx g_caret_s, g_sys_counter_s, g_catch_s, g_memberchk_s;
extern pl_idx g_cont_s, g_sys_set_if_var_s, g_is_s, g_maplist_s;
extern pl_idx g_sys_succeed_on_retry_s, g_sys_fail_on_retry_s;
extern pl_idx g_sys_call_check_s, g_ignore_s, g_sys_reset_handler_s;
extern pl_idx g_reset_s, g_sys_get_level_s, g_sys_jump_s, g_if_s;
extern pl_idx g_sys_call_s, g_sys_cut_s, g_notunify_s, g_sys_module_s;
extern pl_idx g_sys_reunify_s, g_sys_undo_s, g_sys_jump_if_nil_s;
extern pl_idx g_sys_loop_s, g_sys_end_s, g_sys_create_var_s;
extern pl_idx g_sys_match_s, g_double_bar_s, g_sys_list_s, g_ge_s;
extern pl_idx g_sys_abort_s, g_count_s, g_exit_s, g_killed_s;
extern pl_idx g_dummy_s;

extern void convert_path(char *filename);

extern void g_sigfn(int s);

extern builtins g_atts_bifs[];
extern builtins g_bboard_bifs[];
extern builtins g_dcgs_bifs[];
extern builtins g_tabling_bifs[];
extern builtins g_database_bifs[];
extern builtins g_csv_bifs[];
extern builtins g_evaluable_bifs[];
extern builtins g_format_bifs[];
extern builtins g_ffi_bifs[];
extern builtins g_iso_bifs[];
extern builtins g_misc_bifs[];
extern builtins g_net_bifs[];
extern builtins g_os_bifs[];
// Builtin tables supplied by a port or an optional subsystem, as a
// NULL-terminated array so a build can have more than one - a board with
// both GPIO and networking, say. Empty unless something selects otherwise.
extern builtins *g_port_bif_tables[];
extern builtins g_other_bifs[];
extern builtins g_control_bifs[];
extern builtins g_posix_bifs[];
extern builtins g_sort_bifs[];
extern builtins g_sregex_bifs[];
extern builtins g_streams_bifs[];
extern builtins g_tasks_bifs[];
extern builtins g_threads_bifs[];
extern builtins g_uri_bifs[];

extern prolog *g_prologs[];
extern pl_atomic int g_tpl_count;

extern thread *find_thread_by_id(prolog *pl, int chan);
extern void threads_destroy(prolog *pl);
extern thread *get_self_query(const query *q);

extern void keyfree(const void *key, const void *val, const void *p);

inline static void prolog_lock(prolog *pl)
{
	acquire_lock(&pl->guard);
}

inline static void prolog_unlock(prolog *pl)
{
	release_lock(&pl->guard);
}

// Per-module counterpart, for database mutation only (assert/retract/
// abolish and their purge paths) - see docs/DESIGN-GUSTTO.md phase 3,
// "measured". Not a general substitute for prolog_lock(): stdout
// writes, the thread table, the blackboard and the task registry are
// not module-scoped and stay on the instance-wide lock. A module
// created by module_duplicate() (the :- attribute shadow modules)
// shares its orig's lock, matching find_module()'s own redirect - an
// alias and its original must never serialize independently.

inline static void prolog_lock_mod(prolog *pl, module *m)
{
	if (m->orig) m = m->orig;
	acquire_lock(&m->guard);
}

inline static void prolog_unlock_mod(prolog *pl, module *m)
{
	if (m->orig) m = m->orig;
	release_lock(&m->guard);
}

extern void release_pl_terms(query *q);
