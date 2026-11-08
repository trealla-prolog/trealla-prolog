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

	// A stack, descending at once so variables come in order of first occurrence; (e, save_vgen) is the slot that led here and its mark when done

	// A shared variable's binding is walked twice at most, enough to lift the counts inside past 1

	uint32_t once = 1, twice = 2;

	if (q->vgen == 1)
		once = 3;
	else if (q->vgen == 2)
		twice = 3;

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
			// Arguments done: mark the slot that led here walked once or twice
			slot *pending_e = n->e;
			uint32_t pending_vgen = n->save_vgen;

			list_pop_back(&stack);
			TPL_free(n);

			if (pending_e)
				set_vgen(q, pending_e, pending_vgen);

			continue;
		}

		n->arity--;
		cell *c = n->p1;
		pl_ctx c_ctx = n->p1_ctx;
		slot *e = NULL;
		uint32_t save_vgen = 0;
		bool any = false;
		int both = 0;

		DEREF_VAR(any, both, save_vgen, e, c, c_ctx, q->vgen);
		n->p1 += n->p1->num_cells;

		if (both) {
			if (e) set_vgen(q, e, save_vgen);
			continue;
		}

		if (is_var(c) && !(c->flags & FLAG_VAR_CYCLIC)) {
			accum_var(q, c, c_ctx);
			if (e) set_vgen(q, e, save_vgen);
		} else if (is_compound(c) && !is_ground(c)) {
			if (e && (save_vgen == twice)) {
				set_vgen(q, e, twice);
				continue;
			}

			// Descend; the slot is marked when the subtree is done
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
			cn->save_vgen = (save_vgen == once) ? twice : once;
			list_push_back(&stack, cn);
		} else {
			// atom, number, ground compound or cyclic var: nothing to collect
			if (e) set_vgen(q, e, save_vgen);
		}
	}
}

// Terms can share one table, each one's new variables from the tab_idx before its collect_vars_add()

void collect_vars_begin(query *q)
{
	q->tab_idx = 0;
	TPL_free(q->tabs);
	q->tabs = NULL;
	q->tabs_size = INITIAL_VAR_TABLE_SIZE;
	ENSURE(q->vars = sl_create(NULL, NULL, NULL));
}

void collect_vars_add(query *q, cell *p1, pl_ctx p1_ctx)
{
	if (++q->vgen == 0) q->vgen = 1;
	collect_vars_internal(q, p1, p1_ctx, 0);
}

void collect_vars_end(query *q)
{
	sl_destroy(q->vars);
	q->vars = NULL;
}

void collect_vars(query *q, cell *p1, pl_ctx p1_ctx)
{
	collect_vars_begin(q);
	collect_vars_add(q, p1, p1_ctx);
	collect_vars_end(q);
}

static bool has_vars_internal(query *q, cell *p1, pl_ctx p1_ctx, unsigned depth)
{
	if (is_var(p1))
		return true;

	if (!is_compound(p1) || is_ground(p1))
		return false;

	// Transform recursion into stack iteration...

	// Out of memory it reports variables, so ground/1 fails rather than wrongly succeeding

	list stack = {0};
	snode *n = TPL_malloc(sizeof(snode));

	if (!n)
		return true;

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

			DEREF_VAR(any, both, save_vgen, e, c, c_ctx, q->vgen);

			if (is_var(c)) {
				while ((n = (snode*)list_pop_front(&stack)) != NULL)
					TPL_free(n);

				return true;
			}

			if (!both && is_compound(c) && !is_ground(c)) {
				n = TPL_malloc(sizeof(snode));

				if (!n) {
					while ((n = (snode*)list_pop_front(&stack)) != NULL)
						TPL_free(n);

					return true;
				}

				n->c = c;
				n->c_ctx = c_ctx;
				list_push_back(&stack, n);
			} else if (e)
				set_vgen(q, e, save_vgen);

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

// One open compound of is_cyclic_term_internal()'s walk, kept in an array that starts on the C stack: a heap node each made printing a big term 5% slower

typedef struct {
	cell *p1;
	slot *e;
	pl_ctx p1_ctx;
	uint32_t arity;
	uint32_t save_vgen;
} cnode;

// A variable is marked q->vgen while its binding is walked and done after: met marked is a cycle, met done is skipped

static bool is_cyclic_term_internal(query *q, cell *p1, pl_ctx p1_ctx)
{
	if (!is_compound(p1) || is_ground(p1))
		return false;

	const uint32_t done = (q->vgen == 1) ? 2 : 1;
	cnode local[64], *stack = local;
	size_t cap = sizeof(local) / sizeof(local[0]), top = 0;
	bool cyclic = false;
	stack[top++] = (cnode){.p1 = p1 + 1, .p1_ctx = p1_ctx, .arity = get_arity(p1)};

	while (top) {
		cnode *n = &stack[top - 1];

		if (!n->arity) {
			if (n->e)
				set_vgen(q, n->e, done);

			top--;
			continue;
		}

		n->arity--;
		cell *c = n->p1;
		pl_ctx c_ctx = n->p1_ctx;
		slot *e = NULL;
		uint32_t save_vgen = 0;
		bool any = false;
		int both = 0;

		DEREF_VAR(any, both, save_vgen, e, c, c_ctx, q->vgen);
		n->p1 += n->p1->num_cells;

		if (both) {
			cyclic = true;
			break;
		}

		if (e && (save_vgen == done)) {
			set_vgen(q, e, done);
			continue;
		}

		if (is_compound(c) && !is_ground(c)) {
			if (top == cap) {
				cnode *grown = stack == local ? TPL_malloc(sizeof(cnode) * cap * 2) : TPL_realloc(stack, sizeof(cnode) * cap * 2);

				if (!grown) {
					cyclic = true;			// as before: no memory to look further answers cyclic
					break;
				}

				if (stack == local)
					memcpy(grown, local, sizeof(local));

				stack = grown;
				cap *= 2;
			}

			stack[top++] = (cnode){.p1 = c + 1, .e = e, .p1_ctx = c_ctx, .arity = get_arity(c), .save_vgen = save_vgen};
		} else if (e)
			set_vgen(q, e, done);
	}

	// Leaving early, put back the marks of the bindings still open
	while (cyclic && top) {
		cnode *n = &stack[--top];

		if (n->e)
			set_vgen(q, n->e, n->save_vgen);
	}

	if (stack != local)
		TPL_free(stack);

	return cyclic;
}

bool is_cyclic_term(query *q, cell *p1, pl_ctx p1_ctx)
{
	if (++q->vgen == 0) q->vgen = 1;
	return is_cyclic_term_internal(q, p1, p1_ctx);
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

			// The rest is walked as the whole is

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
