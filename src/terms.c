#include <stdlib.h>

#include "query.h"

static const size_t INITIAL_VAR_TABLE_SIZE = 256;

typedef struct { lnode hdr; cell *c; pl_ctx c_ctx; slot *e; uint32_t save_vgen; } snode;

static bool accum_var(query *q, const cell *c, pl_ctx c_ctx)
{
	const frame *f = GET_FRAME(c_ctx);
	const slot *e = get_slot(q, f, c->var_num);
	const void *v;

	if (sl_get(q->vars, e, &v)) {
		size_t idx = (size_t)v;
		q->tabs[idx].cnt++;
		return true;
	}

	sl_app(q->vars, e, (void*)(size_t)q->tab_idx);

	if (!q->tabs) {
		q->tabs_size = INITIAL_VAR_TABLE_SIZE;
		q->tabs = TPL_malloc(sizeof(var_item)*q->tabs_size);

		if (!q->tabs)
			return false;
	}

	if (q->tab_idx == q->tabs_size) {
		q->tabs_size *= 2;
		q->tabs = TPL_realloc(q->tabs, sizeof(var_item)*q->tabs_size);

		if (!q->tabs)
			return false;
	}

	q->tabs[q->tab_idx].val_off = c->val_off;
	q->tabs[q->tab_idx].var_num = c->var_num;
	q->tabs[q->tab_idx].ctx = c_ctx;
	q->tabs[q->tab_idx].is_anon = is_anon(c) ? true : false;
	q->tabs[q->tab_idx].cnt = 1;
	q->tab_idx++;
	return false;
}

typedef struct {
	lnode hdr;
	cell *p1;
	pl_ctx p1_ctx;
	uint32_t arity;
	unsigned depth;
	slot *e;
	uint32_t save_vgen;
} vnode;

static void collect_vars_internal(query *q, cell *p1, pl_idx p1_ctx, unsigned depth)
{
	if (is_var(p1) && !(p1->flags & FLAG_VAR_CYCLIC)) {
		accum_var(q, p1, p1_ctx);
		return;
	}

	if (!is_compound(p1) || is_ground(p1))
		return;

	// Transform recursion into stack iteration (as in has_vars_internal):
	//
	// Unlike has_vars_internal() we must preserve the order in which
	// variables are encountered, so we cannot use a plain FIFO/LIFO of
	// pending nodes: a variable that is a direct argument would then be
	// accumulated before the variables inside an earlier sibling that was
	// only queued. Instead each frame walks one compound's arguments
	// left-to-right and we descend into a child compound immediately, which
	// reproduces the pre-order, depth-first, left-to-right walk of the
	// recursion exactly. The (e, save_vgen) pair belongs to the parent
	// arg-slot that caused us to descend and is restored once this subtree
	// is fully collected - i.e. at the point the recursive call would have
	// returned.

	list stack = {0};
	vnode *n = TPL_malloc(sizeof(vnode));
	if (!n) return;
	n->arity = get_arity(p1);
	n->p1 = p1 + 1;
	n->p1_ctx = p1_ctx;
	n->depth = depth;
	n->e = NULL;
	n->save_vgen = 0;
	list_push_back(&stack, n);

	while ((n = (vnode*)list_back(&stack)) != NULL) {
		if (!n->arity) {
			// This node's arguments are all done - the point at which the
			// recursive call would have returned. Restore the parent
			// arg-slot's vgen mark now that its subtree is complete.
			slot *pending_e = n->e;
			uint32_t pending_vgen = n->save_vgen;

			list_pop_back(&stack);
			TPL_free(n);

			if (pending_e)
				pending_e->vgen = pending_vgen;

			continue;
		}

		n->arity--;
		cell *c = n->p1;
		pl_ctx c_ctx = n->p1_ctx;
		slot *e = NULL;
		uint32_t save_vgen = 0;
		bool any = false;
		int both = 0;

		DEREF_VAR(any, both, save_vgen, e, e->vgen, c, c_ctx, q->vgen);
		n->p1 += n->p1->num_cells;

		if (both) {
			if (e) e->vgen = save_vgen;
			continue;
		}

		if (is_var(c) && !(c->flags & FLAG_VAR_CYCLIC)) {
			accum_var(q, c, c_ctx);
			if (e) e->vgen = save_vgen;
		} else if (is_compound(c) && !is_ground(c)) {
			// Descend iteratively instead of recursing; defer the vgen
			// restore until this child's whole subtree is finished.
			vnode *cn = TPL_malloc(sizeof(vnode));

			if (!cn) {
				while ((n = (vnode*)list_pop_back(&stack)) != NULL)
					TPL_free(n);

				return;
			}

			cn->arity = get_arity(c);
			cn->p1 = c + 1;
			cn->p1_ctx = c_ctx;
			cn->depth = n->depth + 1;
			cn->e = e;
			cn->save_vgen = save_vgen;
			list_push_back(&stack, cn);
		} else {
			// atom, number, ground compound or cyclic var: nothing to collect
			if (e) e->vgen = save_vgen;
		}
	}
}

void collect_vars(query *q, cell *p1, pl_ctx p1_ctx)
{
	if (++q->vgen == 0) q->vgen = 1;
	q->tab_idx = 0;
	TPL_free(q->tabs);
	q->tabs = NULL;
	q->tabs_size = INITIAL_VAR_TABLE_SIZE;
	ENSURE(q->vars = sl_create(NULL, NULL, NULL));
	collect_vars_internal(q, p1, p1_ctx, 0);
	sl_destroy(q->vars);
	q->vars = NULL;
}

static bool has_vars_internal(query *q, cell *p1, pl_ctx p1_ctx, unsigned depth)
{
	if (is_var(p1))
		return true;

	if (!is_compound(p1) || is_ground(p1))
		return false;

	// Transform recursion into stack iteration...

	list stack = {0};
	snode *n = TPL_malloc(sizeof(snode));
	n->c = p1;
	n->c_ctx = p1_ctx;
	list_push_back(&stack, n);

	while ((n = (snode*)list_pop_front(&stack)) != NULL) {
		cell *p1 = n->c;
		pl_ctx p1_ctx = n->c_ctx;
		TPL_free(n);

		if (!is_compound(p1)) {
			if (has_vars_internal(q, p1, p1_ctx, depth+1)) {
				while ((n = (snode*)list_pop_front(&stack)) != NULL)
					TPL_free(n);

				return true;
			}

			continue;
		}

		bool any = false;
		uint32_t arity = get_arity(p1);
		p1++;

		while (arity--) {
			cell *c = p1;
			pl_ctx c_ctx = p1_ctx;
			slot *e = NULL;
			uint32_t save_vgen = 0;
			int both = 0;

			DEREF_VAR(any, both, save_vgen, e, e->vgen, c, c_ctx, q->vgen);

			if (is_var(c)) {
				while ((n = (snode*)list_pop_front(&stack)) != NULL)
					TPL_free(n);

				return true;
			}

			if (!both && is_compound(c) && !is_ground(c)) {
				n = TPL_malloc(sizeof(snode));
				n->c = c;
				n->c_ctx = c_ctx;
				list_push_back(&stack, n);
			} else if (e)
				e->vgen = save_vgen;

			p1 += p1->num_cells;
		}
	}

	return false;
}

bool has_vars(query *q, cell *p1, pl_ctx p1_ctx)
{
	if (++q->vgen == 0) q->vgen = 1;
	return has_vars_internal(q, p1, p1_ctx, 0);
}

static void cyclic_stack_abort(list *stack)
{
	vnode *n;

	while ((n = (vnode*)list_pop_back(stack)) != NULL) {
		if (n->e)
			n->e->vgen = n->save_vgen;

		TPL_free(n);
	}
}

// Stack iteration as in collect_vars_internal(): each frame walks one
// compound's arguments left-to-right; vgen restore for the slot that led
// here is deferred until the frame pops (recursive-return point). That
// keeps ancestor marks live for cycle detection and removes the need for
// a post-pass RESTORE_VAR over list spines.
static bool is_cyclic_term_internal(query *q, cell *p1, pl_ctx p1_ctx, unsigned depth)
{
	if (depth >= g_max_depth)
		return true;

	if (!is_compound(p1) || is_ground(p1))
		return false;

	list stack = {0};
	vnode *n = TPL_malloc(sizeof(vnode));

	if (!n)
		return true;

	n->arity = get_arity(p1);
	n->p1 = p1 + 1;
	n->p1_ctx = p1_ctx;
	n->depth = depth;
	n->e = NULL;
	n->save_vgen = 0;
	list_push_back(&stack, n);

	while ((n = (vnode*)list_back(&stack)) != NULL) {
		if (!n->arity) {
			slot *pending_e = n->e;
			uint32_t pending_vgen = n->save_vgen;

			list_pop_back(&stack);
			TPL_free(n);

			if (pending_e)
				pending_e->vgen = pending_vgen;

			continue;
		}

		if (n->depth >= g_max_depth) {
			cyclic_stack_abort(&stack);
			return true;
		}

		n->arity--;
		cell *c = n->p1;
		pl_ctx c_ctx = n->p1_ctx;
		slot *e = NULL;
		uint32_t save_vgen = 0;
		bool any = false;
		int both = 0;

		DEREF_VAR(any, both, save_vgen, e, e->vgen, c, c_ctx, q->vgen);
		n->p1 += n->p1->num_cells;

		if (both) {
			cyclic_stack_abort(&stack);
			return true;
		}

		if (is_compound(c) && !is_ground(c)) {
			vnode *cn = TPL_malloc(sizeof(vnode));

			if (!cn) {
				cyclic_stack_abort(&stack);
				return true;
			}

			cn->arity = get_arity(c);
			cn->p1 = c + 1;
			cn->p1_ctx = c_ctx;
			// List spines do not consume depth (same as the old iterative
			// is_cyclic_term_lists); only non-list compounds do.
			cn->depth = is_iso_list(c) ? n->depth : n->depth + 1;
			cn->e = e;
			cn->save_vgen = save_vgen;
			list_push_back(&stack, cn);
		} else if (e)
			e->vgen = save_vgen;
	}

	return false;
}

bool is_cyclic_term(query *q, cell *p1, pl_ctx p1_ctx)
{
	if (++q->vgen == 0) q->vgen = 1;
	return is_cyclic_term_internal(q, p1, p1_ctx, 0);
}

bool is_acyclic_term(query *q, cell *p1, pl_ctx p1_ctx)
{
	return !is_cyclic_term(q, p1, p1_ctx);
}

cell *term_next(query *q, cell *c, pl_ctx *c_ctx, bool *done)
{
	if (!is_iso_list(c)) {
		*done = true;
		return c;
	}

	c += 1;
	c += c->num_cells;
	c = deref(q, c, *c_ctx);
	*c_ctx = q->latest_ctx;
	return c;
}

// This uses Brent's algorithm...

cell *skip_max_list(query *q, cell *head, pl_ctx *head_ctx, pl_int max, pl_int *skip, cell *tmp)
{
	if (!head)
		return NULL;

	if (!max) {
		*skip = max;
		return head;
	}

	cell *slow;
	pl_int offset = 0;

LOOP:

	if (is_string(head)) {
		const char *src = C_STR(q, head);
		size_t len_src = C_STRLEN(q, head);
		const char *save_src = src;
		const bool bytes = is_bytes(head);

		while ((max-- > 0) && (len_src > 0)) {
			size_t len = bytes ? 1 : len_char_utf8(src);
			len_src -= len;
			src += len;
			*skip += 1;
		}

		unshare_cell(tmp);

		if (C_STRLEN(q, head) == (size_t)(src-save_src)) {
			make_atom(tmp, g_nil_s);
		} else if (src == save_src) {
			tmp = head;
		} else {
			make_stringn(tmp, src, C_STRLEN(q, head) - (src-save_src));

			// The remainder has to be walked the same way as the whole.

			if (bytes && is_string(tmp))
				tmp->flags |= FLAG_CSTR_BYTES;
		}

		*skip += offset;
		return tmp;
	}

	// Handle ISO lists...

	slow = head;
	pl_ctx slow_ctx = *head_ctx, fast_ctx = *head_ctx;
	bool done = false;
	cell *fast = term_next(q, head, &fast_ctx, &done);
	pl_int length = 1, cnt = 0;
	int power = 1;

	while (!done) {
		if ((fast == slow) && (fast_ctx == slow_ctx))
			break;

		if (length == power) {
			power *= 2;
			length = 0;
			slow = fast;
			slow_ctx = fast_ctx;
		}

		if (max == ++cnt) {
			*skip = cnt;
			*head_ctx = fast_ctx;
			return fast;
		}

		fast = term_next(q, fast, &fast_ctx, &done);

		if (is_string(slow)) {
			head = fast;
			max -= cnt + 1;
			max += 1;
			offset = cnt;
			goto LOOP;
		}

		++length;
	}

	if (done) {
		if (is_string(fast)) {
			cnt += C_STRLEN_UTF8(fast);
			*skip = cnt;
			make_atom(tmp, g_nil_s);
			return tmp;
		}

		*skip = cnt;
		*head_ctx = fast_ctx;
		return fast;
	}

	slow = fast = head;
	fast_ctx = slow_ctx = *head_ctx;

	while (length-- > 0) {
		fast = term_next(q, fast, &fast_ctx, &done);

		if (length == max)
			break;
	}

	pl_int len = 0;

	while (true) {
		if ((fast == slow) && (fast_ctx == slow_ctx))
			break;

		fast = term_next(q, fast, &fast_ctx, &done);
		slow = term_next(q, slow, &slow_ctx, &done);
		len++;
	}

	*skip = len;
	*head_ctx = slow_ctx;
	return slow;
}

bool check_list(query *q, cell *p1, pl_ctx p1_ctx, bool *is_partial, pl_int *skip_)
{
	pl_int skip = 0, max = 1000000000;
	pl_ctx c_ctx = p1_ctx;
	cell tmp = {0};

	if (is_partial)
		*is_partial = false;

	cell *c = skip_max_list(q, p1, &c_ctx, max, &skip, &tmp);
	unshare_cell(&tmp);

	if (skip_)
		*skip_ = skip;

	if (is_nil(c))
		return true;

	if (is_var(c)) {
		if (is_partial)
			*is_partial = true;
	}

	return false;
}
