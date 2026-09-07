#include <ctype.h>
#include <errno.h>
#include <stdlib.h>

#include "prolog.h"
#include "query.h"

struct heap_save {
	cell *heap;
	pl_idx size, hp;
};

size_t alloc_grow(query *q, void **addr, size_t elem_size, size_t min_elements, size_t max_elements)
{
	if (min_elements > max_elements)
		max_elements = min_elements;

	// Cap single allocations when memory limiting is enabled (8GB).
	// If only the optimistic max overshoots (e.g. check_slot's num*3/2),
	// clamp to the largest fitting size instead of failing when min fits.
	const uint64_t limit_bytes = 1024ULL * 1024ULL * 1024ULL * 8ULL;
	const size_t effective_limit = limit_bytes > SIZE_MAX ? SIZE_MAX : (size_t)limit_bytes;

	if (q->pl->limit && elem_size) {
		size_t capped = effective_limit / elem_size;

		if (max_elements > capped) {
			if (capped < min_elements) {
				q->oom = true;
				return 0;
			}

			max_elements = capped;
		}
	}

	size_t elements = max_elements;
	void *mem = NULL;

	do {
		mem = TPL_realloc(*addr, elem_size * elements);
		if (mem) break;
		elements = min_elements + (elements - min_elements) / 2;
	}
	 while (elements > min_elements);

	if (!mem) {
		q->oom = true;
		return 0;
	}

	*addr = mem;
	return elements;
}

cell *init_tmp_heap(query *q)
{
	if (!q->tmp_heap) {
		q->tmp_heap = TPL_malloc(q->tmph_size * sizeof(cell));
		if (!q->tmp_heap) return NULL;
	}

	q->tmphp = 0;
	return q->tmp_heap;
}

// The tmp heap is used for temporary allocations (a scratch-pad)
// for work in progress. As such it can survive a TPL_realloc() call.
// No need to incr refcnt on tmp heap cells.

cell *alloc_tmp(query *q, unsigned num_cells)
{
	pl_idx new_size = q->tmphp + num_cells;

	if (new_size >= q->tmph_size) {
		size_t elements = alloc_grow(q, (void**)&q->tmp_heap, sizeof(cell), new_size, new_size*5/4);
		if (!elements) return NULL;
		q->tmph_size = elements;
	}

	cell *c = q->tmp_heap + q->tmphp;
	q->tmphp = new_size;
	return c;
}

static inline bool ref_is_live(const query *q, const cell *c)
{
	if (c->val_ctx >= q->st.fp)
		return false;

	const frame *f = GET_FRAME(c->val_ctx);
	return c->var_num < f->actual_slots;
}

#define deep_copy(c) \
 (!q->noderef || (is_ref(c) && (c->val_ctx <= q->st.cur_ctx) && ref_is_live(q, c) && !is_anon(c)))

// The slot a variable ultimately names, following the chain of refs the
// way deref() does.

static slot *ultimate_slot(const query *q, const cell *c, pl_ctx c_ctx)
{
	if (is_ref(c))
		c_ctx = c->val_ctx;

	const frame *f = GET_FRAME(c_ctx);
	slot *e = get_slot(q, f, c->var_num);

	while (is_var(&e->c)) {
		c_ctx = e->c.val_ctx;
		c = &e->c;

		if (is_ref(c))
			c_ctx = c->val_ctx;

		f = GET_FRAME(c_ctx);
		slot *e2 = get_slot(q, f, c->var_num);

		if (e == e2)
			break;

		e = e2;
	}

	return e;
}

// Whether a variable in the term being copied denotes the same thing as
// the variable copy_term/2 is replacing, so that a reference back to it
// becomes the target variable instead of a newly invented one.
//
// Comparing var_num and context is not enough, and neither is comparing
// slots. Given
//
//     cyclic(X) :- X = f(g(X,_),_).
//     wrap(X, Y) :- copy_term(X, Y).
//
// the back-reference inside the term names the slot of whoever built it
// (cyclic/1's X), while copy_term/2 is handed the term through another
// slot again (wrap/2's X), arguments being passed by value. All three
// name the same term, which is what makes the reference a cycle, so a
// bound variable is compared by the term it denotes. Getting this wrong
// left copy_term/2 with nothing to replace, and it then invented a
// variable for the back-reference: an acyclic unrolling of a cyclic
// term, with one variable too many (#1002).
//
// A term is a cell together with a context, and both halves have to
// match: activations of a recursive clause share its cells and are told
// apart only by their context.

static bool denotes_same(query *q, cell *c, pl_ctx c_ctx, const cell *from_val, pl_ctx from_val_ctx, const slot *from_e)
{
	if (!from_val)
		return ultimate_slot(q, c, c_ctx) == from_e;

	const pl_ctx save_latest_ctx = q->latest_ctx;
	const cell *val = deref(q, c, c_ctx);
	const pl_ctx val_ctx = q->latest_ctx;
	q->latest_ctx = save_latest_ctx;
	return (val == from_val) && (val_ctx == from_val_ctx);
}

// Whether a dereferenced argument is a reference back to the whole term
// being copied. Only the raw argument tells us: dereferencing it lands
// on the term itself, and it is the variable that named it which has to
// be kept, for copy_vars() to turn into the target variable. Descending
// instead copies one level of the cycle before catching it, so that each
// copy of a cyclic term came out larger than the last.
//
// The context matters as much as the cell: the arguments of a recursive
// clause's head are the same cells at every depth, and comparing cells
// alone takes the deeper one for the term the copy started from.
//
// Callers test the raw argument for being a variable first: that cell has
// just been read, whereas this walks off into the query.

static bool cycles_back(const query *q, const cell *c, pl_ctx c_ctx)
{
	return q->clone_root && (c == q->clone_root) && (c_ctx == q->clone_root_ctx);
}

static void record_clone_def(query *q, pl_idx slot_nbr, pl_idx tmp_offset);

// Note: convert vars to refs
// Note: doesn't increment ref counts

// Used by clone_term_to_tmp_internal() to walk plain (non-list) compound
// terms iteratively instead of recursing, one frame per unfinished compound.
// 'e'/'save_vgen' belong to the *parent* argument slot that caused us to
// descend into this node, and are restored once this node (and everything
// beneath it) has been fully cloned - i.e. at the point the recursive call
// would otherwise have returned.

typedef struct { lnode hdr; cell *p1; pl_ctx p1_ctx; uint32_t arity; pl_idx save_idx; unsigned depth; slot *e; uint32_t save_vgen; } snode;

static cell *clone_term_to_tmp_internal(query *q, cell *p1, pl_ctx p1_ctx, unsigned depth)
{
#if 0
	if (depth >= g_max_depth) {
		printf("*** OOPS %s %d\n", __FILE__, __LINE__);
		q->cycle_error = true;
		return NULL;
	}
#endif

	pl_idx save_idx = tmp_heap_used(q);
	cell *tmp = alloc_tmp(q, 1);
	if (!tmp) return NULL;
	copy_cells(tmp, p1, 1);

	if (is_var(p1))
		q->has_vars = true;

	if (is_var(tmp) && !is_ref(tmp) && !q->noderef) {
		tmp->flags |= FLAG_VAR_REF;
		tmp->val_ctx = p1_ctx;
	}

	if (!is_compound(p1))
		return tmp;

	if (is_iso_list(p1)) {
		cell *save_p1 = p1;
		pl_ctx save_p1_ctx = p1_ctx;
		bool any1 = false, any2 = false;

		while (is_iso_list(p1)) {
			slot *e = NULL;
			cell *h = p1 + 1;
			pl_ctx h_ctx = p1_ctx;
			uint32_t save_vgen = 0;
			int both = 0;
			if (deep_copy(h)) DEREF_CHECKED(any1, both, save_vgen, e, e->vgen, h, h_ctx, q->vgen);
			if (both) q->cycle_error = q->cycle_dropped = true;

			if (is_var(p1 + 1) && cycles_back(q, h, h_ctx)) {
				h = p1 + 1;
				h_ctx = p1_ctx;
				q->cycle_error = true;
			}

			cell *rec = clone_term_to_tmp_internal(q, h, h_ctx, depth+1);
			if (!rec) return NULL;
			if (e) e->vgen = save_vgen;

			p1 = p1 + 1; p1 += p1->num_cells;
			cell *t = p1;
			pl_ctx t_ctx = p1_ctx;

			if (is_var(t) && (t->var_num == q->dump_var_num) && (t_ctx == q->dump_var_ctx)) {
				q->cycle_error = true;
				break;
			}

			both = 0;
			bool t_was_var = is_var(t);
			unsigned t_var_num = t_was_var ? t->var_num : 0;
			pl_ctx t_owning_ctx = t_was_var ? (is_ref(t) ? t->val_ctx : t_ctx) : 0;
			if (deep_copy(t)) DEREF_CHECKED(any2, both, save_vgen, e, e->vgen, t, t_ctx, q->vgen);

			// Slot is about to be flattened in for the first time: remember
			// where, so a later back-edge to it (issue #1121) has something
			// to bind to instead of coming out dangling.
			if (q->close_cycles && t_was_var && !both && is_compound(t))
				record_clone_def(q, get_ordered_slot_num(q, GET_FRAME(t_owning_ctx), t_var_num), tmp_heap_used(q));

			if (both)
				q->cycle_error = q->cycle_dropped = true;

			if (is_var(p1) && cycles_back(q, t, t_ctx)) {
				t = p1;
				t_ctx = p1_ctx;
				q->cycle_error = true;
			}

			p1 = t;
			p1_ctx = t_ctx;

			if (is_iso_list(p1)) {
				cell *tmp = alloc_tmp(q, 1);
				if (!tmp) return NULL;
				copy_cells(tmp, p1, 1);
			}
		}

		cell *rec = clone_term_to_tmp_internal(q, p1, p1_ctx, depth+1);
		if (!rec) return NULL;

		if (any2) {
			p1 = save_p1;
			p1_ctx = save_p1_ctx;

			while (is_iso_list(p1) && !q->cycle_error) {
				p1 = p1 + 1; p1 += p1->num_cells;
				cell *c = p1;
				pl_ctx c_ctx = p1_ctx;
				RESTORE_VAR(c, c_ctx, p1, p1_ctx, q->vgen);
			}
		}

		tmp = get_tmp_heap(q, save_idx);
		tmp->num_cells = tmp_heap_used(q) - save_idx;

		if (!q->has_vars)
			tmp->flags |= FLAG_INTERNED_GROUND;

		return tmp;
	}

	// Transform recursion into stack iteration (as in terms.c)...

	list stack = {0};
	snode *n = TPL_malloc(sizeof(snode));
	if (!n) return NULL;
	n->arity = get_arity(p1);
	n->p1 = p1 + 1;
	n->p1_ctx = p1_ctx;
	n->save_idx = save_idx;
	n->depth = depth;
	n->e = NULL;
	n->save_vgen = 0;
	list_push_back(&stack, n);

	cell *result = NULL;

	while ((n = (snode*)list_back(&stack)) != NULL) {
		if (!n->arity) {
			// This node's arguments are all done, so finalize it. This is
			// the point at which a recursive call would have returned.
			tmp = get_tmp_heap(q, n->save_idx);
			tmp->num_cells = tmp_heap_used(q) - n->save_idx;

			if (!q->has_vars)
				tmp->flags |= FLAG_INTERNED_GROUND;

			result = tmp;
			slot *pending_e = n->e;
			uint32_t pending_vgen = n->save_vgen;

			list_pop_back(&stack);
			TPL_free(n);

			if (pending_e)
				pending_e->vgen = pending_vgen;

			continue;
		}

		n->arity--;
		slot *e = NULL;
		cell *c = n->p1;
		pl_ctx c_ctx = n->p1_ctx;
		uint32_t save_vgen = 0;
		bool any = false;
		int both = 0;
		if (deep_copy(c)) DEREF_CHECKED(any, both, save_vgen, e, e->vgen, c, c_ctx, q->vgen);
		if (both) q->cycle_error = q->cycle_dropped = true;

		if (is_var(n->p1) && cycles_back(q, c, c_ctx)) {
			c = n->p1;
			c_ctx = n->p1_ctx;
			q->cycle_error = true;
		}

		n->p1 += n->p1->num_cells;

		if (is_compound(c) && !is_iso_list(c)) {
			// Instead of recursing, push a new frame and keep iterating.
			// The (e, save_vgen) pair travels with the child frame and is
			// restored once the child (its whole subtree) is finished.
			pl_idx child_idx = tmp_heap_used(q);
			cell *child = alloc_tmp(q, 1);

			if (!child) {
				while ((n = (snode*)list_pop_back(&stack)) != NULL)
					TPL_free(n);

				return NULL;
			}

			copy_cells(child, c, 1);

			snode *cn = TPL_malloc(sizeof(snode));

			if (!cn) {
				while ((n = (snode*)list_pop_back(&stack)) != NULL)
					TPL_free(n);

				return NULL;
			}

			cn->arity = get_arity(c);
			cn->p1 = c + 1;
			cn->p1_ctx = c_ctx;
			cn->save_idx = child_idx;
			cn->depth = n->depth + 1;
			cn->e = e;
			cn->save_vgen = save_vgen;
			list_push_back(&stack, cn);
		} else {
			// Atoms, variables and lists still recurse (lists have their
			// own cycle-aware traversal above, and atoms/variables only
			// ever recurse one level deep).
			cell *rec = clone_term_to_tmp_internal(q, c, c_ctx, n->depth+1);

			if (!rec) {
				while ((n = (snode*)list_pop_back(&stack)) != NULL)
					TPL_free(n);

				return NULL;
			}

			if (e) e->vgen = save_vgen;
		}
	}

	return result;
}

cell *clone_term_to_tmp(query *q, cell *p1, pl_ctx p1_ctx)
{
	q->cycle_dropped = false;
	if (++q->vgen == 0) q->vgen = 1;
	q->has_vars = false;
	cell *rec = clone_term_to_tmp_internal(q, p1, p1_ctx, 0);
	if (!rec) return NULL;
	return rec;
}

cell *append_to_tmp(query *q, cell *p1, pl_ctx p1_ctx)
{
	cell *tmp = alloc_tmp(q, p1->num_cells);
	if (!tmp) return NULL;
	copy_cells_by_ref(tmp, p1, p1_ctx, p1->num_cells);
	return tmp;
}

// close_cycles only (see internal.h): first-write-wins, so a slot keeps
// the offset of its own definition rather than some later revisit of it.

static void record_clone_def(query *q, pl_idx slot_nbr, pl_idx tmp_offset)
{
	const void *v;

	if (q->clone_defs && sl_get(q->clone_defs, (void*)(size_t)slot_nbr, &v))
		return;

	if (!q->clone_defs)
		q->clone_defs = sl_create(NULL, NULL, NULL);

	sl_app(q->clone_defs, (void*)(size_t)slot_nbr, (void*)(size_t)tmp_offset);
}

static int accum_slot(query *q, size_t slot_nbr, unsigned var_num)
{
	const void *vnbr;

	if (q->vars && sl_get(q->vars, (void*)slot_nbr, &vnbr))
		return (unsigned)(size_t)vnbr;

	if (!q->vars)
		q->vars = sl_create(NULL, NULL, NULL);

	sl_app(q->vars, (void*)slot_nbr, (void*)(size_t)var_num);
	return -1;
}

static bool copy_vars(query *q, cell *c, bool copy_attrs, cell *from, pl_ctx from_ctx, cell *to, pl_ctx to_ctx)
{
	unsigned num_cells = c->num_cells;
	unsigned cnt = 0;
	const slot *from_e = NULL;			// the slot 'from' names
	cell *from_val = NULL;				// the term it denotes, if bound
	pl_ctx from_val_ctx = 0;

	if (from) {
		const pl_ctx save_latest_ctx = q->latest_ctx;
		from_val = deref(q, from, from_ctx);
		from_val_ctx = q->latest_ctx;
		q->latest_ctx = save_latest_ctx;
		from_e = ultimate_slot(q, from, from_ctx);

		if (is_var(from_val))
			from_val = NULL;
	}

	for (unsigned i = 0; i < num_cells; i++, c++) {
		if (!is_ref(c))
			continue;

		// NB. do not stamp FLAG_VAR_LOCAL here: unify_var()'s occurs-check
		// gate reads it to skip the cyclic-term scan, and this loop runs
		// on every var copy_term produces, so it was silently disabling
		// occurs-check on any goal run via copy_term (e.g. quads'
		// call_nth), re issue #1135.

		if (from && denotes_same(q, c, c->val_ctx, from_val, from_val_ctx, from_e)) {
			c->var_num = to->var_num;
			c->val_ctx = to_ctx;

			// BUGFIX: the replacement fast-path must still carry over
			// attributes from the source variable onto the target.

			if (copy_attrs && !c->tmp_attrs) {
				cell *attrs = from_e->c.val_attrs;

				if (attrs) {
					cell *save_tmp_heap = q->tmp_heap;
					pl_idx save_tmp_hp = q->tmphp;
					q->tmp_heap = NULL;
					cell *tmp = copy_term_to_heap(q, attrs, q->st.cur_ctx, false);
					CHECKED(tmp);
					c->tmp_attrs = tmp;
					TPL_free(q->tmp_heap);
					q->tmp_heap = save_tmp_heap;
					q->tmphp = save_tmp_hp;
				}
			}
		} else {
			const frame *f = GET_FRAME(c->val_ctx);
			// NB. get_ordered_slot_num is pure arithmetic (no deref), so
			// it is safe even when c->val_ctx names a long-dead frame, as
			// happens when rebasing an imported (detached) term image.
			// Only consult the slot itself when attributes are wanted:
			// dereferencing a dead frame's slot is undefined.
			const size_t slot_nbr = get_ordered_slot_num(q, f, c->var_num);
			cell *attrs = NULL;

			if (copy_attrs) {
				const slot *e = get_slot(q, f, c->var_num);
				attrs = c->tmp_attrs ? c->tmp_attrs : e->c.val_attrs;
			}
			int var_num;

			if ((var_num = accum_slot(q, slot_nbr, q->varno)) == -1) {
				var_num = q->varno++;
				cnt++;

				if (create_vars(q, 1) < 0)
					return false;
			}

			if (!q->tab_idx) {
				q->tab0_varno = var_num;
				q->tab_idx++;
			}

			c->var_num = var_num;
			c->val_ctx = q->st.cur_ctx;

			if (copy_attrs && attrs) {
				cell *save_tmp_heap = q->tmp_heap;
				pl_idx save_tmp_hp = q->tmphp;
				q->tmp_heap = NULL;

				if (!c->tmp_attrs) {
					cell *tmp =
						from ?copy_term_to_heap_with_replacement(q, attrs, q->st.cur_ctx, false, from, from_ctx, to, to_ctx)
						:copy_term_to_heap(q, attrs, q->st.cur_ctx, false);
					CHECKED(tmp);
					c->tmp_attrs = tmp;
				}

				TPL_free(q->tmp_heap);
				q->tmp_heap = save_tmp_heap;
				q->tmphp = save_tmp_hp;
			}
		}
	}

	return true;
}

unsigned rebase_term(query *q, cell *c, unsigned start_nbr, bool copy_attrs)
{
	q->vars = NULL;
	q->varno = start_nbr;
	q->tab_idx = 0;

	if (!copy_vars(q, c, copy_attrs, NULL, 0, NULL, 0)) {
		if (q->vars)
			sl_destroy(q->vars);

		q->vars = NULL;
		return q->varno;
	}

	if (q->vars) {
		sl_destroy(q->vars);
		q->vars = NULL;
	}

	// Turn refs back into vars to recontextualize

	cell *tmp = c;

	for (unsigned i = 0; i < c->num_cells; i++, tmp++) {
		if (!is_ref(tmp))
			continue;

		tmp->flags &= ~FLAG_VAR_REF;
	}

	return q->varno;
}

static cell *copy_term_to_tmp_with_replacement(query *q, cell *p1, pl_ctx p1_ctx, bool copy_attrs, cell *from, pl_ctx from_ctx, cell *to, pl_ctx to_ctx)
{
	cell *c = deref(q, p1, p1_ctx);
	pl_ctx c_ctx = q->latest_ctx;

	// Have the walk stop at references back to the whole term, but only
	// when there is a target variable for such a reference to become:
	// without one it has to be a newly invented variable wherever the
	// cycle is caught, and catching it later at least keeps a level of
	// the structure.

	cell *save_root = q->clone_root;
	pl_ctx save_root_ctx = q->clone_root_ctx;
	q->clone_root = (from && to && is_compound(c)) ? c : NULL;
	q->clone_root_ctx = c_ctx;
	cell *tmp = clone_term_to_tmp(q, c, c_ctx);
	q->clone_root = save_root;
	q->clone_root_ctx = save_root_ctx;

	if (!tmp)
		return NULL;

	bool created = false;

	if (!q->vars) {
		created = true;
		const frame *f = GET_CURR_FRAME();
		q->varno = f->actual_slots;
		q->tab_idx = 0;
	}

	bool ok = copy_vars(q, tmp, copy_attrs, from, from_ctx, to, to_ctx);

	// close_cycles needs q->vars alive a bit longer, to resolve interior
	// back-edges once the heap copy exists - see close_clone_cycles().

	if (created && !q->close_cycles) {
		if (q->vars)
			sl_destroy(q->vars);

		q->vars = NULL;
	}

	c = tmp;

	for (pl_idx i = 0; i < tmp->num_cells; i++, c++) {
		if (is_var(c) && copy_attrs && c->tmp_attrs) {
			const frame *f = GET_FRAME(c->val_ctx);
			slot *e = get_slot(q, f, c->var_num);
			e->c.val_attrs = c->tmp_attrs;
			c->tmp_attrs = NULL;
			if (!add_trail(q, c->val_ctx, c->var_num, NULL))
				return NULL;
		}
	}

	return ok ? tmp : NULL;
}

cell *copy_term_to_tmp(query *q, cell *p1, pl_ctx p1_ctx, bool copy_attrs)
{
	q->has_vars = false;
	return copy_term_to_tmp_with_replacement(q, p1, p1_ctx, copy_attrs, NULL, 0, NULL, 0);
}

cell *alloc_heap(query *q, unsigned num_cells)
{
	size_t page_size = q->heap_pages ? q->heap_pages->page_size*2 : q->heap_size;

	if (!q->heap_pages || ((q->st.hp + num_cells) >= q->heap_pages->page_size))  {
		page *a = TPL_calloc(1, sizeof(page));
		if (!a) return NULL;
		a->next = q->heap_pages;
		unsigned n = MAX_OF(page_size, num_cells);
		a->cells = TPL_calloc(a->page_size=n, sizeof(cell));
		if (!a->cells) { TPL_free(a); return NULL; }
		a->num = q->st.hp_num++;
		q->heap_pages = a;
		q->st.hp = 0;
	}

	cell *c = q->heap_pages->cells + q->st.hp;
	q->st.hp += num_cells;
	q->heap_pages->idx = q->st.hp;
	return c;
}

void trim_heap(query *q)
{
	for (page *a = q->heap_pages; a;) {
		if (a->num <= q->st.hp_num)
			break;

		cell *c = a->cells;

		for (pl_idx i = 0; i < a->idx; i++, c++)
			unshare_cell(c);

		page *save = a;
		q->heap_pages = a = a->next;
		TPL_free(save->cells);
		TPL_free(save);
	}

	if (!q->heap_pages)
		return;

	while (q->heap_pages->idx > q->st.hp) {
		cell *c = q->heap_pages->cells + --q->heap_pages->idx;
		unshare_cell(c);
		c->tag = TAG_EMPTY;
		c->val_attrs = NULL;
	}
}

// Cleans up q->clone_defs/q->vars when a close_cycles copy bails out
// (e.g. OOM) before close_clone_cycles() gets to run them.

void abandon_clone_cycles(query *q)
{
	if (q->clone_defs) {
		sl_destroy(q->clone_defs);
		q->clone_defs = NULL;
	}

	if (q->vars) {
		sl_destroy(q->vars);
		q->vars = NULL;
	}
}

// close_cycles only: bind each fresh var created for an interior back-edge
// (record_clone_def) to where its slot's value landed in the heap copy.

static void close_clone_cycles(query *q, cell *tmp2)
{
	if (!q->close_cycles)
		return;

	if (q->clone_defs) {
		sliter *iter = sl_first(q->clone_defs);
		void *offset_v;

		while (sl_next(iter, &offset_v)) {
			pl_idx slot_nbr = (pl_idx)(size_t)sl_key(iter);
			const void *var_v;

			if (!q->vars || !sl_get(q->vars, (void*)(size_t)slot_nbr, &var_v))
				continue;

			unsigned var_num = (unsigned)(size_t)var_v;
			const frame *f = GET_FRAME(q->st.cur_ctx);
			slot *e = get_slot(q, f, var_num);
			make_indirect(&e->c, tmp2 + (pl_idx)(size_t)offset_v, q->st.cur_ctx);
			add_trail(q, q->st.cur_ctx, var_num, NULL);
		}

		// Every other sl_first() in the tree is paired with sl_done() to
		// return the iterator to the skiplist's pool; this was the one
		// site that dropped it instead, leaking sizeof(sliter) on every
		// copy_term/2 (etc.) of a term with an interior cycle.
		sl_done(iter);
		sl_destroy(q->clone_defs);
		q->clone_defs = NULL;
	}

	if (q->vars) {
		sl_destroy(q->vars);
		q->vars = NULL;
	}
}

cell *clone_term_to_heap(query *q, cell *p1, pl_ctx p1_ctx)
{
	if (!init_tmp_heap(q))
		return NULL;

	q->has_vars = false;
	p1 = clone_term_to_tmp(q, p1, p1_ctx);
	if (!p1) return p1;
	cell *tmp = alloc_heap(q, p1->num_cells);
	if (!tmp) return NULL;
	dup_cells(tmp, p1, p1->num_cells);
	return tmp;
}

cell *copy_term_to_heap_with_replacement(query *q, cell *p1, pl_ctx p1_ctx, bool copy_attrs, cell *from, pl_ctx from_ctx, cell *to, pl_ctx to_ctx)
{
	if (!init_tmp_heap(q))
		return NULL;

	cell *tmp = copy_term_to_tmp_with_replacement(q, p1, p1_ctx, copy_attrs, is_var(from)?from:NULL, from_ctx, is_var(to)?to:NULL, to_ctx);
	if (!tmp) return tmp;
	cell *tmp2 = alloc_heap(q, tmp->num_cells);
	if (!tmp2) return NULL;
	dup_cells(tmp2, tmp, tmp->num_cells);
	close_clone_cycles(q, tmp2);

	if (!copy_attrs)
		return tmp2;

	cell *c = tmp2;

	for (pl_idx i = 0; i < tmp2->num_cells; i++, c++) {
		if (is_var(c) && copy_attrs && c->tmp_attrs) {
			const frame *f = GET_FRAME(c->val_ctx);
			slot *e = get_slot(q, f, c->var_num);
			e->c.val_attrs = c->tmp_attrs;
			c->tmp_attrs = NULL;
		}
	}

	return tmp2;
}

cell *copy_term_to_heap(query *q, cell *p1, pl_ctx p1_ctx, bool copy_attrs)
{
	if (!init_tmp_heap(q))
		return NULL;

	q->has_vars = false;
	cell *tmp = copy_term_to_tmp_with_replacement(q, p1, p1_ctx, copy_attrs, NULL, 0, NULL, 0);
	if (!tmp) return tmp;
	cell *tmp2 = alloc_heap(q, tmp->num_cells);
	if (!tmp2) return NULL;
	dup_cells(tmp2, tmp, tmp->num_cells);
	close_clone_cycles(q, tmp2);

	if (!copy_attrs)
		return tmp2;

	cell *c = tmp2;

	for (pl_idx i = 0; i < tmp2->num_cells; i++, c++) {
		if (is_var(c) && copy_attrs && c->tmp_attrs) {
			const frame *f = GET_FRAME(c->val_ctx);
			slot *e = get_slot(q, f, c->var_num);
			e->c.val_attrs = c->tmp_attrs;
			c->tmp_attrs = NULL;
			if (!add_trail(q, c->val_ctx, c->var_num, NULL))
				return NULL;
		}
	}

	return tmp2;
}

void fix_list(cell *c)
{
	pl_idx cnt = c->num_cells;

	while (is_iso_list(c)) {
		c->num_cells = cnt;
		c = c + 1;					// skip .
		cnt -= 1 + c->num_cells;
		c = c + c->num_cells;		// skip head
	}
}

cell *allocate_list(query *q, const cell *c)
{
	if (!init_tmp_heap(q))
		return NULL;

	append_list(q, c);
	return get_tmp_heap(q, 0);
}

cell *append_list(query *q, const cell *c)
{
	cell *tmp = alloc_tmp(q, 1+c->num_cells);
	if (!tmp) return NULL;
	cell *save = tmp;
	tmp->tag = TAG_INTERNED;
	tmp->num_cells = 1 + c->num_cells;
	tmp->val_off = g_dot_s;
	tmp->flags = 0;
	set_arity(tmp, 2);
	tmp++;
	copy_cells(tmp, c, c->num_cells);
	return save;
}

cell *end_list(query *q)
{
	cell *tmp = alloc_tmp(q, 1);
	if (!tmp) return NULL;
	tmp->tag = TAG_INTERNED;
	tmp->num_cells = 1;
	tmp->val_off = g_nil_s;
	tmp->flags = 0;
	set_arity(tmp, 0);

	if (is_nil(get_tmp_heap(q, 0))) {
		init_tmp_heap(q);
		return make_nil();
	}

	pl_idx num_cells = tmp_heap_used(q);
	tmp = alloc_heap(q, num_cells);
	if (!tmp) return NULL;
	dup_cells(tmp, get_tmp_heap(q, 0), num_cells);
	tmp->num_cells = num_cells;
	fix_list(tmp);
	init_tmp_heap(q);
	return tmp;
}

cell *end_list_unsafe(query *q)
{
	cell *tmp = alloc_tmp(q, 1);
	if (!tmp) return NULL;
	tmp->tag = TAG_INTERNED;
	tmp->num_cells = 1;
	tmp->val_off = g_nil_s;
	tmp->flags = 0;
	set_arity(tmp, 0);

	if (is_nil(get_tmp_heap(q, 0))) {
		init_tmp_heap(q);
		return make_nil();
	}
	pl_idx num_cells = tmp_heap_used(q);
	tmp = alloc_heap(q, num_cells);
	if (!tmp) return NULL;
	copy_cells(tmp, get_tmp_heap(q, 0), num_cells);
	tmp->num_cells = num_cells;
	fix_list(tmp);
	init_tmp_heap(q);
	return tmp;
}

cell *allocate_structure(query *q, const char *functor, const cell *c)
{
	if (!init_tmp_heap(q))
		return NULL;

	cell *tmp = alloc_tmp(q, 1);
	if (!tmp) return NULL;
	tmp->tag = TAG_INTERNED;
	tmp->num_cells = 1;
	tmp->val_off = new_atom(q->pl, functor);
	tmp->flags = 0;
	set_arity(tmp, 0);
	append_structure(q, c);
	return get_tmp_heap(q, 0);
}

cell *append_structure(query *q, const cell *c)
{
	cell *tmp = alloc_tmp(q, c->num_cells);
	if (!tmp) return NULL;
	copy_cells(tmp, c, c->num_cells);
	tmp = q->tmp_heap;
	set_arity(tmp, get_arity(tmp) + 1);
	return tmp;
}

cell *end_structure_heap(query *q)
{
	pl_idx num_cells = tmp_heap_used(q);
	cell *tmp = alloc_heap(q, num_cells);
	if (!tmp) return NULL;
	dup_cells(tmp, get_tmp_heap(q, 0), num_cells);
	tmp->num_cells = num_cells;

	if (q->tmp_heap && (q->tmph_size > 1000)) {
		TPL_free(q->tmp_heap);
		q->tmp_heap = NULL;
		q->tmph_size = 1000;
	}

	return tmp;
}

cell *alloc_queuen(query *q, unsigned qnum, const cell *c)
{
	if (!ensure_queuen(q, qnum))
		return NULL;

	qbuf *b = &q->queues[qnum];

	if (!b->queue) {
		b->queue = TPL_malloc(sizeof(cell)*b->q_size);
		if (!b->queue) return NULL;
	}

	while ((b->qp+c->num_cells) >= b->q_size) {
		size_t n = b->q_size + b->q_size / 2;
		void *ptr = TPL_realloc(b->queue, sizeof(cell)*n);
		if (!ptr) return NULL;
		b->queue = ptr;
		b->q_size = n;
	}

	cell *dst = b->queue + b->qp;
	b->qp += dup_cells(dst, c, c->num_cells);
	b->qcnt++;
	return dst;
}

// Release a detached term image built for import_term().

void free_detached_term(cell *c)
{
	if (!c)
		return;

	unshare_cells(c, c->num_cells);
	TPL_free(c);
}

cell *import_term(query *q, cell *c, pl_ctx c_ctx)
{
	cell *tmp = TPL_malloc(sizeof(cell) * c->num_cells);
	if (!tmp) return NULL;
	dup_cells_by_ref(tmp, c, c_ctx, c->num_cells);
	const frame *f = GET_CURR_FRAME();
	rebase_term(q, tmp, f->actual_slots, false);
	undo_on_backtrack(q, tmp, UNDO_CELLS);
	return tmp;
}
