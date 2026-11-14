#include <stdlib.h>
#include <stdio.h>

#include "prolog.h"
#include "query.h"

#include "sre/re.h"

// The text "" is the atom [], whose name "[]" must not be matched against
static const char *sre_text(query *q, cell *c, size_t *n)
{
	*n = is_nil(c) ? 0 : C_STRLEN(q, c);
	return is_nil(c) ? "" : C_STR(q, c);
}

bool bif_sre_compile_2(query *q)
{
	GET_FIRST_ARG(p1,atom);
	GET_NEXT_ARG(p2,var);
	const char *pattern = C_STR(q, p1);
	unsigned char *buf;
	re_t reg = re_compile(pattern, &buf);
	if (!reg) return false;
	cell tmp = {0};
	tmp.tag = TAG_BLOB;
	tmp.flags = FLAG_MANAGED | FLAG_BLOB_SREGEX;
	tmp.num_cells = 1;
	tmp.val_blob = TPL_malloc(sizeof(blob));
	CHECKED(tmp.val_blob);
	tmp.val_blob->ptr = (void*)reg;
	tmp.val_blob->ptr2 = (void*)buf;
	tmp.val_blob->refcnt = 1;
	bool ok = unify(q, p2, p2_ctx, &tmp, q->st.cur_ctx);
	unshare_cell(&tmp);
	return ok;
}

bool bif_sre_matchp_4(query *q)
{
	GET_FIRST_ARG(p1,sregex);
	GET_NEXT_ARG(p2,atom);
	GET_NEXT_ARG(p3,atom_or_var);
	GET_NEXT_ARG(p4,atom_or_var);
	re_t re = (void*)p1->val_blob->ptr;
	size_t textlen;
	const char *text = sre_text(q, p2, &textlen);
	int len = 0;
	int off = re_matchp(re, text, &len);
	cell tmp;

	if (!len)
		make_atom(&tmp, g_nil_s);
	else
		make_slice(q, &tmp, p2, off, len);

	bool ok = unify(q, p3, p3_ctx, &tmp, q->st.cur_ctx);
	unshare_cell(&tmp);
	if (!ok) return false;

	if ((size_t)(off + len) >= textlen)
		make_atom(&tmp, g_nil_s);
	else
		make_slice(q, &tmp, p2, off + len, textlen-(off+len));

	ok = unify(q, p4, p4_ctx, &tmp, q->st.cur_ctx);
	unshare_cell(&tmp);
	return ok;
}

bool bif_sre_match_4(query *q)
{
	GET_FIRST_ARG(p1,atom);
	GET_NEXT_ARG(p2,atom);
	GET_NEXT_ARG(p3,atom_or_var);
	GET_NEXT_ARG(p4,atom_or_var);
	const char *pattern = C_STR(q, p1);
	size_t textlen;
	const char *text = sre_text(q, p2, &textlen);
	int len = 0;
	int off = re_match(pattern, text, &len);
	cell tmp;

	if (!len)
		make_atom(&tmp, g_nil_s);
	else
		make_slice(q, &tmp, p2, off, len);

	bool ok = unify(q, p3, p3_ctx, &tmp, q->st.cur_ctx);
	unshare_cell(&tmp);
	if (!ok) return false;

	if ((size_t)(off + len) >= textlen)
		make_atom(&tmp, g_nil_s);
	else
		make_slice(q, &tmp, p2, off + len, textlen-(off+len));

	ok = unify(q, p4, p4_ctx, &tmp, q->st.cur_ctx);
	unshare_cell(&tmp);
	return ok;
}

bool bif_sre_substp_4(query *q)
{
	GET_FIRST_ARG(p1,sregex);
	GET_NEXT_ARG(p2,atom);
	GET_NEXT_ARG(p3,atom_or_var);
	GET_NEXT_ARG(p4,atom_or_var);
	re_t re = (void*)p1->val_blob->ptr;
	size_t textlen;
	const char *text = sre_text(q, p2, &textlen);
	int len = 0;
	int off = re_matchp(re, text, &len);
	cell tmp;

	if (!len && textlen)
		make_stringn(&tmp, text, textlen);
	else if (!len || !off)
		make_atom(&tmp, g_nil_s);		// make_slice() and make_stringn() give '' when empty
	else
		make_slice(q, &tmp, p2, 0, off);

	bool ok = unify(q, p3, p3_ctx, &tmp, q->st.cur_ctx);
	unshare_cell(&tmp);
	if (!ok) return false;

	if ((size_t)(off + len) >= textlen)
		make_atom(&tmp, g_nil_s);
	else
		make_slice(q, &tmp, p2, off + len, textlen-(off+len));

	ok = unify(q, p4, p4_ctx, &tmp, q->st.cur_ctx);
	unshare_cell(&tmp);
	return ok;
}

bool bif_sre_subst_4(query *q)
{
	GET_FIRST_ARG(p1,atom);
	GET_NEXT_ARG(p2,atom);
	GET_NEXT_ARG(p3,atom_or_var);
	GET_NEXT_ARG(p4,atom_or_var);
	const char *pattern = C_STR(q, p1);
	size_t textlen;
	const char *text = sre_text(q, p2, &textlen);
	int len = 0;
	int off = re_match(pattern, text, &len);
	cell tmp;

	if (!len && textlen)
		make_stringn(&tmp, text, textlen);
	else if (!len || !off)
		make_atom(&tmp, g_nil_s);		// make_slice() and make_stringn() give '' when empty
	else
		make_slice(q, &tmp, p2, 0, off);

	bool ok = unify(q, p3, p3_ctx, &tmp, q->st.cur_ctx);
	unshare_cell(&tmp);
	if (!ok) return false;

	if ((size_t)(off + len) >= textlen)
		make_atom(&tmp, g_nil_s);
	else
		make_slice(q, &tmp, p2, off + len, textlen-(off+len));

	ok = unify(q, p4, p4_ctx, &tmp, q->st.cur_ctx);
	unshare_cell(&tmp);
	return ok;
}

builtins g_sregex_bifs[] =
{
	{"sre_compile", 2, bif_sre_compile_2, "+string,-string,", false, false, BLAH},
	{"sre_matchp", 4, bif_sre_matchp_4, "+string,+string,-string,-string,", false, false, BLAH},
	{"sre_match", 4, bif_sre_match_4, "+string,+string,-string,-string,", false, false, BLAH},
	{"sre_substp", 4, bif_sre_substp_4, "+string,+string,-string,-string,", false, false, BLAH},
	{"sre_subst", 4, bif_sre_subst_4, "+string,+string,-string,-string,", false, false, BLAH},

	{0}
};
