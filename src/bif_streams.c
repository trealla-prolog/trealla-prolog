#if defined(__sun)
#define __EXTENSIONS__
#endif
#define _XOPEN_SOURCE 700
#include <ctype.h>
#include "tpl_features.h"

#if TPL_FEATURE_FILESYSTEM
#include <dirent.h>
#endif

#include <errno.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include <sys/stat.h>

#ifndef _WIN32
#ifndef USE_MMAP
#define USE_MMAP 1
#endif
#if USE_MMAP
#include <sys/mman.h>
#endif
#endif

#include "history.h"
#include "files.h"
#include "module.h"
#include "network.h"
#include "query.h"

#define PROMPT ""

#ifdef _WIN32
#define mkdir(p1,p2) mkdir(p1)
#endif

#ifdef __wasi__
#include <limits.h>
#include <unistd.h>

/*
	realpath implementation borrowed from musl
	realpath is excluded from WASI because it doesn't have the concept of absolute paths
	(apparently)

	musl as a whole is licensed under the following standard MIT license:

	----------------------------------------------------------------------
	Copyright © 2005-2020 Rich Felker, et al.

	Permission is hereby granted, TPL_free of charge, to any person obtaining
	a copy of this software and associated documentation files (the
	"Software"), to deal in the Software without restriction, including
	without limitation the rights to use, copy, modify, merge, publish,
	distribute, sublicense, and/or sell copies of the Software, and to
	permit persons to whom the Software is furnished to do so, subject to
	the following conditions:

	The above copyright notice and this permission notice shall be
	included in all copies or substantial portions of the Software.

	THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
	EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
	MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
	IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY
	CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
	TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
	SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
	----------------------------------------------------------------------
*/

static size_t slash_len(const char *s)
{
	const char *s0 = s;
	while (*s == '/') s++;
	return s-s0;
}



char *realpath(const char *restrict filename, char *restrict resolved)
{
	char stack[PATH_MAX+1];
	char output[PATH_MAX];
	size_t p, q, l, l0, cnt=0, nup=0;
	int check_dir=0;

	if (!filename) {
		errno = EINVAL;
		return 0;
	}
	l = strnlen(filename, sizeof stack);
	if (!l) {
		errno = ENOENT;
		return 0;
	}
	if (l >= PATH_MAX) goto toolong;
	p = sizeof stack - l - 1;
	q = 0;
	memcpy(stack+p, filename, l+1);

	/* Main loop. Each iteration pops the next part from stack of
	 * remaining path components and consumes any slashes that follow.
	 * If not a link, it's moved to output; if a link, contents are
	 * pushed to the stack. */
restart:
	for (; ; p+=slash_len(stack+p)) {
		/* If stack starts with /, the whole component is / or //
		 * and the output state must be reset. */
		if (stack[p] == '/') {
			check_dir=0;
			nup=0;
			q=0;
			output[q++] = '/';
			p++;
			/* Initial // is special. */
			if (stack[p] == '/' && stack[p+1] != '/')
				output[q++] = '/';
			continue;
		}

		char *z = strchrnul(stack+p, '/');
		l0 = l = z-(stack+p);

		if (!l && !check_dir) break;

		/* Skip any . component but preserve check_dir status. */
		if (l==1 && stack[p]=='.') {
			p += l;
			continue;
		}

		/* Copy next component onto output at least temporarily, to
		 * call readlink, but wait to advance output position until
		 * determining it's not a link. */
		if (q && output[q-1] != '/') {
			if (!p) goto toolong;
			stack[--p] = '/';
			l++;
		}
		if (q+l >= PATH_MAX) goto toolong;
		memcpy(output+q, stack+p, l);
		output[q+l] = 0;
		p += l;

		int up = 0;
		if (l0==2 && stack[p-2]=='.' && stack[p-1]=='.') {
			up = 1;
			/* Any non-.. path components we could cancel start
			 * after nup repetitions of the 3-byte string "../";
			 * if there are none, accumulate .. components to
			 * later apply to cwd, if needed. */
			if (q <= 3*nup) {
				nup++;
				q += l;
				continue;
			}
			/* When previous components are already known to be
			 * directories, processing .. can skip readlink. */
			if (!check_dir) goto skip_readlink;
		}
		ssize_t k = readlink(output, stack, p);
		if (k==(ssize_t)p) goto toolong;
		if (!k) {
			errno = ENOENT;
			return 0;
		}
		if (k<0) {
			if (errno != EINVAL) return 0;
skip_readlink:
			check_dir = 0;
			if (up) {
				while(q && output[q-1]!='/') q--;
				if (q>1 && (q>2 || output[0]!='/')) q--;
				continue;
			}
			if (l0) q += l;
			check_dir = stack[p];
			continue;
		}
		if (++cnt == SYMLOOP_MAX) {
			errno = ELOOP;
			return 0;
		}

		/* If link contents end in /, strip any slashes already on
		 * stack to avoid /->// or //->/// or spurious toolong. */
		if (stack[k-1]=='/') while (stack[p]=='/') p++;
		p -= k;
		memmove(stack+p, stack, k);

		/* Skip the stack advancement in case we have a new
		 * absolute base path. */
		goto restart;
	}

 	output[q] = 0;

	if (output[0] != '/') {
		if (!getcwd(stack, sizeof stack)) return 0;
		l = strlen(stack);
		/* Cancel any initial .. components. */
		p = 0;
		while (nup--) {
			while(l>1 && stack[l-1]!='/') l--;
			if (l>1) l--;
			p += 2;
			if (p<q) p++;
		}
		if (q-p && stack[l-1]!='/') stack[l++] = '/';
		if (l + (q-p) + 1 >= PATH_MAX) goto toolong;
		memmove(output + l, output + p, q - p + 1);
		memcpy(output, stack, l);
		q = l + q-p;
	}

	struct stat st = {0};
	if (resolved) {
		if (stat(resolved, &st) == -1) return NULL;
		return memcpy(resolved, output, q+1);
	}
	if (stat(output, &st) == -1) return NULL;
	return TPL_strdup(output);

toolong:
	errno = ENAMETOOLONG;
	return 0;
}
/* end of code borrowed from musl */
#endif

#ifdef _WIN32
#include <windows.h>

char *realpath(const char *path, char resolved_path[PATH_MAX])
{
  char *return_path = 0;

  if (path) //Else EINVAL
  {
    if (resolved_path)
    {
      return_path = resolved_path;
    }
    else
    {
      //Non standard extension that glibc uses
      return_path = TPL_malloc(PATH_MAX);
    }

    if (return_path) //Else EINVAL
    {
      //This is a Win32 API function similar to what realpath() is supposed to do
      size_t size = GetFullPathNameA(path, PATH_MAX, return_path, 0);

      //GetFullPathNameA() returns a size larger than buffer if buffer is too small
      if (size > PATH_MAX)
      {
        if (return_path != resolved_path) //Malloc'd buffer - Unstandard extension retry
        {
          size_t new_size;

          TPL_free(return_path);
          return_path = TPL_malloc(size);

          if (return_path)
          {
            new_size = GetFullPathNameA(path, size, return_path, 0); //Try again

            if (new_size > size) //If it's still too large, we have a problem, don't try again
            {
              TPL_free(return_path);
              return_path = 0;
              errno = ENAMETOOLONG;
            }
            else
            {
              size = new_size;
            }
          }
          else
          {
            //I wasn't sure what to return here, but the standard does say to return EINVAL
            //if resolved_path is null, and in this case we couldn't TPL_malloc large enough buffer
            errno = EINVAL;
          }
        }
        else //resolved_path buffer isn't big enough
        {
          return_path = 0;
          errno = ENAMETOOLONG;
        }
      }

      //GetFullPathNameA() returns 0 if some path resolve problem occured
      if (!size)
      {
        if (return_path != resolved_path) //Malloc'd buffer
        {
          TPL_free(return_path);
        }

        return_path = 0;

        //Convert MS errors into standard errors
        switch (GetLastError())
        {
          case ERROR_FILE_NOT_FOUND:
            errno = ENOENT;
            break;

          case ERROR_PATH_NOT_FOUND: case ERROR_INVALID_DRIVE:
            errno = ENOTDIR;
            break;

          case ERROR_ACCESS_DENIED:
            errno = EACCES;
            break;

          default: //Unknown Error
            errno = EIO;
            break;
        }
      }

      //If we get to here with a valid return_path, we're still doing good
      if (return_path)
      {
        struct stat stat_buffer;

        //Make sure path exists, stat() returns 0 on success
        if (stat(return_path, &stat_buffer))
        {
          if (return_path != resolved_path)
          {
            TPL_free(return_path);
          }

          return_path = 0;
          //stat() will set the correct errno for us
        }
        //else we succeeded!
      }
    }
    else
    {
      errno = EINVAL;
    }
  }
  else
  {
    errno = EINVAL;
  }

  return return_path;
}
#endif

// FIXME: there should be one overall alias map, not one per stream

int get_named_stream(prolog *pl, const char *name, size_t len)
{
	prolog_lock(pl);

	for (int i = 0; i < MAX_STREAMS; i++) {
		stream *str = &pl->streams[i];

		if (!str->fp || !str->alias)
			continue;

		if (sl_get(str->alias, name, NULL)) {
			prolog_unlock(pl);
			return i;
		}

		if (str->filename && (strlen(str->filename) == len)
			&& !strncmp(str->filename, name, len)) {
			prolog_unlock(pl);
			return i;
		}
	}

	prolog_unlock(pl);
	return -1;
}

int get_stream(query *q, cell *p1)
{
	if (is_atom(p1)) {
		int n = get_named_stream(q->pl, C_STR(q, p1), C_STRLEN(q, p1));

		if (n < 0)
			return -1;

		return n;
	}

	if (p1->tag != TAG_INT)
		return -1;

	if (!(p1->flags & FLAG_INT_STREAM))
		return -1;

	int n = get_smallint(p1);

	if (!q->pl->streams[n].fp)
		return -1;

	return n;
}

int new_stream(prolog *pl)
{
	prolog_lock(pl);

	for (int i = 3; i < MAX_STREAMS; i++) {
		stream *str = &pl->streams[i];

		if (str->is_active)
			continue;

		str->timeout_ms = 0;
		str->is_pipe = false;
		str->is_popen = false;
		str->is_socket = false;
		str->is_alias = false;
		str->is_engine = false;
		str->is_map = false;
		str->is_memory = false;
		str->captures = NULL;
		str->idx = i;
		str->bom = false;
		str->repo = false;
		str->binary = false;
		str->at_end_of_file = false;
		str->ungetch = 0;
		str->did_getc = false;
		str->invalid_pending = false;
		str->fp = str->fp_in = str->fp_out = NULL;
		str->is_active = true;
		prolog_unlock(pl);
		return i;
	}

	prolog_unlock(pl);
	return -1;
}

bool is_closed_stream(prolog *pl, cell *p1)
{
	if (!(p1->flags & FLAG_INT_STREAM))
		return false;

	if (pl->streams[get_smallint(p1)].fp)
		return false;

	return true;
}

static void add_stream_properties(query *q, int n)
{
	stream *str = &q->pl->streams[n];
	char tmpbuf[1024*8];
	char *dst = tmpbuf;
	*dst = '\0';
	off_t pos = !str->is_socket && !str->is_map && !str->is_engine ? ftello(str->fp_out) : 0;
	bool at_end_of_file = false;

	if (!str->at_end_of_file && (n > 2) && !str->is_socket && !str->is_engine && !str->is_map && !str->is_pipe && !str->p && str->filename) {
#if 0
		if (str->p) {
			if (str->p->srcptr && *str->p->srcptr) {
				str->ungetch = get_char_utf8((const char**)&str->p->srcptr);
			}
		}
#endif

		int ch = str->ungetch ? str->ungetch : tpl_getc(str);

		if (str->ungetch)
			;
		else if (feof(str->fp_in) || ferror(str->fp_in)) {
			clearerr(str->fp_in);

			if (str->eof_action != eof_action_reset)
				at_end_of_file = true;
		} else
			str->ungetch = ch;
	}

	sliter *iter = sl_first(str->alias);

	while (sl_next(iter, NULL)) {
		const char *alias = sl_key(iter);
		char *dst2 = formatted(alias, strlen(alias), false, false);
		dst += snprintf(dst, sizeof(tmpbuf)-strlen(tmpbuf), "'$stream_property'(%d, alias('%s')).\n", n, dst2);
		TPL_free(dst2);
	}

	sl_done(iter);

	if (!str->is_engine && !str->is_map) {
		if (str->filename) {
			char *dst2 = formatted(str->filename, strlen(str->filename), false, false);
			dst += snprintf(dst, sizeof(tmpbuf)-strlen(tmpbuf), "'$stream_property'(%d, file_name('%s')).\n", n, dst2);
			TPL_free(dst2);
		}

		dst += snprintf(dst, sizeof(tmpbuf)-strlen(tmpbuf), "'$stream_property'(%d, file_no(%u)).\n", n, fileno(str->fp_in));

		dst += snprintf(dst, sizeof(tmpbuf)-strlen(tmpbuf), "'$stream_property'(%d, file(%llu)).\n", n, (unsigned long long)(size_t)str->fp_in);
		dst += snprintf(dst, sizeof(tmpbuf)-strlen(tmpbuf), "'$stream_property'(%d, mode(%s)).\n", n, str->mode);
		dst += snprintf(dst, sizeof(tmpbuf)-strlen(tmpbuf), "'$stream_property'(%d, type(%s)).\n", n, str->binary ? "binary" : "text");
		dst += snprintf(dst, sizeof(tmpbuf)-strlen(tmpbuf), "'$stream_property'(%d, line_count(%d)).\n", n, str->p ? str->p->line_num : 1);
		dst += snprintf(dst, sizeof(tmpbuf)-strlen(tmpbuf), "'$stream_property'(%d, position(%llu)).\n", n, (unsigned long long)(pos != -1 ? pos : 0));
		dst += snprintf(dst, sizeof(tmpbuf)-strlen(tmpbuf), "'$stream_property'(%d, reposition(%s)).\n", n, (n < 3) || str->is_socket ? "false" : "true");
		dst += snprintf(dst, sizeof(tmpbuf)-strlen(tmpbuf), "'$stream_property'(%d, end_of_stream(%s)).\n", n, str->at_end_of_file ? "past" : at_end_of_file ? "at" : "not");
		dst += snprintf(dst, sizeof(tmpbuf)-strlen(tmpbuf), "'$stream_property'(%d, eof_action(%s)).\n", n, str->eof_action == eof_action_eof_code ? "eof_code" : str->eof_action == eof_action_error ? "error" : str->eof_action == eof_action_reset ? "reset" : "none");

		if (!str->binary) {
			dst += snprintf(dst, sizeof(tmpbuf)-strlen(tmpbuf), "'$stream_property'(%d, bom(%s)).\n", n, str->bom ? "true" : "false");
			dst += snprintf(dst, sizeof(tmpbuf)-strlen(tmpbuf), "'$stream_property'(%d, encoding('%s')).\n", n, "UTF-8");
		}

		if (!strcmp(str->mode, "read"))
			dst += snprintf(dst, sizeof(tmpbuf)-strlen(tmpbuf), "'$stream_property'(%d, input).\n", n);
		else
			dst += snprintf(dst, sizeof(tmpbuf)-strlen(tmpbuf), "'$stream_property'(%d, output).\n", n);

		dst += snprintf(dst, sizeof(tmpbuf)-strlen(tmpbuf), "'$stream_property'(%d, newline(%s)).\n", n, "posix");
	}

	if (str->is_engine)
		dst += snprintf(dst, sizeof(tmpbuf)-strlen(tmpbuf), "'$stream_property'(%d, engine(true)).\n", n);
	else if (str->is_map)
		dst += snprintf(dst, sizeof(tmpbuf)-strlen(tmpbuf), "'$stream_property'(%d, skiplist(true)).\n", n);
	else if (str->is_alias)
		dst += snprintf(dst, sizeof(tmpbuf)-strlen(tmpbuf), "'$stream_property'(%d, alias(true)).\n", n);

	parser *p = parser_create(q->st.m);

	if (!p)
		return;

	p->srcptr = tmpbuf;
	p->is_consulting = true;
	tokenize(p, false, false);
	parser_destroy(p);
}

static bool del_stream_properties(query *q, int n)
{
	cell *tmp = alloc_heap(q, 3);
	CHECKED(tmp);
	make_atom(tmp+0, g_sys_stream_property_s);
	make_int(tmp+1, n);
	int vnbr = create_vars(q, 1);
	CHECKED(vnbr != -1);
	make_ref(tmp+2, vnbr, q->st.cur_ctx);
	tmp->num_cells = 3;
	set_arity(tmp, 2);
	q->retry = QUERY_OK;

	while (do_retract(q, tmp, q->st.cur_ctx, DO_RETRACTALL)) {
		if (q->did_throw) return false;
		q->retry = QUERY_RETRY;
		retry_choice(q);
	}

	q->retry = QUERY_OK;
	return true;
}

static bool do_stream_property(query *q)
{
	GET_FIRST_ARG(pstr,any);
	GET_NEXT_ARG(p1,any);
	int n = get_stream(q, pstr);
	stream *str = &q->pl->streams[n];
	cell *c = p1 + 1;
	c = deref(q, c, p1_ctx);
	pl_ctx c_ctx = q->latest_ctx;

	if (!CMP_STRING_TO_CSTR(q, p1, "file_name")) {
		cell tmp;
		make_cstring(&tmp, str->filename);
		bool ok = unify(q, c, c_ctx, &tmp, q->st.cur_ctx);
		unshare_cell(&tmp);
		return ok;
	}

	if (!CMP_STRING_TO_CSTR(q, p1, "file_no")) {
		if (!str->fp_in)
			return false;

		cell tmp;
		make_int(&tmp, fileno(str->fp_in));
		bool ok = unify(q, c, c_ctx, &tmp, q->st.cur_ctx);
		return ok;
	}

	if (!CMP_STRING_TO_CSTR(q, p1, "file")) {
		if (!str->fp_in)
			return false;

		cell tmp;
		make_uint(&tmp, (size_t)str->fp_in);
		bool ok = unify(q, c, c_ctx, &tmp, q->st.cur_ctx);
		return ok;
	}

	if (!CMP_STRING_TO_CSTR(q, p1, "alias")) {
		cell tmp;
		sliter *iter = sl_first(str->alias);
		bool ok = false;

		while (sl_next(iter, NULL)) {
			const char *alias = sl_key(iter);
			make_cstring(&tmp, alias);
			ok = unify(q, c, c_ctx, &tmp, q->st.cur_ctx);
			unshare_cell(&tmp);
			if (ok) break;
		}

		sl_done(iter);
		return ok;
	}

	if (!CMP_STRING_TO_CSTR(q, p1, "mode")) {
		cell tmp;
		make_atom(&tmp, new_atom(q->pl, str->mode));
		return unify(q, c, c_ctx, &tmp, q->st.cur_ctx);
	}

	if (!CMP_STRING_TO_CSTR(q, p1, "engine")) {
		cell tmp;
		make_atom(&tmp, new_atom(q->pl, str->is_engine?"true":"false"));
		return unify(q, c, c_ctx, &tmp, q->st.cur_ctx);
	}

	if (!CMP_STRING_TO_CSTR(q, p1, "skiplist")) {
		cell tmp;
		make_atom(&tmp, new_atom(q->pl, str->is_map?"true":"false"));
		return unify(q, c, c_ctx, &tmp, q->st.cur_ctx);
	}

	if (!CMP_STRING_TO_CSTR(q, p1, "bom") && !str->binary) {
		cell tmp;
		make_atom(&tmp, new_atom(q->pl, str->bom?"true":"false"));
		return unify(q, c, c_ctx, &tmp, q->st.cur_ctx);
	}

	if (!CMP_STRING_TO_CSTR(q, p1, "type")) {
		cell tmp;
		make_atom(&tmp, new_atom(q->pl, str->binary?"binary":"text"));
		return unify(q, c, c_ctx, &tmp, q->st.cur_ctx);
	}

	if (!CMP_STRING_TO_CSTR(q, p1, "reposition")) {
		cell tmp;
		make_atom(&tmp, new_atom(q->pl, str->is_socket || (n <= 2) ? "false" : str->repo ? "true" : "false"));
		return unify(q, c, c_ctx, &tmp, q->st.cur_ctx);
	}

	if (!CMP_STRING_TO_CSTR(q, p1, "encoding") && !str->binary) {
		cell tmp;
		make_atom(&tmp, new_atom(q->pl, "UTF-8"));
		return unify(q, c, c_ctx, &tmp, q->st.cur_ctx);
	}

	if (!CMP_STRING_TO_CSTR(q, p1, "newline")) {
		cell tmp;
		make_atom(&tmp, new_atom(q->pl, "posix"));
		return unify(q, c, c_ctx, &tmp, q->st.cur_ctx);
	}

	if (!CMP_STRING_TO_CSTR(q, p1, "input"))
		return !strcmp(str->mode, "read");

	if (!CMP_STRING_TO_CSTR(q, p1, "output"))
		return strcmp(str->mode, "read");

	if (!CMP_STRING_TO_CSTR(q, p1, "eof_action") && is_stream(pstr)) {
		cell tmp;

		if (str->eof_action == eof_action_eof_code)
			make_atom(&tmp, new_atom(q->pl, "eof_code"));
		else if (str->eof_action == eof_action_error)
			make_atom(&tmp, new_atom(q->pl, "error"));
		else if (str->eof_action == eof_action_reset)
			make_atom(&tmp, new_atom(q->pl, "reset"));
		else
			make_atom(&tmp, new_atom(q->pl, "none"));

		return unify(q, c, c_ctx, &tmp, q->st.cur_ctx);
	}

	if (!CMP_STRING_TO_CSTR(q, p1, "end_of_stream") && is_stream(pstr)) {
		bool at_end_of_file = false;

		// A speculative tpl_getc() here would misreport a live, merely
		// idle socket as at EOF: the fd is always non-blocking now (see
		// bif_net.c), so "nothing pending yet" and "peer closed" both
		// leave ferror() set with no data. add_stream_properties() above
		// already excludes sockets from the equivalent peek for the same
		// reason.
		if (!str->at_end_of_file && (n > 2) && !str->is_socket) {
			if (str->p) {
				if (str->p->srcptr && *str->p->srcptr) {
					int ch = get_char_utf8((const char**)&str->p->srcptr);
					str->ungetch = ch;
				}
			}

			int ch = str->ungetch ? str->ungetch : tpl_getc(str);

			if (str->ungetch)
				;
			else if (feof(str->fp_in) || ferror(str->fp_in)) {
				clearerr(str->fp_in);

				if (str->eof_action != eof_action_reset)
					at_end_of_file = true;
			} else
				str->ungetch = ch;
		}

		cell tmp;

		if (str->at_end_of_file)
			make_atom(&tmp, new_atom(q->pl, "past"));
		else if (at_end_of_file)
			make_atom(&tmp, new_atom(q->pl, "at"));
		else
			make_atom(&tmp, new_atom(q->pl, "not"));

		return unify(q, c, c_ctx, &tmp, q->st.cur_ctx);
	}

	if (!CMP_STRING_TO_CSTR(q, p1, "position") && !is_var(pstr)) {
		pl_int pos = ftello(str->fp_out);

		if (str->ungetch)
			pos -= put_len_utf8(str->ungetch);

		cell tmp;
		make_int(&tmp, pos);
		return unify(q, c, c_ctx, &tmp, q->st.cur_ctx);
	}

	if (!CMP_STRING_TO_CSTR(q, p1, "line_count") && !is_var(pstr)) {
		cell tmp;
		make_int(&tmp, str->p->line_num);
		return unify(q, c, c_ctx, &tmp, q->st.cur_ctx);
	}

	return false;
}

static void clear_streams_properties(query *q)
{
	cell tmp;
	make_atom(&tmp, g_sys_stream_property_s);
	tmp.num_cells = 1;
	set_arity(&tmp, 2);

	predicate *pr = find_predicate(q->st.m, &tmp);

	if (pr) {
		for (rule *r = pr->head; r;) {
			rule *save = r;
			r = r->next;
			retract_from_db(pr->m, save);
		}

		pr->head = pr->tail = NULL;
		pr->cnt = 0;
	}
}

static const char *s_properties =
	"alias,file_name,mode,encoding,type,line_count,"			\
	"position,reposition,end_of_stream,eof_action,"				\
	"input,output,newline,engine,skiplist";

static bool bif_iso_stream_property_2(query *q)
{
	GET_FIRST_ARG(pstr,any);
	GET_NEXT_ARG(p1,any);

	if (!is_stream_or_var(pstr)) {
		if (is_closed_stream(q->pl, pstr))
			return throw_error(q, pstr, q->st.cur_ctx, "existence_error", "stream");
		else
			return throw_error(q, pstr, q->st.cur_ctx, "domain_error", "stream");
	}

	if (get_arity(p1) > 1)
		return throw_error(q, p1, p1_ctx, "domain_error", "stream_property");

	if (!is_var(p1) && !is_callable(p1))
		return throw_error(q, p1, p1_ctx, "domain_error", "stream_property");

	if (!is_var(pstr) && !is_var(p1))
		return do_stream_property(q);

	if (!q->retry) {
		clear_streams_properties(q);

		for (int i = 0; i < MAX_STREAMS; i++) {
			if (!q->pl->streams[i].fp)
				continue;

			add_stream_properties(q, i);
		}
	}

	CHECKED(init_tmp_heap(q));
	cell *tmp = clone_term_to_tmp(q, q->st.instr, q->st.cur_ctx);
	CHECKED(tmp);
	tmp->val_off = g_sys_stream_property_s;

	if (match_clause(q, tmp, q->st.cur_ctx, NULL, DO_CLAUSE) != true) {
		clear_streams_properties(q);

		if (is_callable(p1) && !strstr(s_properties, C_STR(q, p1)))
			return throw_error(q, p1, p1_ctx, "domain_error", "stream_property");

		return false;
	}

	GET_FIRST_ARG(pstrx,integer);
	pstrx->flags |= FLAG_INT_STREAM;
	return true;
}

bool valid_list(query *q, cell *c, pl_ctx c_ctx)
{
	while (is_iso_list(c)) {
		c = c + 1;
		c += c->num_cells;
		c = deref(q, c, c_ctx);
		c_ctx = q->latest_ctx;

		if (!is_iso_list_or_nil_or_var(c))
			return false;
	}

	return true;
}

#ifdef _WIN32
#include <windows.h>
#include <io.h>

/* macro definitions extracted from git/git-compat-util.h */
#define PROT_READ  1
#define PROT_WRITE 2
#define MAP_FAILED ((void*)-1)

/* macro definitions extracted from /usr/include/bits/mman.h */
#define MAP_SHARED	0x01		/* Share changes.  */
#define MAP_PRIVATE	0x02		/* Changes are private.  */

static void *mmap(void *start, size_t length, int prot, int flags, int fd, off_t offset)
{
	size_t len;
	struct stat st;
	uint64_t o = offset;
	uint32_t l = o & 0xFFFFFFFF;
	uint32_t h = (o >> 32) & 0xFFFFFFFF;

	if (!fstat(fd, &st))
		len = (size_t) st.st_size;
	else {
		fprintf(stderr, "ERROR: mmap could not determine filesize");
		return NULL;
	}

	if ((length + offset) > len)
		length = len - offset;

	if (!(flags & MAP_PRIVATE)) {
		fprintf(stderr, "ERROR: Invalid usage of mmap");
		return NULL;
	}

	HANDLE hmap = CreateFileMapping((HANDLE)_get_osfhandle(fd), 0, PAGE_WRITECOPY, 0, 0, 0);

	if (!hmap) {
		fprintf(stderr, "ERROR: CreateFileMapping failed");
		return NULL;
	}

	void *temp = MapViewOfFileEx(hmap, FILE_MAP_COPY, h, l, length, start);

	if (!CloseHandle(hmap))
		fprintf(stderr, "Unable to close file mapping handle\n");

	return temp ? temp : MAP_FAILED;
}
#endif


// Release a stream slot that was handed out by new_stream() but never
// finished being built. open/4 allocates filename, mode and the alias
// skiplist BEFORE parsing options, and the option loop has a dozen
// error exits; each one used to return straight out. The slot stayed
// is_active, so 1024 bad-option opens exhausted MAX_STREAMS and every
// later open/4 raised resource_error(too_many_streams). The paths that
// did clear is_active still leaked the three allocations.
//
// Not stream_close(): that assumes a fully built stream and would
// tpl_close() a NULL fp. Here fp may or may not exist, so close it
// only if it does.

void unwind_stream(query *q, int n)
{
	stream *str = &q->pl->streams[n];

	if (str->fp && (str->fp != stdin) && (str->fp != stdout) && (str->fp != stderr))
		fclose(str->fp);

	str->fp = str->fp_in = str->fp_out = NULL;
	parser_destroy(str->p);
	str->p = NULL;
	sl_destroy(str->alias);
	str->alias = NULL;
	TPL_free(str->filename);
	str->filename = NULL;
	TPL_free(str->mode);
	str->mode = NULL;
	TPL_free(str->addr);
	str->addr = NULL;
	TPL_free(str->data);
	str->data = NULL;
	str->is_active = false;
}


static bool bif_iso_open_4(query *q)
{
	GET_FIRST_ARG(p1,source_sink);
	GET_NEXT_ARG(p2,atom);
	GET_NEXT_ARG(p3,var);
	GET_NEXT_ARG(p4,list_or_nil);

	// Everything that can throw happens BEFORE new_stream(), so that a
	// bad call never takes a stream slot it then has to give back. The
	// old shape took the slot first and every one of the twenty-odd
	// error exits had to remember unwind_stream() - see bif_threads.c
	// and bif_os.c for the same change.
	//
	// Order of errors is unchanged, with one deliberate addition: in
	// read mode a missing file is reported here rather than after
	// fopen() fails, which is the common case and the one that used to
	// allocate a slot, a filename, an alias table and a mode string
	// only to throw them away.
	//
	// Two things need the slot number and so can only be applied after
	// the commit: alias(current_input/output/error), which is recorded
	// in std_alias, and a plain alias(A), recorded in alias.

	// --- validate the source_sink. Shape only, nothing allocated yet.

	stream *oldstr = NULL;

	if (is_compound(p1) && (get_arity(p1) == 1) && !CMP_STRING_TO_CSTR(q, p1, "stream")) {
		int oldn = get_stream(q, p1+1);

		if (oldn < 0)
			return throw_error(q, p1, p1_ctx, "type_error", "not_a_stream");

		oldstr = &q->pl->streams[oldn];
	} else if (!is_atom(p1) && !is_iso_list(p1))
		return throw_error(q, p1, p1_ctx, "domain_error", "source_sink");
	else if (is_iso_list(p1) && !scan_is_chars_list(q, p1, p1_ctx, true))
		return throw_error(q, p1, p1_ctx, "type_error", "atom");

	// --- validate the options, collecting what the commit will need.

	cell *alias = NULL;
	int std_alias = 0;			// 0 none, 1 input, 2 output, 3 error
	bool binary = false, repo = true;
	uint8_t eof_action = eof_action_eof_code;
	bool bom_specified = false, use_bom = false, is_alias = false;

#if USE_MMAP
	cell *mmap_var = NULL;
	pl_ctx mmap_ctx = 0;
#endif

	PROLOG_LIST_HANDLER(p4);

	while (is_list(p4)) {
		cell *h = PROLOG_LIST_HEAD(p4);
		cell *c = deref(q, h, p4_ctx);
		pl_ctx c_ctx = q->latest_ctx;

		if (is_var(c))
			return throw_error(q, c, q->latest_ctx, "instantiation_error", "args_not_sufficiently_instantiated");

		cell *name = c + 1;
		name = deref(q, name, c_ctx);

		if (!CMP_STRING_TO_CSTR(q, c, "mmap")) {
#if USE_MMAP
			mmap_var = name;
			mmap_var = deref(q, mmap_var, q->latest_ctx);
			mmap_ctx = q->latest_ctx;
#endif
		} else if (!CMP_STRING_TO_CSTR(q, c, "encoding")) {
			if (is_var(name))
				return throw_error(q, name, q->latest_ctx, "instantiation_error", "stream_option");

			if (!is_atom(name))
				return throw_error(q, c, c_ctx, "domain_error", "stream_option");
		} else if (!CMP_STRING_TO_CSTR(q, c, "alias")) {
			if (is_var(name))
				return throw_error(q, name, q->latest_ctx, "instantiation_error", "stream_option");

			if (!is_atom(name))
				return throw_error(q, c, c_ctx, "domain_error", "stream_option");

			if (get_named_stream(q->pl, C_STR(q, name), C_STRLEN(q, name)) >= 0)
				return throw_error(q, c, c_ctx, "permission_error", "open,source_sink");

			if (!CMP_STRING_TO_CSTR(q, name, "current_input"))
				std_alias = 1;
			else if (!CMP_STRING_TO_CSTR(q, name, "current_output"))
				std_alias = 2;
			else if (!CMP_STRING_TO_CSTR(q, name, "current_error"))
				std_alias = 3;
			else
				alias = name;
		} else if (!CMP_STRING_TO_CSTR(q, c, "type")) {
			if (is_var(name))
				return throw_error(q, name, q->latest_ctx, "instantiation_error", "stream_option");

			if (!is_atom(name))
				return throw_error(q, c, c_ctx, "domain_error", "stream_option");

			if (!CMP_STRING_TO_CSTR(q, name, "binary"))
				binary = true;
			else if (!CMP_STRING_TO_CSTR(q, name, "text"))
				binary = false;
			else
				return throw_error(q, c, c_ctx, "domain_error", "stream_option");
		} else if (!CMP_STRING_TO_CSTR(q, c, "bom")) {
			if (is_var(name))
				return throw_error(q, name, q->latest_ctx, "instantiation_error", "stream_option");

			if (!is_atom(name))
				return throw_error(q, c, c_ctx, "domain_error", "stream_option");

			bom_specified = true;

			if (!CMP_STRING_TO_CSTR(q, name, "true"))
				use_bom = true;
			else if (!CMP_STRING_TO_CSTR(q, name, "false"))
				use_bom = false;
		} else if (!CMP_STRING_TO_CSTR(q, c, "reposition")) {
			if (is_var(name))
				return throw_error(q, name, q->latest_ctx, "instantiation_error", "stream_option");

			if (!is_atom(name))
				return throw_error(q, c, c_ctx, "domain_error", "stream_option");

			if (!CMP_STRING_TO_CSTR(q, name, "true"))
				repo = true;
			else if (!CMP_STRING_TO_CSTR(q, name, "false"))
				repo = false;
		} else if (!CMP_STRING_TO_CSTR(q, c, "eof_action")) {
			if (is_var(name))
				return throw_error(q, name, q->latest_ctx, "instantiation_error", "stream_option");

			if (!is_atom(name))
				return throw_error(q, c, c_ctx, "domain_error", "stream_option");

			if (!CMP_STRING_TO_CSTR(q, name, "error"))
				eof_action = eof_action_error;
			else if (!CMP_STRING_TO_CSTR(q, name, "eof_code"))
				eof_action = eof_action_eof_code;
			else if (!CMP_STRING_TO_CSTR(q, name, "reset"))
				eof_action = eof_action_reset;
		} else
			return throw_error(q, c, c_ctx, "domain_error", "stream_option");

		p4 = PROLOG_LIST_TAIL(p4);
		p4 = deref(q, p4, p4_ctx);
		p4_ctx = q->latest_ctx;

		if (is_var(p4))
			return throw_error(q, p4, p4_ctx, "instantiation_error", "args_not_sufficiently_instantiated");
	}

	// --- materialise the filename and stat it. Three exits below still
	// free src by hand; that is three, not twenty, and none of them has
	// a stream slot to worry about.

	char *src = NULL;
	const char *filename;

	if (oldstr)
		filename = oldstr->filename;
	else if (is_atom(p1))
		filename = src = DUP_STRING(q, p1);
	else
		filename = src = chars_list_to_string(q, p1, p1_ctx);

	struct stat st = {0};
	bool statted = !stat(filename, &st);
	int stat_errno = errno;

	// A directory can be handed to fopen() in read mode on POSIX: it
	// succeeds and only the subsequent reads fail with EISDIR. Catch
	// it here so open/4 reports it like any other unopenable sink.

	if (statted && S_ISDIR(st.st_mode)) {
		TPL_free(src);
		return throw_error(q, p1, p1_ctx, "permission_error", "open,source_sink");
	}

	bool is_read = !CMP_STRING_TO_CSTR(q, p2, "read");

	if (!is_read
		&& CMP_STRING_TO_CSTR(q, p2, "write")
		&& CMP_STRING_TO_CSTR(q, p2, "append")
		&& CMP_STRING_TO_CSTR(q, p2, "update")) {
		TPL_free(src);
		return throw_error(q, p2, p2_ctx, "domain_error", "io_mode");
	}

	// Reading something that is not there. Only ENOENT: if stat failed
	// for any other reason (EACCES on a parent directory, say) fopen is
	// left to produce the error it always did.

	if (is_read && !oldstr && !statted && (stat_errno == ENOENT)) {
		TPL_free(src);
		return throw_error(q, p1, p1_ctx, "existence_error", "source_sink");
	}

	// --- commit. Only fopen/fdopen can fail from here.

	int n = new_stream(q->pl);

	if (n < 0) {
		TPL_free(src);
		return throw_error(q, p1, p1_ctx, "resource_error", "too_many_streams");
	}

	stream *str = &q->pl->streams[n];
	CHECKED(str->filename = TPL_strdup(filename));
	TPL_free(src);
	CHECKED(str->alias = sl_create((void*)fake_strcmp, (void*)keyfree, NULL));
	CHECKED(str->mode = DUP_STRING(q, p2));

	if (std_alias == 1)
		q->pl->current_input = n;
	else if (std_alias == 2)
		q->pl->current_output = n;
	else if (std_alias == 3)
		q->pl->current_error = n;
	else if (alias)
		sl_app(str->alias, DUP_STRING(q, alias), NULL);

	if (!S_ISREG(st.st_mode) && !bom_specified) {
		bom_specified = true;
		use_bom = false;
	}

	str->repo = repo;
	str->binary = binary;
	str->eof_action = eof_action;
	str->bom = false;
	str->did_getc = false;
	str->srclen = str->ungetch = 0;
	str->invalid_pending = false;
	str->at_end_of_file = false;

	if (oldstr) {
		int fd = fileno(oldstr->fp);

		if (!strcmp(str->mode, "read"))
			str->fp = fdopen(fd, str->binary?"rb":"r");
		else if (!strcmp(str->mode, "write"))
			str->fp = fdopen(fd, str->binary?"wb":"w");
		else if (!strcmp(str->mode, "append"))
			str->fp = fdopen(fd, str->binary?"ab":"a");
		else
			str->fp = fdopen(fd, str->binary?"rb+":"r+");
	} else {
		if (!strcmp(str->mode, "read"))
			str->fp = fopen(str->filename, str->binary?"rb":"r");
		else if (!strcmp(str->mode, "write"))
			str->fp = fopen(str->filename, str->binary?"wb":"w");
		else if (!strcmp(str->mode, "append"))
			str->fp = fopen(str->filename, str->binary?"ab":"a");
		else
			str->fp = fopen(str->filename, str->binary?"rb+":"r+");
	}

	if (!str->fp) {
		bool perm = (errno == EACCES)
			|| (!is_read && ((errno == EROFS) || (errno == EISDIR)));
		unwind_stream(q, n);

		if (perm)
			return throw_error(q, p1, p1_ctx, "permission_error", "open,source_sink");
		else
			return throw_error(q, p1, p1_ctx, "existence_error", "source_sink");
	}

	str->fp_out = str->fp;

	if (S_ISFIFO(st.st_mode))
		setvbuf(str->fp, NULL, _IONBF, 0);

#if USE_MMAP
	size_t offset = 0;
#endif

	if (!strcmp(str->mode, "read") && !str->binary && (!bom_specified || use_bom)) {
		int ch = xgetc_utf8_lax(tpl_getc, str);

		if (feof(str->fp_in))
			clearerr(str->fp_in);

		if ((unsigned)ch == 0xFEFF) {
			str->bom = true;
#if USE_MMAP
			offset = 3;
#endif
		} else
			fseek(str->fp, 0, SEEK_SET);
	} else if (!strcmp(str->mode, "write") && !str->binary && use_bom) {
		int ch = 0xFEFF;
		char tmpbuf[MAX_BYTES_PER_CODEPOINT+1];
		put_char_utf8(tmpbuf, ch);
		tpl_write(tmpbuf, strlen(tmpbuf), str);
		str->bom = true;
	}

#if USE_MMAP
	int prot = 0;

	if (!strcmp(str->mode, "read"))
		prot = PROT_READ;
	else
		prot = PROT_WRITE;

	if (mmap_var && is_var(mmap_var)) {
		int fd = fileno(str->fp);
		struct stat st = {0};
		fstat(fd, &st);
		size_t len = st.st_size;
		void *addr = mmap(0, len, prot, MAP_PRIVATE, fd, offset);
		check_error(addr);
		cell tmp = {0};

		if (len) {
			tmp.tag = TAG_CSTR;
			tmp.flags = FLAG_CSTR_BLOB | FLAG_CSTR_STRING | FLAG_CSTR_SLICE;
			tmp.num_cells = 1;
			set_arity(&tmp, 2);
			tmp.val_str = addr;
			tmp.str_len = len;

			// The stream owns this mapping and unmaps it at close. A
			// slice carries no refcount, so nothing else can own it -
			// which is why the mapping used to be retained for the life
			// of the process. Ls must not outlive the stream; every
			// in-tree caller already wraps the pair in
			// setup_call_cleanup/3.

			str->mmap_addr = addr;
			str->mmap_len = len;
		} else {
			munmap(addr, len);
			make_atom(&tmp, g_nil_s);
		}

		unify(q, mmap_var, mmap_ctx, &tmp, q->st.cur_ctx);
	}
#endif

	if (!is_alias) {
		cell tmp ;
		make_int(&tmp, n);
		tmp.flags |= FLAG_INT_STREAM;
		unify(q, p3, p3_ctx, &tmp, q->st.cur_ctx);
	}

	return true;
}

bool stream_close(query *q, int n)
{
	stream *str = &q->pl->streams[n];
	parser_destroy(str->p);
	str->p = NULL;

#if USE_MMAP
	// Release the open/4 mmap(Ls) mapping. Nothing else can: a slice
	// carries no refcount, so before this the mapping was retained for
	// the life of the process (see docs/LEAK-mmap-slices-never-unmapped.md).

	if (str->mmap_addr) {
		munmap(str->mmap_addr, str->mmap_len);
		str->mmap_addr = NULL;
		str->mmap_len = 0;
	}
#endif

	if ((str->fp == stdin)
		|| (str->fp == stdout)
		|| (str->fp == stderr))
		return true;

	if ((int)q->pl->current_input == n)
		q->pl->current_input = 0;

	if ((int)q->pl->current_output == n)
		q->pl->current_output = 1;

	if ((int)q->pl->current_error == n)
		q->pl->current_error = 2;

	if (sl_get(str->alias, "user_input", NULL)) {
		stream *str2 = &q->pl->streams[0];
		sl_app(str2->alias, TPL_strdup("user_input"), NULL);
	}

	if (sl_get(str->alias, "user_output", NULL)) {
		stream *str2 = &q->pl->streams[1];
		sl_app(str2->alias, TPL_strdup("user_output"), NULL);
	}

	if (sl_get(str->alias, "user_error", NULL)) {
		stream *str2 = &q->pl->streams[2];
		sl_app(str2->alias, TPL_strdup("user_error"), NULL);
	}

	del_stream_properties(q, n);
	bool ok = true;

	if (str->is_alias) {
	} else if (str->is_map) {
		sl_destroy(str->keyval);
	} else if (str->is_engine) {
		query_destroy(str->engine);
		free_detached_term(str->cur_yield);
		str->cur_yield = NULL;
	} else
		ok = !tpl_close(str);

	sl_destroy(str->alias);
	str->alias = NULL;
	str->fp_in = str->fp_out = NULL;
	TPL_free(str->mode);
	str->mode = NULL;
	TPL_free(str->filename);
	str->filename = NULL;
	TPL_free(str->addr);
	str->addr = NULL;
	TPL_free(str->data);
	str->data = NULL;
	str->data_owner = NULL;
	TPL_free(str->wbuf);
	str->wbuf = NULL;
	str->wbuf_owner = NULL;
	str->wbuf_len = str->wbuf_pos = 0;
	str->at_end_of_file = true;
	str->is_active = false;

	if (!ok)
		return throw_error(q, q->st.instr, q->st.cur_ctx, "system_error", strerror(errno));

	return true;
}

// There is no io_error class in 7.12.2. An output failure on a stream
// that has gone away -- the far end of a pipe closing, say -- is a
// stream that no longer exists, so report it as such, as other systems
// do (#948).

bool throw_stream_gone(query *q, stream *str)
{
	cell tmp;
	make_int(&tmp, str->idx);
	tmp.flags |= FLAG_INT_STREAM;
	return throw_error(q, &tmp, q->st.cur_ctx, "existence_error", "stream");
}

bool bif_iso_close_1(query *q)
{
	GET_FIRST_ARG(pstr,stream_or_alias);
	int n = get_stream(q, pstr);
	return stream_close(q, n);
}

static bool bif_iso_close_2(query *q)
{
	GET_FIRST_ARG(pstr,stream_or_alias);
	GET_NEXT_ARG(p1,list_or_nil);
	PROLOG_LIST_HANDLER(p1);

	while (is_list(p1)) {
		cell *h = PROLOG_LIST_HEAD(p1);
		h = deref(q, h, p1_ctx);

		if (!is_compound(h)
			|| CMP_STRING_TO_CSTR(q, h, "force")
			|| CMP_STRING_TO_CSTR(q, h+1, "true"))
			return throw_error(q, h, q->latest_ctx, "domain_error", "close_option");

		p1 = PROLOG_LIST_TAIL(p1);
		p1 = deref(q, p1, p1_ctx);
		p1_ctx = q->latest_ctx;
	}

	if (is_var(p1))
		return throw_error(q, p1, p1_ctx, "instantiation_error", "close_option");

	if (!is_nil(p1))
		return throw_error(q, p1, p1_ctx, "type_error", "list");

	return bif_iso_close_1(q);
}

static bool bif_iso_at_end_of_stream_0(query *q)
{
	int n = q->pl->current_input;
	stream *str = &q->pl->streams[n];

	if (!str->ungetch && str->p) {
		if (str->p->srcptr && *str->p->srcptr) {
			int ch = get_char_utf8((const char**)&str->p->srcptr);
			str->ungetch = ch;
		}
	}

	if (!str->ungetch && isatty(fileno(str->fp_in)))
		return false;

	if (!str->is_socket) {
		int ch = str->ungetch ? str->ungetch : xgetc_utf8_lax(tpl_getc, str);
		str->ungetch = ch;
	}

	if (!feof(str->fp_in) && !ferror(str->fp_in))
		return false;

	if (str->eof_action == eof_action_reset)
		clearerr(str->fp_in);

	return true;
}

static bool bif_iso_at_end_of_stream_1(query *q)
{
	GET_FIRST_ARG(pstr,stream_or_alias);
	int n = get_stream(q, pstr);
	stream *str = &q->pl->streams[n];

	if (strcmp(str->mode, "read") && strcmp(str->mode, "update"))
		return throw_error(q, pstr, q->st.cur_ctx, "permission_error", "input,stream");

	if (!str->ungetch && str->p) {
		if (str->p->srcptr && *str->p->srcptr) {
			int ch = get_char_utf8((const char**)&str->p->srcptr);
			str->ungetch = ch;
		}
	}

	if (!str->ungetch && isatty(fileno(str->fp_in)))
		return false;

	if (!str->is_socket) {
		int ch = str->ungetch ? str->ungetch : xgetc_utf8_lax(tpl_getc, str);
		str->ungetch = ch;
	}

	if (!feof(str->fp_in) && !ferror(str->fp_in))
		return false;

	if (str->eof_action == eof_action_reset)
		clearerr(str->fp_in);

	return true;
}

// fflush()es str, throwing existence_error(...) on a real failure - but
// not on a non-task socket's transient EAGAIN (its fd is non-blocking
// now, see bif_net.c), which waits for room and retries instead. Without
// this, flush_output/0,1 and nl/0,1 threw "stream gone" for a peer that
// was simply slow to drain its receive buffer: fflush() failing here
// always sets ferror(), and this file could not tell a full send buffer
// apart from the stream having actually gone away.
static bool flush_or_throw(query *q, stream *str)
{
	for (;;) {
		int err = fflush(str->fp_out);

		if ((err == EOF) && !str->is_socket)
			return throw_stream_gone(q, str);

		if (ferror(str->fp_out)) {
			if (str->is_socket && !q->is_task && ((errno == EAGAIN) || (errno == EWOULDBLOCK))) {
				clearerr(str->fp_out);

				if (!tpl_wait_fd_writable(q, fileno(str->fp_out)))
					return throw_timeout(q);

				continue;
			}

			clearerr(str->fp_out);
			return throw_stream_gone(q, str);
		}

		return true;
	}
}

static bool write_all(query *q, stream *str, const char *src, size_t len);

// A parked write belongs to the query that parked it, not to the stream
// it is parked on. Another query writing the same stream meanwhile must
// leave that buffer alone and send its own bytes - see write_all().
static bool resuming_write(const query *q, const stream *str)
{
	return str->wbuf && (str->wbuf_owner == q);
}

static bool bif_iso_flush_output_0(query *q)
{
	int n = q->pl->current_output;
	stream *str = &q->pl->streams[n];
	return flush_or_throw(q, str);
}

static bool bif_iso_flush_output_1(query *q)
{
	GET_FIRST_ARG(pstr,stream_or_alias);
	int n = get_stream(q, pstr);
	stream *str = &q->pl->streams[n];

	if (!strcmp(str->mode, "read"))
		return throw_error(q, pstr, q->st.cur_ctx, "permission_error", "output,stream");

	return flush_or_throw(q, str);
}

static bool bif_iso_nl_0(query *q)
{
	int n = q->pl->current_output;
	stream *str = &q->pl->streams[n];

	if (str->binary)
		return throw_error(q, q->st.instr, q->st.cur_ctx, "permission_error", "output,binary_stream");

	if (!write_all(q, str, "\n", 1))
		return throw_stream_gone(q, str);

	return flush_or_throw(q, str);
}

static bool bif_iso_nl_1(query *q)
{
	GET_FIRST_ARG(pstr,stream_or_alias);
	int n = get_stream(q, pstr);
	stream *str = &q->pl->streams[n];

	if (!strcmp(str->mode, "read"))
		return throw_error(q, pstr, q->st.cur_ctx, "permission_error", "output,stream");

	if (str->binary)
		return throw_error(q, pstr, q->st.cur_ctx, "permission_error", "output,binary_stream");

	if (!write_all(q, str, "\n", 1))
		return throw_stream_gone(q, str);

	return flush_or_throw(q, str);
}

static bool bif_iso_read_1(query *q)
{
	CHECKED(check_frame(q, MAX_VARS));
	GET_FIRST_ARG(p1,any);
	int n = q->pl->current_input;
	stream *str = &q->pl->streams[n];

	if (str->binary) {
		cell tmp;
		make_int(&tmp, n);
		return throw_error(q, &tmp, q->st.cur_ctx, "permission_error", "input,binary_stream");
	}

	return do_read_term(q, str, p1, p1_ctx, make_nil(), q->st.cur_ctx, NULL);
}

static bool bif_iso_read_2(query *q)
{
	CHECKED(check_frame(q, MAX_VARS));
	GET_FIRST_ARG(pstr,stream_or_alias);
	int n = get_stream(q, pstr);
	stream *str = &q->pl->streams[n];
	GET_NEXT_ARG(p1,any);

	if (strcmp(str->mode, "read"))
		return throw_error(q, pstr, q->st.cur_ctx, "permission_error", "input,stream");

	if (str->binary) {
		cell tmp;
		make_int(&tmp, n);
		return throw_error(q, &tmp, q->st.cur_ctx, "permission_error", "input,binary_stream");
	}

	return do_read_term(q, str, p1, p1_ctx, make_nil(), q->st.cur_ctx, NULL);
}

static bool parse_read_params(query *q, stream *str, cell *c, pl_ctx c_ctx, cell **vars, pl_ctx *vars_ctx, cell **varnames, pl_ctx *varnames_ctx, cell **sings, pl_ctx *sings_ctx)
{
	parser *p = str->p;

	if (!is_compound(c)) {
		throw_error(q, c, c_ctx, "domain_error", "read_option");
		return false;
	}

	cell *c1 = deref(q, FIRST_ARG(c), c_ctx);
	pl_ctx c1_ctx = q->latest_ctx;

	if (!CMP_STRING_TO_CSTR(q, c, "character_escapes")) {
		if (is_interned(c1))
			p->flags.character_escapes = !CMP_STRING_TO_CSTR(q, c1, "true");
	} else if (!CMP_STRING_TO_CSTR(q, c, "json")) {
		if (is_interned(c1))
			p->flags.json = !CMP_STRING_TO_CSTR(q, c1, "true");
	} else if (!CMP_STRING_TO_CSTR(q, c, "var_prefix")) {
		if (is_interned(c1))
			p->flags.var_prefix = !CMP_STRING_TO_CSTR(q, c1, "true");
	} else if (!CMP_STRING_TO_CSTR(q, c, "double_quotes")) {
		if (is_interned(c1)) {
			if (!CMP_STRING_TO_CSTR(q, c1, "atom")) {
				p->flags.double_quote_codes = p->flags.double_quote_chars = false;
				p->flags.double_quote_atom = true;
			} else if (!CMP_STRING_TO_CSTR(q, c1, "chars")) {
				p->flags.double_quote_atom = p->flags.double_quote_codes = false;
				p->flags.double_quote_chars = true;
			} else if (!CMP_STRING_TO_CSTR(q, c1, "codes")) {
				p->flags.double_quote_atom = p->flags.double_quote_chars = false;
				p->flags.double_quote_codes = true;
			}
		}
	} else if (!CMP_STRING_TO_CSTR(q, c, "variables")) {
		if ((is_iso_list_or_nil_or_var(c1) && valid_list(q, c1, c1_ctx)) || true) {
			if (vars) *vars = c1;
			if (vars_ctx) *vars_ctx = c1_ctx;
		} else {
			throw_error(q, c, c_ctx, "domain_error", "read_option");
			return false;
		}
	} else if (!CMP_STRING_TO_CSTR(q, c, "variable_names")) {
		if ((is_iso_list_or_nil_or_var(c1) && valid_list(q, c1, c1_ctx)) || true) {
			if (varnames) *varnames = c1;
			if (varnames_ctx) *varnames_ctx = c1_ctx;
		} else {
			throw_error(q, c, c_ctx, "domain_error", "read_option");
			return false;
		}
	} else if (!CMP_STRING_TO_CSTR(q, c, "singletons")) {
		if ((is_iso_list_or_nil_or_var(c1) && valid_list(q, c1, c1_ctx)) || true) {
			if (sings) *sings = c1;
			if (sings_ctx) *sings_ctx = c1_ctx;
		} else {
			throw_error(q, c, c_ctx, "domain_error", "read_option");
			return false;
		}
	} else if (!CMP_STRING_TO_CSTR(q, c, "positions") && (get_arity(c) == 2) && str->fp_out) {
		p->pos_start = ftello(str->fp_out);
	} else if (!CMP_STRING_TO_CSTR(q, c, "line_counts") && (get_arity(c) == 2)) {
	} else {
		throw_error(q, c, c_ctx, "domain_error", "read_option");
		return false;
	}

	return true;
}

bool do_read_term(query *q, stream *str, cell *p1, pl_ctx p1_ctx, cell *p2, pl_ctx p2_ctx, char *src)
{
	if (!str->p) {
		str->p = parser_create(q->st.m);
		CHECKED(str->p);
		str->p->flags = q->st.m->flags;
		str->p->fp = str->fp;
		str->p->is_socket = str->is_socket && !str->ssl;
		if (q->top) str->p->no_fp = q->top->no_fp;
	} else
		parser_reset(str->p);

	// str->p is reused across calls, possibly by a different query on a
	// long-lived stream, so refresh this every call rather than only
	// where the parser is first created.
	str->p->q = q;

	str->p->do_read_term = true;
	str->p->one_shot = true;
	cell *vars = NULL, *varnames = NULL, *sings = NULL;
	pl_ctx vars_ctx = 0, varnames_ctx = 0, sings_ctx = 0;
	cell *p21 = p2;
	pl_ctx p21_ctx = p2_ctx;

	PROLOG_LIST_HANDLER(p21);

	while (is_list(p21)) {
		cell *h = PROLOG_LIST_HEAD(p21);
		h = deref(q, h, p21_ctx);
		pl_ctx h_ctx = q->latest_ctx;

		if (is_var(h))
			return throw_error(q, p2, p2_ctx, "instantiation_error", "read_option");

		if (!parse_read_params(q, str, h, h_ctx, &vars, &vars_ctx, &varnames, &varnames_ctx, &sings, &sings_ctx))
			return true;

		p21 = PROLOG_LIST_TAIL(p21);
		p21 = deref(q, p21, p21_ctx);
		p21_ctx = q->latest_ctx;
	}

	if (is_var(p21))
		return throw_error(q, p2, p2_ctx, "instantiation_error", "read_option");

	if (!is_nil(p21))
		return throw_error(q, p2, p2_ctx, "type_error", "list");

	if (!src && !str->p->srcptr && str->fp_in) {
		if (str->p->no_fp || tpl_getline(&str->p->save_line, &str->p->n_line, q, str) == -1) {
			if (q->is_task && !feof(str->fp_in) && ferror(str->fp_in)) {
				clearerr(str->fp_in);
				return do_yield_on_stream(q, str, false);
			}

			if (errno == EINTR) {
				clearerr(str->fp_in);
				return throw_timeout(q);
			}

			str->p->srcptr = "";
		} else
			str->p->srcptr = str->p->save_line;
	}

	if (str->p->srcptr) {
		errno = 0;	// FIX: reset before parse read so a stale EINTR isn't misread
		char *src = (char*)eat_space(str->p);

		if (errno == EINTR) {
			clearerr(str->fp_in);
			return throw_timeout(q);
		}

		if (str->p->error)
			return throw_error(q, q->st.instr, q->st.cur_ctx,
				str->p->error_type?str->p->error_type:"syntax_error",
				str->p->error_desc?str->p->error_desc:"read_term");

		str->p->line_num_start = str->p->line_num;
		str->p->srcptr = src;
	}

	for (;;) {
		if (!src && (!str->p->srcptr || !*str->p->srcptr || (*str->p->srcptr == '\n'))) {
			if (str->p->srcptr && (*str->p->srcptr == '\n'))
				str->p->line_num++;

			if (str->fp && (
				str->p->no_fp || (tpl_getline(&str->p->save_line, &str->p->n_line, q, str) == -1))) {
				if (q->is_task && !feof(str->fp_in) && ferror(str->fp_in)) {
					clearerr(str->fp_in);
					return do_yield_on_stream(q, str, false);
				}

				if (errno == EINTR) {
					clearerr(str->fp_in);
					return throw_timeout(q);
				}

				str->p->srcptr = "";
				str->at_end_of_file = str->eof_action != eof_action_reset;

				if (str->eof_action == eof_action_reset)
					clearerr(str->fp_in);

				if (vars)
					if (!unify(q, vars, vars_ctx, make_nil(), q->st.cur_ctx))
						return false;

				if (varnames)
					if (!unify(q, varnames, varnames_ctx, make_nil(), q->st.cur_ctx))
						return false;

				if (sings)
					if (!unify(q, sings, sings_ctx, make_nil(), q->st.cur_ctx))
						return false;

				cell *p22 = p2;
				pl_ctx p22_ctx = p2_ctx;
				PROLOG_LIST_HANDLER(p22);

				while (is_list(p22)) {
					cell *h = PROLOG_LIST_HEAD(p22);
					h = deref(q, h, p22_ctx);
					pl_ctx h_ctx = q->latest_ctx;

					if (is_var(h))
						return throw_error(q, p2, p2_ctx, "instantiation_error", "read_option");

					if (!CMP_STRING_TO_CSTR(q, h, "positions") && (get_arity(h) == 2)) {
						cell *p = h+1;
						p = deref(q, p, h_ctx);
						pl_ctx p_ctx = q->latest_ctx;
						cell tmp;
						make_int(&tmp, str->p->pos_start);
						unify(q, p, p_ctx, &tmp, q->st.cur_ctx);
						p = h+2;
						p = deref(q, p, h_ctx);
						p_ctx = q->latest_ctx;
						make_int(&tmp, ftello(str->fp_out));
						unify(q, p, p_ctx, &tmp, q->st.cur_ctx);
					} else if (!CMP_STRING_TO_CSTR(q, h, "line_counts") && (get_arity(h) == 2)) {
						cell *p = h+1;
						p = deref(q, p, h_ctx);
						pl_ctx p_ctx = q->latest_ctx;
						cell tmp;
						make_int(&tmp, str->p->line_num_start);
						unify(q, p, p_ctx, &tmp, q->st.cur_ctx);
						p = h+2;
						p = deref(q, p, h_ctx);
						p_ctx = q->latest_ctx;
						make_int(&tmp, str->p->line_num);
						unify(q, p, p_ctx, &tmp, q->st.cur_ctx);
					}

					p22 = PROLOG_LIST_TAIL(p22);
					p22 = deref(q, p22, p22_ctx);
					p22_ctx = q->latest_ctx;
				}

				cell tmp;
				make_atom(&tmp, g_eof_s);
				return unify(q, p1, p1_ctx, &tmp, q->st.cur_ctx);
			}

			//if (!*p->save_line || (*p->save_line == '\r') || (*p->save_line == '\n'))
			//	continue;

			str->p->srcptr = str->p->save_line;
		} else if (src)
			str->p->srcptr = src;

		break;
	}

	if (str->p->did_getline)
		q->is_input = true;

	frame *f = GET_CURR_FRAME();
	str->p->read_term_slots = f->actual_slots;
	tokenize(str->p, false, false);
	str->p->read_term_slots = 0;

	if (str->p->error || !str->p->end_of_term) {
		str->p->error = false;

		if (!str->p->fp || !isatty(fileno(str->p->fp))) {
			void *save_fp = str->p->fp;
			str->p->fp = NULL;

			while (get_token(str->p, false, false)
				&& SB_strlen(str->p->token) && SB_strcmp(str->p->token, ".")) {
			}

			str->p->fp = save_fp;
			str->p->did_getline = false;
		}

		str->p->do_read_term = false;
		return throw_error(q, make_nil(), q->st.cur_ctx,
			str->p->error_type?str->p->error_type:"syntax_error",
			str->p->error_desc?str->p->error_desc:"read_term");
	}

	str->p->do_read_term = false;

	cell *p22 = p2;
	pl_ctx p22_ctx = p2_ctx;
	PROLOG_LIST_HANDLER(p22);

	while (is_list(p22)) {
		cell *h = PROLOG_LIST_HEAD(p22);
		h = deref(q, h, p22_ctx);
		pl_ctx h_ctx = q->latest_ctx;

		if (is_var(h))
			return throw_error(q, p2, p2_ctx, "instantiation_error", "read_option");

		if (!CMP_STRING_TO_CSTR(q, h, "positions") && (get_arity(h) == 2)) {
			cell *p = h+1;
			p = deref(q, p, h_ctx);
			pl_ctx p_ctx = q->latest_ctx;
			cell tmp;
			make_int(&tmp, str->p->pos_start);

			if (!unify(q, p, p_ctx, &tmp, q->st.cur_ctx))
				return false;

			p = h+2;
			p = deref(q, p, h_ctx);
			p_ctx = q->latest_ctx;
			make_int(&tmp, ftello(str->fp_out));

			if (!unify(q, p, p_ctx, &tmp, q->st.cur_ctx))
				return false;
		} else if (!CMP_STRING_TO_CSTR(q, h, "line_counts") && (get_arity(h) == 2)) {
			cell *p = h+1;
			p = deref(q, p, h_ctx);
			pl_ctx p_ctx = q->latest_ctx;
			cell tmp;
			make_int(&tmp, str->p->line_num_start);

			if (!unify(q, p, p_ctx, &tmp, q->st.cur_ctx))
				return false;

			p = h+2;
			p = deref(q, p, h_ctx);
			p_ctx = q->latest_ctx;
			make_int(&tmp, str->p->line_num);

			if (!unify(q, p, p_ctx, &tmp, q->st.cur_ctx))
				return false;
		}

		p22 = PROLOG_LIST_TAIL(p22);
		p22 = deref(q, p22, p22_ctx);
		p22_ctx = q->latest_ctx;
	}

	if (!str->p->cl->cidx) {
		cell tmp;
		make_atom(&tmp, g_eof_s);
		return unify(q, p1, p1_ctx, &tmp, q->st.cur_ctx);
	}

	process_clause(str->p->m, str->p->cl, NULL);

	if (str->p->num_vars) {
		if (create_vars(q, str->p->num_vars) < 0)
			return throw_error(q, p1, p1_ctx, "resource_error", "stack");
	}

	q->tab_idx = 0;

	if (str->p->num_vars)
		collect_vars(q, str->p->cl->cells, q->st.cur_ctx);

	if (vars) {
		unsigned cnt = q->tab_idx;
		unsigned idx = 0;

		if (cnt) {
			CHECKED(init_tmp_heap(q));
			cell *tmp = alloc_tmp(q, (cnt*2)+1);
			CHECKED(tmp);
			unsigned done = 0;

			for (unsigned i = 0; i < q->tab_idx; i++) {
				make_atom(tmp+idx, g_dot_s);
				set_arity(&tmp[idx], 2);
				tmp[idx++].num_cells = ((cnt-done)*2)+1;
				cell v;
				make_ref(&v, q->tabs[i].var_num, q->st.cur_ctx);
				tmp[idx++] = v;
				done++;
			}

			make_atom(tmp+idx++, g_nil_s);
			set_arity(&tmp[0], 2);
			tmp[0].num_cells = idx;

			cell *save = tmp;
			tmp = alloc_heap(q, idx);
			CHECKED(tmp);
			dup_cells(tmp, save, idx);
			tmp->num_cells = idx;
			if (!unify(q, vars, vars_ctx, tmp, q->st.cur_ctx))
				return false;
		} else {
			if (!unify(q, vars, vars_ctx, make_nil(), q->st.cur_ctx))
				return false;
		}
	}

	if (varnames) {
		unsigned cnt = 0;
		CHECKED(init_tmp_heap(q));
		unsigned idx = 0;

		for (unsigned i = 0; i < q->tab_idx; i++) {
			if (q->tabs[i].is_anon)
				continue;

			cnt++;
		}

		if (cnt) {
			cell *tmp = alloc_tmp(q, (cnt*4)+1);
			CHECKED(tmp);
			unsigned done = 0;

			for (unsigned i = 0; i < q->tab_idx; i++) {
				if (q->tabs[i].is_anon)
					continue;

				make_atom(tmp+idx, g_dot_s);
				set_arity(&tmp[idx], 2);
				tmp[idx++].num_cells = ((cnt-done)*4)+1;
				cell v;
				make_instr(&v, g_unify_s, bif_iso_unify_2, 2, 2);
				SET_OP(&v,OP_XFX);
				tmp[idx++] = v;
				make_atom(&v, q->tabs[i].val_off);
				tmp[idx++] = v;
				make_ref(&v, q->tabs[i].var_num, q->st.cur_ctx);
				tmp[idx++] = v;
				done++;
			}

			make_atom(tmp+idx++, g_nil_s);
			set_arity(&tmp[0], 2);
			tmp[0].num_cells = idx;

			cell *save = tmp;
			tmp = alloc_heap(q, idx);
			CHECKED(tmp);
			dup_cells(tmp, save, idx);
			tmp->num_cells = idx;
			if (!unify(q, varnames, varnames_ctx, tmp, q->st.cur_ctx))
				return false;
		} else {
			if (!unify(q, varnames, varnames_ctx, make_nil(), q->st.cur_ctx))
				return false;
		}
	}

	if (sings) {
		unsigned cnt = 0;
		CHECKED(init_tmp_heap(q));
		unsigned idx = 0;

		for (unsigned i = 0; i < q->tab_idx; i++) {
			if (q->tabs[i].cnt != 1)
				continue;

			if (varnames && (q->tabs[i].is_anon))
				continue;

			cnt++;
		}

		if (cnt) {
			cell *tmp = alloc_tmp(q, (cnt*4)+1);
			CHECKED(tmp);
			unsigned done = 0;

			for (unsigned i = 0; i < q->tab_idx; i++) {
				if (q->tabs[i].cnt != 1)
					continue;

				if (varnames && (q->tabs[i].is_anon))
					continue;

				make_atom(tmp+idx, g_dot_s);
				set_arity(&tmp[idx], 2);
				tmp[idx++].num_cells = ((cnt-done)*4)+1;
				cell v;
				make_instr(&v, g_unify_s, bif_iso_unify_2, 2, 2);
				SET_OP(&v,OP_XFX);
				tmp[idx++] = v;
				make_atom(&v, q->tabs[i].val_off);
				tmp[idx++] = v;
				make_ref(&v, q->tabs[i].var_num, q->st.cur_ctx);
				tmp[idx++] = v;
				done++;
			}

			make_atom(tmp+idx++, g_nil_s);
			set_arity(&tmp[0], 2);
			tmp[0].num_cells = idx;

			cell *save = tmp;
			tmp = alloc_heap(q, idx);
			CHECKED(tmp);
			dup_cells(tmp, save, idx);
			tmp->num_cells = idx;
			if (!unify(q, sings, sings_ctx, tmp, q->st.cur_ctx))
				return false;
		} else {
			if (!unify(q, sings, sings_ctx, make_nil(), q->st.cur_ctx))
				return false;
		}
	}

	cell *tmp = alloc_heap(q, str->p->cl->cidx-1);
	CHECKED(tmp);
	dup_cells(tmp, str->p->cl->cells, str->p->cl->cidx-1);
	bool ok = unify(q, p1, p1_ctx, tmp, q->st.cur_ctx);
	clear_clause(str->p->cl);
	return ok;
}

struct retry_ctx_ { query *q; stream *str; };

// Single-byte source for a non-task query reading a socket stream, which
// is now always non-blocking (see bif_net.c) - so a plain tpl_getc() can
// come back EOF/ferror on EAGAIN alone, with more data to come shortly.
// Waits that out instead of taking it for real EOF or an error, and
// instead of the caller ever blocking the OS thread inside fgetc() the
// way a blocking-mode fd would. A task defers to its own existing
// is_task/ferror check after the call, unchanged, so this returns
// immediately without waiting when q->is_task. Passed to xgetc_utf8() as
// its byte source (rather than tpl_getc directly) so a UTF-8 sequence
// split across two reads gets the same treatment on every continuation
// byte, not just the first.
static int retry_getc(void *ctx0)
{
	struct retry_ctx_ *ctx = ctx0;
	stream *str = ctx->str;
	query *q = ctx->q;

	for (;;) {
		int ch = tpl_getc(str);

		if ((ch != EOF) || q->is_task || feof(str->fp) || !ferror(str->fp) || (errno == EINTR))
			return ch;

		if ((errno != EAGAIN) && (errno != EWOULDBLOCK))
			return ch;

		clearerr(str->fp_in);

		if (!tpl_wait_fd_readable(q, fileno(str->fp_in)))
			return EOF;	// errno == EINTR, set by tpl_wait_fd_readable()
	}
}

static bool bif_iso_read_term_2(query *q)
{
	CHECKED(check_frame(q, MAX_VARS));
	GET_FIRST_ARG(p1,any);
	GET_NEXT_ARG(p2,list_or_nil);
	int n = q->pl->current_input;
	stream *str = &q->pl->streams[n];

	if (str->binary) {
		cell tmp;
		make_int(&tmp, n);
		return throw_error(q, &tmp, q->st.cur_ctx, "permission_error", "input,binary_stream");
	}

	return do_read_term(q, str, p1, p1_ctx, p2, p2_ctx, NULL);
}

static bool bif_iso_read_term_3(query *q)
{
	CHECKED(check_frame(q, MAX_VARS));
	GET_FIRST_ARG(pstr,stream_or_alias);
	int n = get_stream(q, pstr);
	stream *str = &q->pl->streams[n];
	GET_NEXT_ARG(p1,any);
	GET_NEXT_ARG(p2,list_or_nil);

	if (strcmp(str->mode, "read") && strcmp(str->mode, "update"))
		return throw_error(q, pstr, q->st.cur_ctx, "permission_error", "input,stream");

	if (str->binary) {
		cell tmp;
		make_int(&tmp, n);
		return throw_error(q, &tmp, q->st.cur_ctx, "permission_error", "input,binary_stream");
	}

	return do_read_term(q, str, p1, p1_ctx, p2, p2_ctx, NULL);
}

static bool bif_iso_write_1(query *q)
{
	GET_FIRST_ARG(p1,any);
	int n = q->pl->current_output;
	stream *str = &q->pl->streams[n];

	if (str->binary) {
		cell tmp;
		make_int(&tmp, n);
		return throw_error(q, &tmp, q->st.cur_ctx, "permission_error", "output,binary_stream");
	}

	q->numbervars = true;
	print_term_to_stream(q, str, p1, p1_ctx, 1);
	q->numbervars = false;
	return true;
}

static bool bif_iso_write_2(query *q)
{
	GET_FIRST_ARG(pstr,stream_or_alias);
	int n = get_stream(q, pstr);
	stream *str = &q->pl->streams[n];
	GET_NEXT_ARG(p1,any);

	if (!strcmp(str->mode, "read"))
		return throw_error(q, pstr, q->st.cur_ctx, "permission_error", "output,stream");

	if (str->binary) {
		cell tmp;
		make_int(&tmp, n);
		return throw_error(q, &tmp, q->st.cur_ctx, "permission_error", "output,binary_stream");
	}

	q->numbervars = true;
	print_term_to_stream(q, str, p1, p1_ctx, 1);
	q->numbervars = false;
	return true;
}

static bool bif_iso_writeq_1(query *q)
{
	GET_FIRST_ARG(p1,any);
	int n = q->pl->current_output;
	stream *str = &q->pl->streams[n];

	if (str->binary) {
		cell tmp;
		make_int(&tmp, n);
		return throw_error(q, &tmp, q->st.cur_ctx, "permission_error", "output,binary_stream");
	}

	q->quoted = 1;
	q->numbervars = true;
	print_term_to_stream(q, str, p1, p1_ctx, 1);
	q->numbervars = false;
	q->quoted = 0;
	return true;
}

static bool bif_iso_writeq_2(query *q)
{
	GET_FIRST_ARG(pstr,stream_or_alias);
	int n = get_stream(q, pstr);
	stream *str = &q->pl->streams[n];
	GET_NEXT_ARG(p1,any);

	if (!strcmp(str->mode, "read"))
		return throw_error(q, pstr, q->st.cur_ctx, "permission_error", "output,stream");

	if (str->binary) {
		cell tmp;
		make_int(&tmp, n);
		return throw_error(q, &tmp, q->st.cur_ctx, "permission_error", "output,binary_stream");
	}

	q->quoted = 1;
	q->numbervars = true;
	print_term_to_stream(q, str, p1, p1_ctx, 1);
	q->numbervars = false;
	q->quoted = 0;
	return true;
}

static bool bif_iso_write_canonical_1(query *q)
{
	GET_FIRST_ARG(p1,any);
	int n = q->pl->current_output;
	stream *str = &q->pl->streams[n];

	if (str->binary) {
		cell tmp;
		make_int(&tmp, n);
		return throw_error(q, &tmp, q->st.cur_ctx, "permission_error", "output,binary_stream");
	}

	print_canonical_to_stream(q, str, p1, p1_ctx, 1);
	return true;
}

static bool bif_iso_write_canonical_2(query *q)
{
	GET_FIRST_ARG(pstr,stream_or_alias);
	int n = get_stream(q, pstr);
	stream *str = &q->pl->streams[n];
	GET_NEXT_ARG(p1,any);

	if (!strcmp(str->mode, "read"))
		return throw_error(q, pstr, q->st.cur_ctx, "permission_error", "output,stream");

	if (str->binary) {
		cell tmp;
		make_int(&tmp, n);
		return throw_error(q, &tmp, q->st.cur_ctx, "permission_error", "output,binary_stream");
	}

	print_canonical_to_stream(q, str, p1, p1_ctx, 1);
	return true;
}

bool parse_write_params(query *q, cell *c, pl_ctx c_ctx, cell **vnames, pl_ctx *vnames_ctx)
{
	if (is_var(c)) {
		throw_error(q, c, c_ctx, "instantiation_error", "write_option");
		return false;
	}

	if (!is_interned(c) || !is_compound(c)) {
		throw_error(q, c, c_ctx, "domain_error", "write_option");
		return false;
	}

	cell *c1 = deref(q, FIRST_ARG(c), c_ctx);
	pl_ctx c1_ctx = q->latest_ctx;

	if (!CMP_STRING_TO_CSTR(q, c, "max_depth")) {
		if (is_var(c1)) {
			throw_error(q, c1, c_ctx, "instantiation_error", "write_option");
			return false;
		}

		if (is_integer(c1) && (get_smallint(c1) >= 0))
			q->max_depth = get_smallint(c1);
		else {
			throw_error(q, c, c_ctx, "domain_error", "write_option");
			return false;
		}
	} else if (!CMP_STRING_TO_CSTR(q, c, "fullstop")) {
		if (is_var(c1)) {
			throw_error(q, c1, c_ctx, "instantiation_error", "write_option");
			return false;
		}

		if (!is_interned(c1) || (CMP_STRING_TO_CSTR(q, c1, "true") && CMP_STRING_TO_CSTR(q, c1, "false"))) {
			throw_error(q, c, c_ctx, "domain_error", "write_option");
			return false;
		}

		q->fullstop = !CMP_STRING_TO_CSTR(q, c1, "true");
	} else if (!CMP_STRING_TO_CSTR(q, c, "portrayed")) {
		if (is_var(c1)) {
			throw_error(q, c1, c_ctx, "instantiation_error", "write_option");
			return false;
		}

		if (!is_interned(c1) || (CMP_STRING_TO_CSTR(q, c1, "true") && CMP_STRING_TO_CSTR(q, c1, "false"))) {
			throw_error(q, c, c_ctx, "domain_error", "write_option");
			return false;
		}

		q->portrayed = !CMP_STRING_TO_CSTR(q, c1, "true");
	} else if (!CMP_STRING_TO_CSTR(q, c, "nl")) {
		if (is_var(c1)) {
			throw_error(q, c1, c_ctx, "instantiation_error", "write_option");
			return false;
		}

		if (!is_interned(c1) || (CMP_STRING_TO_CSTR(q, c1, "true") && CMP_STRING_TO_CSTR(q, c1, "false"))) {
			throw_error(q, c, c_ctx, "domain_error", "write_option");
			return false;
		}

		q->nl = !CMP_STRING_TO_CSTR(q, c1, "true");
	} else if (!CMP_STRING_TO_CSTR(q, c, "json")) {
		if (is_var(c1)) {
			throw_error(q, c1, c_ctx, "instantiation_error", "write_option");
			return false;
		}

		if (!is_interned(c1) || (CMP_STRING_TO_CSTR(q, c1, "true") && CMP_STRING_TO_CSTR(q, c1, "false"))) {
			throw_error(q, c, c_ctx, "domain_error", "write_option");
			return false;
		}

		q->json = !CMP_STRING_TO_CSTR(q, c1, "true");
	} else if (!CMP_STRING_TO_CSTR(q, c, "quoted")) {
		if (is_var(c1)) {
			throw_error(q, c1, c_ctx, "instantiation_error", "write_option");
			return false;
		}

		if (!is_interned(c1) || (CMP_STRING_TO_CSTR(q, c1, "true") && CMP_STRING_TO_CSTR(q, c1, "false"))) {
			throw_error(q, c, c_ctx, "domain_error", "write_option");
			return false;
		}

		q->quoted = !CMP_STRING_TO_CSTR(q, c1, "true");
	} else if (!CMP_STRING_TO_CSTR(q, c, "double_quotes")) {
		if (is_var(c1)) {
			throw_error(q, c1, c_ctx, "instantiation_error", "write_option");
			return false;
		}

		if (!is_interned(c1) || (CMP_STRING_TO_CSTR(q, c1, "true") && CMP_STRING_TO_CSTR(q, c1, "false"))) {
			throw_error(q, c, c_ctx, "domain_error", "write_option");
			return false;
		}

		q->double_quotes = !CMP_STRING_TO_CSTR(q, c1, "true");
	} else if (!CMP_STRING_TO_CSTR(q, c, "varnames")) {
		if (is_var(c1)) {
			throw_error(q, c1, c_ctx, "instantiation_error", "write_option");
			return false;
		}

		if (!is_interned(c1) || (CMP_STRING_TO_CSTR(q, c1, "true") && CMP_STRING_TO_CSTR(q, c1, "false"))) {
			throw_error(q, c, c_ctx, "domain_error", "write_option");
			return false;
		}

		q->varnames = !CMP_STRING_TO_CSTR(q, c1, "true");
	} else if (!CMP_STRING_TO_CSTR(q, c, "ignore_ops")) {
		if (is_var(c1)) {
			throw_error(q, c1, c_ctx, "instantiation_error", "write_option");
			return false;
		}

		if (!is_interned(c1) || (CMP_STRING_TO_CSTR(q, c1, "true") && CMP_STRING_TO_CSTR(q, c1, "false"))) {
			throw_error(q, c, c_ctx, "domain_error", "write_option");
			return false;
		}

		q->ignore_ops = !CMP_STRING_TO_CSTR(q, c1, "true");
	} else if (!CMP_STRING_TO_CSTR(q, c, "numbervars")) {
		if (is_var(c1)) {
			throw_error(q, c1, c_ctx, "instantiation_error", "write_option");
			return false;
		}

		if (!is_interned(c1) || (CMP_STRING_TO_CSTR(q, c1, "true") && CMP_STRING_TO_CSTR(q, c1, "false"))) {
			throw_error(q, c, c_ctx, "domain_error", "write_option");
			return false;
		}

		q->numbervars = !CMP_STRING_TO_CSTR(q, c1, "true");
	} else if (!CMP_STRING_TO_CSTR(q, c, "variable_names")) {
		if (is_var(c1)) {
			throw_error(q, c1, c_ctx, "instantiation_error", "write_option");
			return false;
		}

		if (!is_iso_list_or_nil(c1)) {
			throw_error(q, c, c_ctx, "domain_error", "write_option");
			return false;
		}

		bool is_partial = false;

		if (!check_list(q, c1, c1_ctx, &is_partial, NULL)) {
			// Must return after throw: falling through re-throws from the
			// element loop (issue #1090 — double error / write-after-error).
			if (is_partial)
				throw_error(q, c1, c1_ctx, "instantiation_error", "write_option");
			else
				throw_error(q, c, c_ctx, "domain_error", "write_option");

			return false;
		}

		cell *c1_orig = c1;
		pl_ctx c1_orig_ctx = c1_ctx;
		bool any1 = false, any2 = false;
		PROLOG_LIST_HANDLER(c1);

		while (is_list(c1)) {
			cell *h = PROLOG_LIST_HEAD(c1);
			pl_ctx h_ctx = c1_ctx;
			h = deref(q, h, h_ctx);
			h_ctx = q->latest_ctx;

			if (is_var(h)) {
				throw_error(q, h, h_ctx, "instantiation_error", "write_option");
				return false;
			}

			if (!is_compound(h)) {
				throw_error(q, c, c_ctx, "domain_error", "write_option");
				return false;
			}

			if (CMP_STRING_TO_CSTR(q, h, "=")) {
				throw_error(q, c, c_ctx, "domain_error", "write_option");
				return false;
			}

			if (is_interned(h)) {
				cell *h1 = deref(q, h+1, h_ctx);

				if (is_var(h1)) {
					throw_error(q, c, c_ctx, "instantiation_error", "write_option");
					return false;
				} else if (!is_atom(h1)) {
					throw_error(q, c, c_ctx, "domain_error", "write_option");
					return false;
				}
			}

			c1 = PROLOG_LIST_TAIL(c1);
			c1 = deref(q, c1, h_ctx);
			c1_ctx = q->latest_ctx;
		}

		if (vnames) *vnames = c1_orig;
		if (vnames_ctx) *vnames_ctx = c1_orig_ctx;
	} else {
		throw_error(q, c, c_ctx, "domain_error", "write_option");
		return false;
	}

	return true;
}

static bool bif_iso_write_term_2(query *q)
{
	GET_FIRST_ARG(p1,any);
	GET_NEXT_ARG(p2,list_or_nil);
	int n = q->pl->current_output;
	stream *str = &q->pl->streams[n];

	if (str->binary) {
		cell tmp;
		make_int(&tmp, n);
		return throw_error(q, &tmp, q->st.cur_ctx, "permission_error", "output,binary_stream");
	}

	q->flags = q->st.m->flags;
	q->numbervars = false;
	cell *p2_orig = p2, *vnames = NULL;
	pl_ctx p2_orig_ctx = p2_ctx, vnames_ctx = 0;
	PROLOG_LIST_HANDLER(p2);

	while (is_iso_list(p2)) {
		cell *h = PROLOG_LIST_HEAD(p2);
		h = deref(q, h, p2_ctx);
		pl_ctx h_ctx = q->latest_ctx;

		if (!parse_write_params(q, h, h_ctx, &vnames, &vnames_ctx)) {
			clear_write_options(q);
			return true;
		}

		p2 = PROLOG_LIST_TAIL(p2);
		p2 = deref(q, p2, p2_ctx);
		p2_ctx = q->latest_ctx;
	}

	if (is_var(p2)) {
		clear_write_options(q);
		return throw_error(q, p2_orig, p2_orig_ctx, "instantiation_error", "write_option");
	}

	if (!is_nil(p2)) {
		clear_write_options(q);
		return throw_error(q, p2_orig, p2_orig_ctx, "type_error", "list");
	}

	if (q->portrayed) {
		cell *c = p1;
		pl_ctx c_ctx = p1_ctx;
		cell p1[1+c->num_cells];
		make_instr(p1+0, new_atom(q->pl, "$portray"), NULL, 1, c->num_cells);
		dup_cells_by_ref(p1+1, c, c_ctx, c->num_cells);
		cell *tmp = prepare_call(q, CALL_SKIP, p1, q->st.cur_ctx, 1);
		pl_idx num_cells = p1->num_cells;
		make_end(tmp+num_cells);
		query *q2 = query_create_subquery(q, tmp);
		start(q2);
		query_destroy(q2);
		clear_write_options(q);
		if (ferror(str->fp_out)) {
			clearerr(str->fp_out);
			return throw_stream_gone(q, str);
		}

		return true;
	}

	q->variable_names = vnames;
	q->variable_names_ctx = vnames_ctx;

	print_term_to_stream(q, str, p1, p1_ctx, 1);

	clear_write_options(q);
	return true;
}

static bool bif_iso_write_term_3(query *q)
{
	GET_FIRST_ARG(pstr,stream_or_alias);
	int n = get_stream(q, pstr);
	stream *str = &q->pl->streams[n];
	GET_NEXT_ARG(p1,any);
	GET_NEXT_ARG(p2,list_or_nil);

	if (!strcmp(str->mode, "read"))
		return throw_error(q, pstr, q->st.cur_ctx, "permission_error", "output,stream");

	if (str->binary) {
		cell tmp;
		make_int(&tmp, n);
		return throw_error(q, &tmp, q->st.cur_ctx, "permission_error", "output,binary_stream");
	}

	q->flags = q->st.m->flags;
	q->numbervars = false;
	cell *p2_orig = p2, *vnames = NULL;
	pl_ctx p2_orig_ctx = p2_ctx, vnames_ctx;
	PROLOG_LIST_HANDLER(p2);

	while (is_iso_list(p2)) {
		cell *h = PROLOG_LIST_HEAD(p2);
		h = deref(q, h, p2_ctx);
		pl_ctx h_ctx = q->latest_ctx;

		if (!parse_write_params(q, h, h_ctx, &vnames, &vnames_ctx)) {
			clear_write_options(q);
			return true;
		}

		p2 = PROLOG_LIST_TAIL(p2);
		p2 = deref(q, p2, p2_ctx);
		p2_ctx = q->latest_ctx;
	}

	if (is_var(p2)) {
		clear_write_options(q);
		return throw_error(q, p2_orig, p2_orig_ctx, "instantiation_error", "write_option");
	}

	if (!is_nil(p2)) {
		clear_write_options(q);
		return throw_error(q, p2_orig, p2_orig_ctx, "type_error", "list");
	}

	if (q->portrayed) {
		cell *c = p1;
		pl_ctx c_ctx = p1_ctx;
		cell p1[1+1+c->num_cells];
		make_instr(p1+0, new_atom(q->pl, "$portray"), NULL, 2, 1+c->num_cells);
		p1[1] = *pstr;
		dup_cells_by_ref(p1+2, c, c_ctx, c->num_cells);
		cell *tmp = prepare_call(q, CALL_SKIP, p1, q->st.cur_ctx, 1);
		pl_idx num_cells = p1->num_cells;
		make_end(tmp+num_cells);
		query *q2 = query_create_subquery(q, tmp);
		start(q2);
		query_destroy(q2);
		clear_write_options(q);
		if (ferror(str->fp_out)) {
			clearerr(str->fp_out);
			return throw_stream_gone(q, str);
		}

		return true;
	}

	q->latest_ctx = p1_ctx;
	q->variable_names = vnames;
	q->variable_names_ctx = vnames_ctx;

	print_term_to_stream(q, str, p1, p1_ctx, 1);

	clear_write_options(q);
	return true;
}

static bool bif_iso_put_char_1(query *q)
{
	GET_FIRST_ARG(p1,character);
	int n = q->pl->current_output;
	stream *str = &q->pl->streams[n];
	size_t len = len_char_utf8(C_STR(q, p1));

	if (str->binary) {
		cell tmp;
		make_int(&tmp, n);
		return throw_error(q, &tmp, q->st.cur_ctx, "permission_error", "output,binary_stream");
	}

	if (len != C_STRLEN(q, p1))
		return throw_error(q, p1, p1_ctx, "type_error", "character");

	const char *src = C_STR(q, p1);
	int ch = get_char_utf8(&src);
	char tmpbuf[MAX_BYTES_PER_CODEPOINT+1];
	put_char_utf8(tmpbuf, ch);
	tpl_write(tmpbuf, strlen(tmpbuf), str);
	return true;
}

static bool bif_iso_put_char_2(query *q)
{
	GET_FIRST_ARG(pstr,stream_or_alias);
	int n = get_stream(q, pstr);
	stream *str = &q->pl->streams[n];
	GET_NEXT_ARG(p1,character);
	size_t len = len_char_utf8(C_STR(q, p1));

	if (!strcmp(str->mode, "read"))
		return throw_error(q, pstr, q->st.cur_ctx, "permission_error", "output,stream");

	if (str->binary) {
		cell tmp;
		make_int(&tmp, n);
		return throw_error(q, &tmp, q->st.cur_ctx, "permission_error", "output,binary_stream");
	}

	if (len != C_STRLEN(q, p1))
		return throw_error(q, p1, p1_ctx, "type_error", "character");

	const char *src = C_STR(q, p1);
	int ch = get_char_utf8(&src);
	char tmpbuf[MAX_BYTES_PER_CODEPOINT+1];
	put_char_utf8(tmpbuf, ch);
	tpl_write(tmpbuf, strlen(tmpbuf), str);
	return true;
}

static bool bif_iso_put_code_1(query *q)
{
	GET_FIRST_ARG(p1,integer);
	int n = q->pl->current_output;
	stream *str = &q->pl->streams[n];

	if (is_bigint(p1))
		return throw_error(q, p1, p1_ctx, "domain_error", "small_integer_range");

	if (str->binary) {
		cell tmp;
		make_int(&tmp, n);
		return throw_error(q, &tmp, q->st.cur_ctx, "permission_error", "output,binary_stream");
	}

	if (!is_integer(p1))
		return throw_error(q, p1, p1_ctx, "type_error", "integer");

	if (is_integer(p1) && is_le(p1,-1))
		return throw_error(q, p1, p1_ctx, "representation_error", "character_code");

	int ch = (int)get_smallint(p1);
	char tmpbuf[MAX_BYTES_PER_CODEPOINT+1];
	put_char_utf8(tmpbuf, ch);
	tpl_write(tmpbuf, strlen(tmpbuf), str);
	return true;
}

static bool bif_iso_put_code_2(query *q)
{
	GET_FIRST_ARG(pstr,stream_or_alias);
	int n = get_stream(q, pstr);
	stream *str = &q->pl->streams[n];
	GET_NEXT_ARG(p1,integer);

	if (is_bigint(p1))
		return throw_error(q, p1, p1_ctx, "domain_error", "small_integer_range");

	if (!strcmp(str->mode, "read"))
		return throw_error(q, pstr, q->st.cur_ctx, "permission_error", "output,stream");

	if (str->binary) {
		cell tmp;
		make_int(&tmp, n);
		return throw_error(q, &tmp, q->st.cur_ctx, "permission_error", "output,binary_stream");
	}

	if (!is_integer(p1))
		return throw_error(q, p1, p1_ctx, "type_error", "integer");

	if (is_integer(p1) && is_le(p1,-1))
		return throw_error(q, p1, p1_ctx, "representation_error", "character_code");

	int ch = (int)get_smallint(p1);
	char tmpbuf[MAX_BYTES_PER_CODEPOINT+1];
	put_char_utf8(tmpbuf, ch);
	tpl_write(tmpbuf, strlen(tmpbuf), str);
	return true;
}

static bool bif_iso_put_byte_1(query *q)
{
	GET_FIRST_ARG(p1,byte);
	int n = q->pl->current_output;
	stream *str = &q->pl->streams[n];

	if (is_bigint(p1))
		return throw_error(q, p1, p1_ctx, "domain_error", "small_integer_range");

	if (!str->binary) {
		cell tmp;
		make_int(&tmp, n);
		return throw_error(q, &tmp, q->st.cur_ctx, "permission_error", "output,text_stream");
	}

	if (!is_integer(p1))
		return throw_error(q, p1, p1_ctx, "type_error", "integer");

	if (is_integer(p1) && is_le(p1,-1))
		return throw_error(q, p1, p1_ctx, "representation_error", "character_code");

	int ch = (int)get_smallint(p1);
	char tmpbuf[80];
	snprintf(tmpbuf, sizeof(tmpbuf), "%c", ch);
	tpl_write(tmpbuf, 1, str);
	return true;
}

static bool bif_iso_put_byte_2(query *q)
{
	GET_FIRST_ARG(pstr,stream_or_alias);
	int n = get_stream(q, pstr);
	stream *str = &q->pl->streams[n];
	GET_NEXT_ARG(p1,byte);

	if (is_bigint(p1))
		return throw_error(q, p1, p1_ctx, "domain_error", "small_integer_range");

	if (!strcmp(str->mode, "read"))
		return throw_error(q, pstr, q->st.cur_ctx, "permission_error", "output,stream");

	if (!str->binary)
		return throw_error(q, pstr, q->st.cur_ctx, "permission_error", "output,text_stream");

	if (!is_integer(p1))
		return throw_error(q, p1, p1_ctx, "type_error", "integer");

	if (is_integer(p1) && is_le(p1,-1))
		return throw_error(q, p1, p1_ctx, "representation_error", "character_code");

	int ch = (int)get_smallint(p1);
	char tmpbuf[80];
	snprintf(tmpbuf, sizeof(tmpbuf), "%c", ch);
	tpl_write(tmpbuf, 1, str);
	return true;
}

// ISO 13211-1 8.12.1.3 i (and 8.12.2.3 i): "the entity input from the
// stream is not a character". The culprit is what was read rather than
// any argument, so the error context is just the offending goal...

#define CHECK_UTF8_INPUT(q, str, ch)								\
	if ((ch) == UTF8_INVALID) {										\
		(str)->did_getc = false;									\
		(str)->invalid_pending = false;								\
		clearerr((str)->fp_in);										\
		return throw_error((q), (q)->st.instr, (q)->st.cur_ctx,		\
			"representation_error", "character");					\
	}

// A peek must not consume what it reports on, but by the time the
// sequence is known to be ill-formed its octets have been taken from
// the underlying stream, and there is nowhere to put them back: ungetch
// holds one decoded codepoint, and UTF8_INVALID is not one. Nor would a
// single slot do, since an ill-formed sequence may be several octets.
//
// So the stream remembers instead that the character behind it is not a
// character. A later peek throws again without reading anything, and
// the get that finally takes it clears the flag - which is what
// consuming "an entity that is not a character" amounts to. Repeated
// peeks are then idempotent, as peeks should be.

#define CHECK_UTF8_PEEK(q, str, ch)									\
	if ((ch) == UTF8_INVALID) {										\
		(str)->did_getc = false;									\
		(str)->invalid_pending = true;								\
		clearerr((str)->fp_in);										\
		return throw_error((q), (q)->st.instr, (q)->st.cur_ctx,		\
			"representation_error", "character");					\
	}

// Peeking again at a sequence already known to be ill-formed...

#define PEEK_UTF8_PENDING(q, str)									\
	if ((str)->invalid_pending) {									\
		return throw_error((q), (q)->st.instr, (q)->st.cur_ctx,		\
			"representation_error", "character");					\
	}

// ...and reading it, which consumes it.

#define GET_UTF8_PENDING(q, str)									\
	if ((str)->invalid_pending) {									\
		(str)->invalid_pending = false;								\
		return throw_error((q), (q)->st.instr, (q)->st.cur_ctx,		\
			"representation_error", "character");					\
	}

// A character may also come from the line the parser has already read
// off the stream, and those bytes are the stream's bytes: an
// ill-formed sequence among them is no more a character there than it
// is when read directly (issue #1099). The liberal in-memory decoder
// cannot say so - it decodes 0xff to 0, and the caller then reads on
// past the buffer and reports the end of the file - so the buffer is
// decoded strictly here too.
//
// Unlike the stream, the buffer can hold an ill-formed sequence back:
// a peek simply does not advance srcptr, so repeated peeks throw
// again on their own and there is no invalid_pending to remember.

#define TAKE_BUFFERED_CHAR(q, str)									\
	if (!(str)->ungetch && (str)->p								\
		&& (str)->p->srcptr && *(str)->p->srcptr) {					\
		int _ch = get_char_utf8_strict((const char**)&(str)->p->srcptr);	\
		CHECK_UTF8_INPUT((q), (str), _ch);							\
		(str)->ungetch = _ch;										\
	}

#define PEEK_BUFFERED_CHAR(q, str)									\
	if (!(str)->ungetch && (str)->p								\
		&& (str)->p->srcptr && *(str)->p->srcptr) {					\
		int _ch = peek_char_utf8_strict((const char*)(str)->p->srcptr);	\
		if (_ch == UTF8_INVALID)									\
			return throw_error((q), (q)->st.instr, (q)->st.cur_ctx,	\
				"representation_error", "character");				\
		(str)->ungetch = _ch;										\
	}

static bool bif_iso_get_char_1(query *q)
{
	GET_FIRST_ARG(p1,in_character_or_var);
	int n = q->pl->current_input;
	stream *str = &q->pl->streams[n];

	if (str->binary) {
		cell tmp;
		make_int(&tmp, n);
		return throw_error(q, &tmp, q->st.cur_ctx, "permission_error", "input,binary_stream");
	}

	if (str->at_end_of_file && (str->eof_action == eof_action_error)) {
		cell tmp;
		make_int(&tmp, n);
		return throw_error(q, &tmp, q->st.cur_ctx, "permission_error", "input,past_end_of_stream");
	}

	GET_UTF8_PENDING(q, str);
	TAKE_BUFFERED_CHAR(q, str);

	if (isatty(fileno(str->fp_in)) && !str->did_getc && !str->ungetch) {
		fprintf(str->fp_out, "%s", PROMPT);

		if (fflush(str->fp_out))
			return throw_error(q, q->st.instr, q->st.cur_ctx, "system_error", strerror(errno));
	}

	struct retry_ctx_ ctx = { q, str };
	int ch = str->ungetch ? str->ungetch : xgetc_utf8(retry_getc, &ctx);

	if (errno == EINTR) {
		clearerr(str->fp_in);
		return throw_timeout(q);
	}

	if (q->is_task && !feof(str->fp) && ferror(str->fp)) {
		clearerr(str->fp_in);
		return do_yield_on_stream(q, str, false);
	}

	CHECK_UTF8_INPUT(q, str, ch);

	str->did_getc = true;

	if (FEOF(str)) {
		str->did_getc = false;
		str->at_end_of_file = str->eof_action != eof_action_reset;

		if (str->eof_action == eof_action_reset)
			clearerr(str->fp_in);

		cell tmp;
		make_atom(&tmp, g_eof_s);
		return unify(q, p1, p1_ctx, &tmp, q->st.cur_ctx);
	}

	str->ungetch = 0;

	if (ch == '\n') {
		str->did_getc = false;

		if (str->p)
			str->p->line_num++;
	}

	char tmpbuf[MAX_BYTES_PER_CODEPOINT+1];
	n = put_char_utf8(tmpbuf, ch);
	cell tmp;
	make_smalln(&tmp, tmpbuf, n);
	return unify(q, p1, p1_ctx, &tmp, q->st.cur_ctx);
}

static bool bif_iso_get_char_2(query *q)
{
	GET_FIRST_ARG(pstr,stream_or_alias);
	int n = get_stream(q, pstr);
	stream *str = &q->pl->streams[n];
	GET_NEXT_ARG(p1,in_character_or_var);

	if (strcmp(str->mode, "read") && strcmp(str->mode, "update"))
		return throw_error(q, pstr, q->st.cur_ctx, "permission_error", "input,stream");

	if (str->binary) {
		cell tmp;
		make_int(&tmp, n);
		return throw_error(q, &tmp, q->st.cur_ctx, "permission_error", "input,binary_stream");
	}

	if (str->at_end_of_file && (str->eof_action == eof_action_error)) {
		cell tmp;
		make_int(&tmp, n);
		return throw_error(q, &tmp, q->st.cur_ctx, "permission_error", "input,past_end_of_stream");
	}

	GET_UTF8_PENDING(q, str);
	TAKE_BUFFERED_CHAR(q, str);

	if (isatty(fileno(str->fp)) && !str->did_getc && !str->ungetch) {
		fprintf(str->fp_out, "%s", PROMPT);

		if (fflush(str->fp_out))
			return throw_error(q, q->st.instr, q->st.cur_ctx, "system_error", strerror(errno));
	}

	struct retry_ctx_ ctx = { q, str };
	int ch = str->ungetch ? str->ungetch : xgetc_utf8(retry_getc, &ctx);

	if (errno == EINTR) {
		clearerr(str->fp_in);
		return throw_timeout(q);
	}

	if (q->is_task && !feof(str->fp) && ferror(str->fp)) {
		clearerr(str->fp_in);
		return do_yield_on_stream(q, str, false);
	}

	CHECK_UTF8_INPUT(q, str, ch);

	str->did_getc = true;

	if (FEOF(str)) {
		str->did_getc = false;
		str->at_end_of_file = str->eof_action != eof_action_reset;

		if (str->eof_action == eof_action_reset)
			clearerr(str->fp_in);

		cell tmp;
		make_atom(&tmp, g_eof_s);
		return unify(q, p1, p1_ctx, &tmp, q->st.cur_ctx);
	}

	str->ungetch = 0;

	if (ch == '\n') {
		str->did_getc = false;

		if (str->p)
			str->p->line_num++;
	}

	char tmpbuf[MAX_BYTES_PER_CODEPOINT+1];
	n = put_char_utf8(tmpbuf, ch);
	cell tmp;
	make_smalln(&tmp, tmpbuf, n);
	return unify(q, p1, p1_ctx, &tmp, q->st.cur_ctx);
}

static bool bif_iso_get_code_1(query *q)
{
	GET_FIRST_ARG(p1,integer_or_var);
	int n = q->pl->current_input;
	stream *str = &q->pl->streams[n];

	if (is_bigint(p1))
		return throw_error(q, p1, p1_ctx, "domain_error", "small_integer_range");

	if (is_integer(p1) && (get_smallint(p1) < -1))
		return throw_error(q, p1, p1_ctx, "representation_error", "in_character_code");

	if (str->binary) {
		cell tmp;
		make_int(&tmp, n);
		return throw_error(q, &tmp, q->st.cur_ctx, "permission_error", "input,binary_stream");
	}

	if (str->at_end_of_file && (str->eof_action == eof_action_error)) {
		cell tmp;
		make_int(&tmp, n);
		return throw_error(q, &tmp, q->st.cur_ctx, "permission_error", "input,past_end_of_stream");
	}

	GET_UTF8_PENDING(q, str);
	TAKE_BUFFERED_CHAR(q, str);

	if (isatty(fileno(str->fp)) && !str->did_getc && !str->ungetch) {
		fprintf(str->fp_out, "%s", PROMPT);
		fflush(str->fp_out);
	}

	struct retry_ctx_ ctx = { q, str };
	int ch = str->ungetch ? str->ungetch : xgetc_utf8(retry_getc, &ctx);

	if (errno == EINTR) {
		clearerr(str->fp_in);
		return throw_timeout(q);
	}

	if (q->is_task && !feof(str->fp) && ferror(str->fp)) {
		clearerr(str->fp_in);
		return do_yield_on_stream(q, str, false);
	}

	CHECK_UTF8_INPUT(q, str, ch);

	str->did_getc = true;

	if (FEOF(str)) {
		str->did_getc = false;
		str->at_end_of_file = str->eof_action != eof_action_reset;

		if (str->eof_action == eof_action_reset)
			clearerr(str->fp_in);

		cell tmp;
		make_int(&tmp, -1);
		return unify(q, p1, p1_ctx, &tmp, q->st.cur_ctx);
	}

	str->ungetch = 0;

	if (ch == '\n') {
		str->did_getc = false;

		if (str->p)
			str->p->line_num++;
	} else if (ch == EOF)
		str->did_getc = false;

	cell tmp;
	make_int(&tmp, ch);
	return unify(q, p1, p1_ctx, &tmp, q->st.cur_ctx);
}

static bool bif_iso_get_code_2(query *q)
{
	GET_FIRST_ARG(pstr,stream_or_alias);
	int n = get_stream(q, pstr);
	stream *str = &q->pl->streams[n];
	GET_NEXT_ARG(p1,integer_or_var);

	if (is_bigint(p1))
		return throw_error(q, p1, p1_ctx, "domain_error", "small_integer_range");

	if (is_integer(p1) && (get_smallint(p1) < -1))
		return throw_error(q, p1, p1_ctx, "representation_error", "in_character_code");

	if (strcmp(str->mode, "read"))
		return throw_error(q, pstr, q->st.cur_ctx, "permission_error", "input,stream");

	if (str->binary) {
		cell tmp;
		make_int(&tmp, n);
		return throw_error(q, &tmp, q->st.cur_ctx, "permission_error", "input,binary_stream");
	}

	if (str->at_end_of_file && (str->eof_action == eof_action_error)) {
		cell tmp;
		make_int(&tmp, n);
		return throw_error(q, &tmp, q->st.cur_ctx, "permission_error", "input,past_end_of_stream");
	}

	GET_UTF8_PENDING(q, str);
	TAKE_BUFFERED_CHAR(q, str);

	if (isatty(fileno(str->fp)) && !str->did_getc && !str->ungetch) {
		fprintf(str->fp_out, "%s", PROMPT);
		fflush(str->fp_out);
	}

	struct retry_ctx_ ctx = { q, str };
	int ch = str->ungetch ? str->ungetch : xgetc_utf8(retry_getc, &ctx);

	if (errno == EINTR) {
		clearerr(str->fp_in);
		return throw_timeout(q);
	}

	if (q->is_task && !feof(str->fp) && ferror(str->fp)) {
		clearerr(str->fp_in);
		return do_yield_on_stream(q, str, false);
	}

	CHECK_UTF8_INPUT(q, str, ch);

	str->did_getc = true;

	if (FEOF(str)) {
		str->did_getc = false;
		str->at_end_of_file = str->eof_action != eof_action_reset;

		if (str->eof_action == eof_action_reset)
			clearerr(str->fp_in);

		cell tmp;
		make_int(&tmp, -1);
		return unify(q, p1, p1_ctx, &tmp, q->st.cur_ctx);
	}

	str->ungetch = 0;

	if (ch == '\n') {
		str->did_getc = false;

		if (str->p)
			str->p->line_num++;
	}

	cell tmp;
	make_int(&tmp, ch);
	return unify(q, p1, p1_ctx, &tmp, q->st.cur_ctx);
}

static bool bif_iso_get_byte_1(query *q)
{
	GET_FIRST_ARG(p1,in_byte_or_var);
	int n = q->pl->current_input;
	stream *str = &q->pl->streams[n];

	if (!str->binary) {
		cell tmp;
		make_int(&tmp, n);
		return throw_error(q, &tmp, q->st.cur_ctx, "permission_error", "input,text_stream");
	}

	if (str->at_end_of_file && (str->eof_action == eof_action_error)) {
		cell tmp;
		make_int(&tmp, n);
		return throw_error(q, &tmp, q->st.cur_ctx, "permission_error", "input,past_end_of_stream");
	}

	if (!str->ungetch && str->p) {
		if (str->p->srcptr && *str->p->srcptr) {
			int ch = *str->p->srcptr++;
			str->ungetch = ch;
		}
	}

	if (isatty(fileno(str->fp)) && !str->did_getc && !str->ungetch) {
		fprintf(str->fp_out, "%s", PROMPT);
		fflush(str->fp_out);
	}

	struct retry_ctx_ ctx = { q, str };
	int ch = str->ungetch ? str->ungetch : retry_getc(&ctx);

	if (errno == EINTR) {
		clearerr(str->fp_in);
		return throw_timeout(q);
	}

	if (q->is_task && !feof(str->fp) && ferror(str->fp)) {
		clearerr(str->fp_in);
		return do_yield_on_stream(q, str, false);
	}

	str->did_getc = true;

	if (FEOF(str)) {
		str->did_getc = false;
		str->at_end_of_file = str->eof_action != eof_action_reset;

		if (str->eof_action == eof_action_reset)
			clearerr(str->fp_in);

		cell tmp;
		make_int(&tmp, -1);
		return unify(q, p1, p1_ctx, &tmp, q->st.cur_ctx);
	}

	str->ungetch = 0;

	cell tmp;
	make_int(&tmp, ch);
	return unify(q, p1, p1_ctx, &tmp, q->st.cur_ctx);
}

static bool bif_iso_get_byte_2(query *q)
{
	GET_FIRST_ARG(pstr,stream_or_alias);
	int n = get_stream(q, pstr);
	stream *str = &q->pl->streams[n];
	GET_NEXT_ARG(p1,in_byte_or_var);

	if (strcmp(str->mode, "read") && strcmp(str->mode, "update"))
		return throw_error(q, pstr, q->st.cur_ctx, "permission_error", "input,stream");

	if (!str->binary) {
		cell tmp;
		make_int(&tmp, n);
		return throw_error(q, &tmp, q->st.cur_ctx, "permission_error", "input,text_stream");
	}

	if (str->at_end_of_file && (str->eof_action == eof_action_error)) {
		cell tmp;
		make_int(&tmp, n);
		return throw_error(q, &tmp, q->st.cur_ctx, "permission_error", "input,past_end_of_stream");
	}

	if (!str->ungetch && str->p) {
		if (str->p->srcptr && *str->p->srcptr) {
			int ch = *str->p->srcptr;
			str->ungetch = ch;
		}
	}

	if (isatty(fileno(str->fp)) && !str->did_getc && !str->ungetch) {
		fprintf(str->fp_out, "%s", PROMPT);
		fflush(str->fp_out);
	}

	struct retry_ctx_ ctx = { q, str };
	int ch = str->ungetch ? str->ungetch : retry_getc(&ctx);

	if (errno == EINTR) {
		clearerr(str->fp_in);
		return throw_timeout(q);
	}

	if (q->is_task && !feof(str->fp) && ferror(str->fp)) {
		clearerr(str->fp_in);
		return do_yield_on_stream(q, str, false);
	}

	str->did_getc = true;

	if (FEOF(str)) {
		str->did_getc = false;
		str->at_end_of_file = str->eof_action != eof_action_reset;

		if (str->eof_action == eof_action_reset)
			clearerr(str->fp_in);

		cell tmp;
		make_int(&tmp, -1);
		return unify(q, p1, p1_ctx, &tmp, q->st.cur_ctx);
	}

	str->ungetch = 0;
	cell tmp;
	make_int(&tmp, ch);
	return unify(q, p1, p1_ctx, &tmp, q->st.cur_ctx);
}

// The argument is a character, not its code - the type check is
// in_character - so the code point comes out of the atom the way
// put_char/2 takes it. get_smallint() read the val_int arm of the
// cell's union instead, which for an atom is its offset in the symbol
// table: unget_char(S, a) pushed back U+6101, wherever 'a' happened to
// sit.
//
// One character can be held back at a time, str->ungetch being a
// single code point, so ungetting twice without an intervening read
// keeps only the second.

static bool do_unget_char(query *q, stream *str, cell *p1, pl_ctx p1_ctx)
{
	// in_character admits end_of_file, and a get_char/2 that reported
	// it consumed nothing, so putting it back restores nothing: the
	// stream is left exactly as it is, rather than having a genuine
	// end-of-file state cleared under it.

	if (!CMP_STRING_TO_CSTR(q, p1, "end_of_file"))
		return true;

	const char *src = C_STR(q, p1);

	if (!*src)				// '' passes the type check but is no character
		return throw_error(q, p1, p1_ctx, "type_error", "in_character");

	str->did_getc = false;
	str->at_end_of_file = false;
	str->ungetch = get_char_utf8(&src);
	return true;
}

static bool bif_unget_char_1(query *q)
{
	GET_FIRST_ARG(p1,in_character);
	int n = q->pl->current_input;
	stream *str = &q->pl->streams[n];

	if (str->binary) {
		cell tmp;
		make_int(&tmp, n);
		return throw_error(q, &tmp, q->st.cur_ctx, "permission_error", "input,binary_stream");
	}

	if (str->at_end_of_file && (str->eof_action == eof_action_error)) {
		cell tmp;
		make_int(&tmp, n);
		return throw_error(q, &tmp, q->st.cur_ctx, "permission_error", "input,past_end_of_stream");
	}

	return do_unget_char(q, str, p1, p1_ctx);
}

static bool bif_unget_char_2(query *q)
{
	GET_FIRST_ARG(pstr,stream_or_alias);
	int n = get_stream(q, pstr);
	stream *str = &q->pl->streams[n];
	GET_NEXT_ARG(p1,in_character);

	if (strcmp(str->mode, "read") && strcmp(str->mode, "update"))
		return throw_error(q, pstr, q->st.cur_ctx, "permission_error", "input,stream");

	if (str->binary) {
		cell tmp;
		make_int(&tmp, n);
		return throw_error(q, &tmp, q->st.cur_ctx, "permission_error", "input,binary_stream");
	}

	// Nothing is read here, so there is no character to fetch ahead
	// into str->ungetch first: the block that did consumed one out of
	// the parser's line buffer and then had it overwritten below,
	// losing it.

	return do_unget_char(q, str, p1, p1_ctx);
}

static bool bif_unget_code_1(query *q)
{
	GET_FIRST_ARG(p1,integer);
	int n = q->pl->current_input;
	stream *str = &q->pl->streams[n];

	if (is_bigint(p1))
		return throw_error(q, p1, p1_ctx, "domain_error", "small_integer_range");

	if (is_integer(p1) && (get_smallint(p1) < -1))
		return throw_error(q, p1, p1_ctx, "representation_error", "in_character_code");

	if (str->binary) {
		cell tmp;
		make_int(&tmp, n);
		return throw_error(q, &tmp, q->st.cur_ctx, "permission_error", "input,binary_stream");
	}

	str->did_getc = false;
	str->at_end_of_file = false;
	str->ungetch = get_smallint(p1);
	return true;
}

static bool bif_unget_code_2(query *q)
{
	GET_FIRST_ARG(pstr,stream_or_alias);
	int n = get_stream(q, pstr);
	stream *str = &q->pl->streams[n];
	GET_NEXT_ARG(p1,integer);

	if (is_bigint(p1))
		return throw_error(q, p1, p1_ctx, "domain_error", "small_integer_range");

	if (is_integer(p1) && (get_smallint(p1) < -1))
		return throw_error(q, p1, p1_ctx, "representation_error", "in_character_code");

	if (strcmp(str->mode, "read"))
		return throw_error(q, pstr, q->st.cur_ctx, "permission_error", "input,stream");

	if (str->binary) {
		cell tmp;
		make_int(&tmp, n);
		return throw_error(q, &tmp, q->st.cur_ctx, "permission_error", "input,binary_stream");
	}

	str->did_getc = false;
	str->at_end_of_file = false;
	str->ungetch = get_smallint(p1);
	return true;
}

static bool bif_unget_byte_1(query *q)
{
	GET_FIRST_ARG(p1,in_byte);
	int n = q->pl->current_input;
	stream *str = &q->pl->streams[n];

	if (!str->binary) {
		cell tmp;
		make_int(&tmp, n);
		return throw_error(q, &tmp, q->st.cur_ctx, "permission_error", "input,text_stream");
	}

	str->did_getc = false;
	str->at_end_of_file = false;
	str->ungetch = get_smallint(p1);
	return true;
}

static bool bif_unget_byte_2(query *q)
{
	GET_FIRST_ARG(pstr,stream_or_alias);
	int n = get_stream(q, pstr);
	stream *str = &q->pl->streams[n];
	GET_NEXT_ARG(p1,in_byte);

	if (strcmp(str->mode, "read") && strcmp(str->mode, "update"))
		return throw_error(q, pstr, q->st.cur_ctx, "permission_error", "input,stream");

	if (!str->binary) {
		cell tmp;
		make_int(&tmp, n);
		return throw_error(q, &tmp, q->st.cur_ctx, "permission_error", "input,text_stream");
	}

	str->did_getc = false;
	str->at_end_of_file = false;
	str->ungetch = get_smallint(p1);
	return true;
}

static bool bif_iso_peek_char_1(query *q)
{
	GET_FIRST_ARG(p1,in_character_or_var);
	int n = q->pl->current_input;
	stream *str = &q->pl->streams[n];

	if (str->binary) {
		cell tmp;
		make_int(&tmp, n);
		return throw_error(q, &tmp, q->st.cur_ctx, "permission_error", "input,binary_stream");
	}

	if (str->at_end_of_file && (str->eof_action == eof_action_error)) {
		cell tmp;
		make_int(&tmp, n);
		return throw_error(q, &tmp, q->st.cur_ctx, "permission_error", "input,past_end_of_stream");
	}

	PEEK_UTF8_PENDING(q, str);
	PEEK_BUFFERED_CHAR(q, str);

	struct retry_ctx_ ctx = { q, str };
	int ch = str->ungetch ? str->ungetch : xgetc_utf8(retry_getc, &ctx);

	if (errno == EINTR) {
		clearerr(str->fp_in);
		return throw_timeout(q);
	}

	if (q->is_task && !feof(str->fp) && ferror(str->fp)) {
		clearerr(str->fp_in);
		return do_yield_on_stream(q, str, false);
	}

	CHECK_UTF8_PEEK(q, str, ch);

	if (FEOF(str)) {
		str->did_getc = false;
		clearerr(str->fp_in);
		cell tmp;
		make_atom(&tmp, g_eof_s);
		return unify(q, p1, p1_ctx, &tmp, q->st.cur_ctx);
	}

	str->ungetch = ch;
	char tmpbuf[MAX_BYTES_PER_CODEPOINT+1];
	n = put_char_utf8(tmpbuf, ch);
	cell tmp;
	make_smalln(&tmp, tmpbuf, n);
	return unify(q, p1, p1_ctx, &tmp, q->st.cur_ctx);
}

static bool bif_iso_peek_char_2(query *q)
{
	GET_FIRST_ARG(pstr,stream_or_alias);
	int n = get_stream(q, pstr);
	stream *str = &q->pl->streams[n];
	GET_NEXT_ARG(p1,in_character_or_var);

	if (strcmp(str->mode, "read"))
		return throw_error(q, pstr, q->st.cur_ctx, "permission_error", "input,stream");

	if (str->binary) {
		cell tmp;
		make_int(&tmp, n);
		return throw_error(q, &tmp, q->st.cur_ctx, "permission_error", "input,binary_stream");
	}

	if (str->at_end_of_file && (str->eof_action == eof_action_error)) {
		cell tmp;
		make_int(&tmp, n);
		return throw_error(q, &tmp, q->st.cur_ctx, "permission_error", "input,past_end_of_stream");
	}

	PEEK_UTF8_PENDING(q, str);
	PEEK_BUFFERED_CHAR(q, str);

	struct retry_ctx_ ctx = { q, str };
	int ch = str->ungetch ? str->ungetch : xgetc_utf8(retry_getc, &ctx);

	if (errno == EINTR) {
		clearerr(str->fp_in);
		return throw_timeout(q);
	}

	if (q->is_task && !feof(str->fp) && ferror(str->fp)) {
		clearerr(str->fp_in);
		return do_yield_on_stream(q, str, false);
	}

	CHECK_UTF8_PEEK(q, str, ch);

	if (FEOF(str)) {
		str->did_getc = false;
		clearerr(str->fp_in);
		cell tmp;
		make_atom(&tmp, g_eof_s);
		return unify(q, p1, p1_ctx, &tmp, q->st.cur_ctx);
	}

	str->ungetch = ch;
	char tmpbuf[MAX_BYTES_PER_CODEPOINT+1];
	n = put_char_utf8(tmpbuf, ch);
	cell tmp;
	make_smalln(&tmp, tmpbuf, n);
	return unify(q, p1, p1_ctx, &tmp, q->st.cur_ctx);
}

static bool bif_iso_peek_code_1(query *q)
{
	GET_FIRST_ARG(p1,integer_or_var);
	int n = q->pl->current_input;
	stream *str = &q->pl->streams[n];

	if (is_bigint(p1))
		return throw_error(q, p1, p1_ctx, "domain_error", "small_integer_range");

	if (is_integer(p1) && (get_smallint(p1) < -1))
		return throw_error(q, p1, p1_ctx, "representation_error", "in_character_code");

	if (str->binary) {
		cell tmp;
		make_int(&tmp, n);
		return throw_error(q, &tmp, q->st.cur_ctx, "permission_error", "input,binary_stream");
	}

	if (str->at_end_of_file && (str->eof_action == eof_action_error)) {
		cell tmp;
		make_int(&tmp, n);
		return throw_error(q, &tmp, q->st.cur_ctx, "permission_error", "input,past_end_of_stream");
	}

	PEEK_UTF8_PENDING(q, str);
	PEEK_BUFFERED_CHAR(q, str);

	struct retry_ctx_ ctx = { q, str };
	int ch = str->ungetch ? str->ungetch : xgetc_utf8(retry_getc, &ctx);

	if (errno == EINTR) {
		clearerr(str->fp_in);
		return throw_timeout(q);
	}

	if (q->is_task && !feof(str->fp) && ferror(str->fp)) {
		clearerr(str->fp_in);
		return do_yield_on_stream(q, str, false);
	}

	CHECK_UTF8_PEEK(q, str, ch);

	if (FEOF(str)) {
		str->did_getc = false;
		clearerr(str->fp_in);
		cell tmp;
		make_int(&tmp, -1);
		return unify(q, p1, p1_ctx, &tmp, q->st.cur_ctx);
	}

	str->ungetch = ch;
	cell tmp;
	make_int(&tmp, ch);
	return unify(q, p1, p1_ctx, &tmp, q->st.cur_ctx);
}

static bool bif_iso_peek_code_2(query *q)
{
	GET_FIRST_ARG(pstr,stream_or_alias);
	int n = get_stream(q, pstr);
	stream *str = &q->pl->streams[n];
	GET_NEXT_ARG(p1,integer_or_var);

	if (is_bigint(p1))
		return throw_error(q, p1, p1_ctx, "domain_error", "small_integer_range");

	if (is_integer(p1) && (get_smallint(p1) < -1))
		return throw_error(q, p1, p1_ctx, "representation_error", "in_character_code");

	if (strcmp(str->mode, "read"))
		return throw_error(q, pstr, q->st.cur_ctx, "permission_error", "input,stream");

	if (str->binary) {
		cell tmp;
		make_int(&tmp, n);
		return throw_error(q, &tmp, q->st.cur_ctx, "permission_error", "input,binary_stream");
	}

	if (str->at_end_of_file && (str->eof_action == eof_action_error)) {
		cell tmp;
		make_int(&tmp, n);
		return throw_error(q, &tmp, q->st.cur_ctx, "permission_error", "input,past_end_of_stream");
	}

	PEEK_UTF8_PENDING(q, str);
	PEEK_BUFFERED_CHAR(q, str);

	struct retry_ctx_ ctx = { q, str };
	int ch = str->ungetch ? str->ungetch : xgetc_utf8(retry_getc, &ctx);

	if (errno == EINTR) {
		clearerr(str->fp_in);
		return throw_timeout(q);
	}

	if (q->is_task && !feof(str->fp) && ferror(str->fp)) {
		clearerr(str->fp_in);
		return do_yield_on_stream(q, str, false);
	}

	CHECK_UTF8_PEEK(q, str, ch);

	if (FEOF(str)) {
		str->did_getc = false;
		clearerr(str->fp_in);
		cell tmp;
		make_int(&tmp, -1);
		return unify(q, p1, p1_ctx, &tmp, q->st.cur_ctx);
	}

	str->ungetch = ch;
	cell tmp;
	make_int(&tmp, ch);
	return unify(q, p1, p1_ctx, &tmp, q->st.cur_ctx);
}

static bool bif_iso_peek_byte_1(query *q)
{
	GET_FIRST_ARG(p1,in_byte_or_var);
	int n = q->pl->current_input;
	stream *str = &q->pl->streams[n];

	if (!str->binary) {
		cell tmp;
		make_int(&tmp, n);
		return throw_error(q, &tmp, q->st.cur_ctx, "permission_error", "input,text_stream");
	}

	if (str->at_end_of_file && (str->eof_action == eof_action_error)) {
		cell tmp;
		make_int(&tmp, n);
		return throw_error(q, &tmp, q->st.cur_ctx, "permission_error", "input,past_end_of_stream");
	}

	if (!str->ungetch && str->p) {
		if (str->p->srcptr && *str->p->srcptr) {
			int ch = peek_char_utf8((const char*)str->p->srcptr);
			str->ungetch = ch;
		}
	}

	struct retry_ctx_ ctx = { q, str };
	int ch = str->ungetch ? str->ungetch : retry_getc(&ctx);

	if (errno == EINTR) {
		clearerr(str->fp_in);
		return throw_timeout(q);
	}

	if (q->is_task && !feof(str->fp) && ferror(str->fp)) {
		clearerr(str->fp_in);
		return do_yield_on_stream(q, str, false);
	}

	if (FEOF(str)) {
		clearerr(str->fp_in);
		cell tmp;
		make_int(&tmp, -1);
		return unify(q, p1, p1_ctx, &tmp, q->st.cur_ctx);
	}

	str->ungetch = ch;
	cell tmp;
	make_int(&tmp, ch);
	return unify(q, p1, p1_ctx, &tmp, q->st.cur_ctx);
}

static bool bif_iso_peek_byte_2(query *q)
{
	GET_FIRST_ARG(pstr,stream_or_alias);
	int n = get_stream(q, pstr);
	stream *str = &q->pl->streams[n];
	GET_NEXT_ARG(p1,in_byte_or_var);

	if (strcmp(str->mode, "read"))
		return throw_error(q, pstr, q->st.cur_ctx, "permission_error", "input,stream");

	if (!str->binary) {
		cell tmp;
		make_int(&tmp, n);
		return throw_error(q, &tmp, q->st.cur_ctx, "permission_error", "input,text_stream");
	}

	if (str->at_end_of_file && (str->eof_action == eof_action_error)) {
		cell tmp;
		make_int(&tmp, n);
		return throw_error(q, &tmp, q->st.cur_ctx, "permission_error", "input,past_end_of_stream");
	}

	if (!str->ungetch && str->p) {
		if (str->p->srcptr && *str->p->srcptr) {
			int ch = peek_char_utf8((const char*)str->p->srcptr);
			str->ungetch = ch;
		}
	}

	struct retry_ctx_ ctx = { q, str };
	int ch = str->ungetch ? str->ungetch : retry_getc(&ctx);

	if (errno == EINTR) {
		clearerr(str->fp_in);
		return throw_timeout(q);
	}

	if (q->is_task && !feof(str->fp) && ferror(str->fp)) {
		clearerr(str->fp_in);
		return do_yield_on_stream(q, str, false);
	}

	if (FEOF(str)) {
		clearerr(str->fp_in);
		cell tmp;
		make_int(&tmp, -1);
		return unify(q, p1, p1_ctx, &tmp, q->st.cur_ctx);
	}

	str->ungetch = ch;
	cell tmp;
	make_int(&tmp, ch);
	return unify(q, p1, p1_ctx, &tmp, q->st.cur_ctx);
}

static bool bif_iso_current_input_1(query *q)
{
	GET_FIRST_ARG(pstr,any);

	if (is_var(pstr)) {
		cell tmp;
		make_int(&tmp, q->pl->current_input);
		tmp.flags |= FLAG_INT_STREAM;
		return unify(q, pstr, pstr_ctx, &tmp, q->st.cur_ctx);
	}

	if (!is_stream(pstr))
		return throw_error(q, pstr, q->st.cur_ctx, "domain_error", "stream");

	int n = get_stream(q, pstr);
	return n == (int)q->pl->current_input ? true : false;
}

static bool bif_iso_current_output_1(query *q)
{
	GET_FIRST_ARG(pstr,any);

	if (is_var(pstr)) {
		cell tmp;
		make_int(&tmp, q->pl->current_output);
		tmp.flags |= FLAG_INT_STREAM;
		return unify(q, pstr, pstr_ctx, &tmp, q->st.cur_ctx);
	}

	if (!is_stream(pstr))
		return throw_error(q, pstr, q->st.cur_ctx, "domain_error", "stream");

	int n = get_stream(q, pstr);
	return n == (int)q->pl->current_output ? true : false;
}

static bool bif_iso_current_error_1(query *q)
{
	GET_FIRST_ARG(pstr,any);

	if (is_var(pstr)) {
		cell tmp;
		make_int(&tmp, q->pl->current_error);
		tmp.flags |= FLAG_INT_STREAM;
		return unify(q, pstr, pstr_ctx, &tmp, q->st.cur_ctx);
	}

	if (!is_stream(pstr))
		return throw_error(q, pstr, q->st.cur_ctx, "domain_error", "stream");

	int n = get_stream(q, pstr);
	return n == (int)q->pl->current_error ? true : false;
}

static bool bif_iso_set_input_1(query *q)
{
	GET_FIRST_ARG(pstr,stream_or_alias);
	int n = get_stream(q, pstr);
	stream *str = &q->pl->streams[n];

	if (strcmp(str->mode, "read") && strcmp(str->mode, "update"))
		return throw_error(q, pstr, q->st.cur_ctx, "permission_error", "input,stream");

	q->pl->current_input = n;
	return true;
}

static bool bif_iso_set_output_1(query *q)
{
	GET_FIRST_ARG(pstr,stream_or_alias);
	int n = get_stream(q, pstr);
	stream *str = &q->pl->streams[n];

	if (!strcmp(str->mode, "read"))
		return throw_error(q, pstr, q->st.cur_ctx, "permission_error", "output,stream");

	q->pl->current_output = n;
	return true;
}

static bool bif_iso_set_stream_position_2(query *q)
{
	GET_FIRST_ARG(pstr,stream_or_alias);
	int n = get_stream(q, pstr);
	stream *str = &q->pl->streams[n];
	GET_NEXT_ARG(p1,any);

	if (str->is_socket || (n <= 2) || !str->repo)
		return throw_error(q, p1, p1_ctx, "permission_error", "reposition,stream");

	if (!is_smallint(p1))
		return throw_error(q, p1, p1_ctx, "domain_error", "stream_position");

	off_t pos = get_smallint(p1);

	if (fseeko(str->fp_out, pos, SEEK_SET))
		return throw_error(q, p1, p1_ctx, "domain_error", "position");

	str->ungetch = 0;
	str->invalid_pending = false;
	return true;
}

static bool bif_sys_read_term_from_chars_4(query *q)
{
	CHECKED(check_frame(q, MAX_VARS));
	GET_FIRST_ARG(p_term,any);
	GET_NEXT_ARG(p_opts,list_or_nil);
	GET_NEXT_ARG(p_chars,any);
	GET_NEXT_ARG(p_rest,any);
	stream tmps;
	memset(&tmps, 0, sizeof(stream));
	stream *str = &tmps;
	char *src = NULL;
	bool has_var, is_partial;
	size_t srclen;

	if (is_atom(p_chars) && !is_string(p_chars)) {
		if (!strcmp(C_STR(q, p_chars), "[]")) {
			cell tmp;
			make_atom(&tmp, g_eof_s);
			return unify(q, p_term, p_term_ctx, &tmp, q->st.cur_ctx);
		} else
			return throw_error(q, p_chars, p_chars_ctx, "type_error", "character");
	} else if (is_string(p_chars)) {
		src = C_STR(q, p_chars);
		srclen = C_STRLEN(q, p_chars);
	} else if (!check_list(q, p_chars, p_chars_ctx, &is_partial, NULL)) {
		return throw_error(q, p_chars, p_chars_ctx, "type_error", "list");
	} else if ((srclen = scan_is_chars_list2(q, p_chars, p_chars_ctx, false, &has_var, &is_partial, NULL)) > 0) {
		if (!srclen)
			return throw_error(q, p_chars, p_chars_ctx, "type_error", "character");

		src = chars_list_to_string(q, p_chars, p_chars_ctx);
	} else {
		if (has_var)
			return throw_error(q, p_chars, p_chars_ctx, "instantiation_error", "var");

		return throw_error(q, p_chars, p_chars_ctx, "type_error", "character");
	}

	str->p = parser_create(q->st.m);

	if (!str->p) {
		if (!is_string(p_chars))
			TPL_free(src);

		return throw_error(q, q->st.instr, q->st.cur_ctx, "resource_error", "memory");
	}

	str->p->flags = q->st.m->flags;
	str->p->fp = str->fp;
	parser_reset(str->p);
	str->p->srcptr = src;

	if (!src || !*src) {
		parser_destroy(str->p);
		cell tmp;
		make_atom(&tmp, g_eof_s);
		return unify(q, p_term, p_term_ctx, &tmp, q->st.cur_ctx);
	}

	bool ok = do_read_term(q, str, p_term, p_term_ctx, p_opts, p_opts_ctx, NULL);

	if (ok != true) {
		if (!is_string(p_chars))
			TPL_free(src);

		parser_destroy(str->p);
		str->p = NULL;
		return false;
	}

	errno = 0;	// FIX: reset before parse read so a stale EINTR isn't misread
	char *rest = str->p->srcptr = eat_space(str->p);

	if (errno == EINTR) {
		clearerr(str->fp_in);
		return throw_timeout(q);
	}

	if (str->p->error) {
		// Note: grab these before the parser (and its strings) go away...
		const char *error_type = str->p->error_type?str->p->error_type:"syntax_error";
		const char *error_desc = str->p->error_desc?str->p->error_desc:"read_term";
		parser_destroy(str->p);
		str->p = NULL;
		return throw_error(q, q->st.instr, q->st.cur_ctx, error_type, error_desc);
	}

	cell tmp;

	if (*rest) {
		const char *ptr = strstr(src, rest);
		size_t off = ptr - src;
		size_t len = srclen - off;

		if (!is_string(p_chars))
			make_string(&tmp, rest);
		else
			make_slice(q, &tmp, p_chars, off, len);
	} else {
		make_atom(&tmp, g_nil_s);
	}

	parser_destroy(str->p);

	if (!is_string(p_chars))
		TPL_free(src);

	unify(q, p_rest, p_rest_ctx, &tmp, q->st.cur_ctx);
	unshare_cell(&tmp);
	return ok;
}

static bool bif_read_term_from_chars_3(query *q)
{
	CHECKED(check_frame(q, MAX_VARS));
	GET_FIRST_ARG(p_chars,any);
	GET_NEXT_ARG(p_term,any);
	GET_NEXT_ARG(p_opts,list_or_nil);
	stream tmps;
	memset(&tmps, 0, sizeof(stream));
	stream *str = &tmps;
	char *src = NULL;
	size_t len;
	bool has_var, is_partial;

	if (is_atom(p_chars) && !is_string(p_chars)) {
		if (!strcmp(C_STR(q, p_chars), "[]")) {
			cell tmp;
			make_atom(&tmp, g_eof_s);
			return unify(q, p_term, p_term_ctx, &tmp, q->st.cur_ctx);
		} else
			return throw_error(q, p_chars, p_chars_ctx, "type_error", "character");
	} else if (is_string(p_chars)) {
		len = C_STRLEN(q, p_chars);
		src = TPL_malloc(len+1+1);		// +1 is to allow adding a '.'
		CHECKED(src);
		memcpy(src, C_STR(q, p_chars), len);
		src[len] = '\0';
	} else if (!check_list(q, p_chars, p_chars_ctx, &is_partial, NULL)) {
		return throw_error(q, p_chars, p_chars_ctx, "type_error", "list");
	} else if ((len = scan_is_chars_list2(q, p_chars, p_chars_ctx, false, &has_var, &is_partial, NULL)) > 0) {
		if (!len)
			return throw_error(q, p_chars, p_chars_ctx, "type_error", "character");

		src = chars_list_to_string(q, p_chars, p_chars_ctx);
	} else {
		if (has_var)
			return throw_error(q, p_chars, p_chars_ctx, "instantiation_error", "var");

		return throw_error(q, p_chars, p_chars_ctx, "type_error", "character");
	}

	str->p = parser_create(q->st.m);

	if (!str->p) {
		TPL_free(src);
		return throw_error(q, q->st.instr, q->st.cur_ctx, "resource_error", "memory");
	}

	str->p->flags = q->st.m->flags;
	str->p->fp = str->fp;
	parser_reset(str->p);
	char *save_src = src;
	str->p->srcptr = src;

	if (!src || !*src) {
		parser_destroy(str->p);
		TPL_free(save_src);
		cell tmp;
		make_atom(&tmp, g_eof_s);
		return unify(q, p_term, p_term_ctx, &tmp, q->st.cur_ctx);
	}

	const char *end_ptr = src + strlen(src) - 1;

	while (isspace((unsigned char)*end_ptr) && (end_ptr != src))
		end_ptr--;

	if (src[strlen(src)-1] != '.')
		strcat(src, ".");

	bool ok = do_read_term(q, str, p_term, p_term_ctx, p_opts, p_opts_ctx, NULL);
	parser_reset(str->p);
	parser_destroy(str->p);
	TPL_free(save_src);

	if (ok != true)
		return false;

	return ok;
}

static bool bif_read_term_from_atom_3(query *q)
{
	CHECKED(check_frame(q, MAX_VARS));
	GET_FIRST_ARG(p_chars,any);
	GET_NEXT_ARG(p_term,any);
	GET_NEXT_ARG(p_opts,list_or_nil);
	stream tmps;
	memset(&tmps, 0, sizeof(stream));
	stream *str = &tmps;
	char *src;
	size_t len;

	if (is_atom(p_chars)) {
		len = C_STRLEN(q, p_chars);
		src = TPL_malloc(len+1+1);	// final +1 is for look-ahead
		CHECKED(src);
		memcpy(src, C_STR(q, p_chars), len);
		src[len] = '\0';
	} else if ((len = scan_is_chars_list(q, p_chars, p_chars_ctx, false)) > 0) {
		if (!len)
			return throw_error(q, p_chars, p_chars_ctx, "type_error", "atom");

		src = chars_list_to_string(q, p_chars, p_chars_ctx);
	} else
		return throw_error(q, p_chars, p_chars_ctx, "type_error", "atom");

	if (!src || !*src) {	// FIX: guard empty input like sibling bif_read_term_from_chars_3
		TPL_free(src);
		cell tmp;
		make_atom(&tmp, g_eof_s);
		return unify(q, p_term, p_term_ctx, &tmp, q->st.cur_ctx);
	}

	const char *end_ptr = src + strlen(src) - 1;

	while (isspace((unsigned char)*end_ptr) && (end_ptr != src))
		end_ptr--;

	if (src[strlen(src)-1] != '.')
		strcat(src, ".");

	bool ok = do_read_term(q, str, p_term, p_term_ctx, p_opts, p_opts_ctx, src);
	parser_destroy(str->p);
	TPL_free(src);
	return ok;
}

static bool bif_write_term_to_atom_3(query *q)
{
	GET_FIRST_ARG(p_chars,atom_or_var);
	GET_NEXT_ARG(p_term,any);
	GET_NEXT_ARG(p2,list_or_nil);
	cell *vnames = NULL;
	pl_ctx vnames_ctx = 0;
	q->flags = q->st.m->flags;
	PROLOG_LIST_HANDLER(p2);

	while (is_iso_list(p2)) {
		cell *h = PROLOG_LIST_HEAD(p2);
		h = deref(q, h, p2_ctx);
		pl_ctx h_ctx = q->latest_ctx;
		parse_write_params(q, h, h_ctx, &vnames, &vnames_ctx);
		p2 = PROLOG_LIST_TAIL(p2);
		p2 = deref(q, p2, p2_ctx);
		p2_ctx = q->latest_ctx;
	}

	q->variable_names = vnames;
	q->variable_names_ctx = vnames_ctx;
	char *dst = print_term_to_strbuf(q, p_term, p_term_ctx, 1);
	clear_write_options(q);
	cell tmp;
	make_cstring(&tmp, dst);
	TPL_free(dst);
	bool ok = unify(q, p_chars, p_chars_ctx, &tmp, q->st.cur_ctx);
	unshare_cell(&tmp);
	return ok;
}

static bool bif_write_term_to_chars_3(query *q)
{
	GET_FIRST_ARG(p_term,any);
	GET_NEXT_ARG(p2,list_or_nil);
	GET_NEXT_ARG(p_chars,atom_or_var);
	cell *vnames = NULL;
	pl_ctx vnames_ctx = 0;
	q->flags = q->st.m->flags;
	PROLOG_LIST_HANDLER(p2);

	while (is_iso_list(p2)) {
		cell *h = PROLOG_LIST_HEAD(p2);
		h = deref(q, h, p2_ctx);
		pl_ctx h_ctx = q->latest_ctx;
		parse_write_params(q, h, h_ctx, &vnames, &vnames_ctx);
		p2 = PROLOG_LIST_TAIL(p2);
		p2 = deref(q, p2, p2_ctx);
		p2_ctx = q->latest_ctx;
	}

	q->variable_names = vnames;
	q->variable_names_ctx = vnames_ctx;
	char *dst = print_term_to_strbuf(q, p_term, p_term_ctx, 1);
	clear_write_options(q);
	cell tmp;
	make_string(&tmp, dst);
	TPL_free(dst);
	bool ok = unify(q, p_chars, p_chars_ctx, &tmp, q->st.cur_ctx);
	unshare_cell(&tmp);
	return ok;
}

static bool bif_write_canonical_to_chars_3(query *q)
{
	GET_FIRST_ARG(p_chars,atom_or_var);
	GET_NEXT_ARG(p_term,any);
	GET_NEXT_ARG(p2,list_or_nil);
	cell *vnames = NULL;
	pl_ctx vnames_ctx = 0;
	q->flags = q->st.m->flags;
	PROLOG_LIST_HANDLER(p2);

	while (is_list(p2)) {
		cell *h = PROLOG_LIST_HEAD(p2);
		h = deref(q, h, p2_ctx);
		pl_ctx h_ctx = q->latest_ctx;
		parse_write_params(q, h, h_ctx, &vnames, &vnames_ctx);
		p2 = PROLOG_LIST_TAIL(p2);
		p2 = deref(q, p2, p2_ctx);
		p2_ctx = q->latest_ctx;
	}

	char *dst = print_canonical_to_strbuf(q, p_term, p_term_ctx, 1);
	clear_write_options(q);
	cell tmp;
	make_string(&tmp, dst);
	TPL_free(dst);
	bool ok = unify(q, p_chars, p_chars_ctx, &tmp, q->st.cur_ctx);
	unshare_cell(&tmp);
	return ok;
}

static bool bif_edin_redo_1(query *q)
{
	GET_FIRST_ARG(p1,integer);
	int n = q->pl->current_input;
	stream *str = &q->pl->streams[n];

	if (is_bigint(p1))
		return throw_error(q, p1, p1_ctx, "domain_error", "small_integer_range");

	if (isatty(fileno(str->fp)) && !str->did_getc && !str->ungetch) {
		fprintf(str->fp_out, "%s", PROMPT);
		fflush(str->fp_out);
	}

	for (;;) {
		str->did_getc = true;
		struct retry_ctx_ ctx = { q, str };
		int ch = str->ungetch ? str->ungetch : xgetc_utf8_lax(retry_getc, &ctx);
		str->ungetch = 0;

		if (errno == EINTR) {
			clearerr(str->fp_in);
			return throw_timeout(q);
		}

		if (feof(str->fp)) {
			str->did_getc = false;
			break;
		} else if (ch == '\n')
			str->did_getc = false;

		if (ch == get_smallint(p1))
			break;
	}

	return true;
}

static bool bif_edin_redo_2(query *q)
{
	GET_FIRST_ARG(pstr,stream_or_alias);
	int n = get_stream(q, pstr);
	stream *str = &q->pl->streams[n];
	GET_NEXT_ARG(p1,integer);

	if (is_bigint(p1))
		return throw_error(q, p1, p1_ctx, "domain_error", "small_integer_range");

	if (isatty(fileno(str->fp)) && !str->did_getc && !str->ungetch) {
		fprintf(str->fp_out, "%s", PROMPT);
		fflush(str->fp_out);
	}

	for (;;) {
		str->did_getc = true;
		struct retry_ctx_ ctx = { q, str };
		int ch = str->ungetch ? str->ungetch : xgetc_utf8_lax(retry_getc, &ctx);
		str->ungetch = 0;

		if (errno == EINTR) {
			clearerr(str->fp_in);
			return throw_timeout(q);
		}

		if (feof(str->fp)) {
			str->did_getc = false;
			break;
		} else if (ch == '\n')
			str->did_getc = false;

		if (ch == get_smallint(p1))
			break;
	}

	return true;
}

static bool bif_edin_tab_1(query *q)
{
	GET_FIRST_ARG(p1_tmp,any);
	cell p1 = eval(q, p1_tmp);

	if (!is_integer(&p1))
		return throw_error(q, &p1, p1_tmp_ctx, "type_error", "integer");

	int n = q->pl->current_output;
	stream *str = &q->pl->streams[n];

	if (resuming_write(q, str))
		return write_all(q, str, NULL, 0);

	int cnt = get_smallint(&p1);

	if (cnt <= 0)
		return true;

	char *buf = TPL_malloc(cnt);
	CHECKED(buf);
	memset(buf, ' ', cnt);
	bool ok = write_all(q, str, buf, cnt);
	TPL_free(buf);
	return ok ? flush_or_throw(q, str) : false;
}

static bool bif_edin_tab_2(query *q)
{
	GET_FIRST_ARG(pstr,stream_or_alias);
	GET_NEXT_ARG(p1_tmp,any);
	cell p1 = eval(q, p1_tmp);

	if (!is_integer(&p1))
		return throw_error(q, &p1, p1_tmp_ctx, "type_error", "integer");

	int n = get_stream(q, pstr);
	stream *str = &q->pl->streams[n];

	if (resuming_write(q, str))
		return write_all(q, str, NULL, 0);

	int cnt = get_smallint(&p1);

	if (cnt <= 0)
		return true;

	char *buf = TPL_malloc(cnt);
	CHECKED(buf);
	memset(buf, ' ', cnt);
	bool ok = write_all(q, str, buf, cnt);
	TPL_free(buf);
	return ok ? flush_or_throw(q, str) : false;
}

static bool bif_edin_seen_0(query *q)
{
	int n = q->pl->current_input;
	stream *str = &q->pl->streams[n];

	if (n < 3)
		return true;

	if ((str->fp != stdin)
		&& (str->fp != stdout)
		&& (str->fp != stderr)) {
		fclose(str->fp_in);

		if (str->fp_out != str->fp_in)
			fclose(str->fp_out);
	}

	sl_destroy(str->alias);
	TPL_free(str->filename);
	TPL_free(str->mode);
	memset(str, 0, sizeof(stream));
	q->pl->current_input = 0;
	return true;
}

static bool bif_edin_told_0(query *q)
{
	int n = q->pl->current_output;
	stream *str = &q->pl->streams[n];

	if (n <= 2)
		return true;

	if ((str->fp != stdin)
		&& (str->fp != stdout)
		&& (str->fp != stderr)) {
		fclose(str->fp_in);

		if (str->fp_out != str->fp_in)
			fclose(str->fp_out);
	}

	sl_destroy(str->alias);
	TPL_free(str->filename);
	TPL_free(str->mode);
	memset(str, 0, sizeof(stream));
	q->pl->current_output = 0;
	return true;
}

static bool bif_edin_seeing_1(query *q)
{
	GET_FIRST_ARG(p1,var);
	sliter *iter = sl_first(q->pl->streams[q->pl->current_input].alias);
	sl_next(iter, NULL);
	const char *alias = sl_key(iter);
	sl_done(iter);
	const char *name = q->pl->current_input==0?"user":alias;
	cell tmp;
	make_cstring(&tmp, name);
	bool ok = unify(q, p1, p1_ctx, &tmp, q->st.cur_ctx);
	unshare_cell(&tmp);
	return ok;
}

static bool bif_edin_telling_1(query *q)
{
	GET_FIRST_ARG(p1,var);
	sliter *iter = sl_first(q->pl->streams[q->pl->current_output].alias);
	sl_next(iter, NULL);
	const char *alias = sl_key(iter);
	sl_done(iter);
	const char *name =q->pl->current_output==1?"user":alias;
	cell tmp;
	make_cstring(&tmp, name);
	bool ok = unify(q, p1, p1_ctx, &tmp, q->st.cur_ctx);
	unshare_cell(&tmp);
	return ok;
}

static bool bif_read_line_to_string_2(query *q)
{
	GET_FIRST_ARG(pstr,stream_or_alias);
	GET_NEXT_ARG(p1,any);
	int n = get_stream(q, pstr);
	stream *str = &q->pl->streams[n];
	char *line = NULL;
	size_t len = 0;

	if (isatty(fileno(str->fp))) {
		fprintf(str->fp_out, "%s", PROMPT);
		fflush(str->fp_out);
	}

	if (tpl_getline(&line, &len, q, str) == -1) {
		TPL_free(line);

		if (q->is_task && !feof(str->fp) && ferror(str->fp)) {
			clearerr(str->fp_in);
			return do_yield_on_stream(q, str, false);
		}

		if (errno == EINTR) {
			clearerr(str->fp_in);
			return throw_timeout(q);
		}

		cell tmp;
		make_atom(&tmp, g_eof_s);
		return unify(q, p1, p1_ctx, &tmp, q->st.cur_ctx);
	}


	if (errno == EINTR) {
		clearerr(str->fp_in);
		return throw_timeout(q);
	}

	len = strlen(line);

	if (len && (line[len-1] == '\n')) {
		line[len-1] = '\0';
		len--;
	}

	if (len && (line[len-1] == '\r')) {
		line[len-1] = '\0';
		len--;
	}

	cell tmp;
	make_string(&tmp, line);
	TPL_free(line);
	bool ok = unify(q, p1, p1_ctx, &tmp, q->st.cur_ctx);
	unshare_cell(&tmp);
	return ok;
}

static bool bif_read_line_to_codes_2(query *q)
{
	GET_FIRST_ARG(pstr,stream_or_alias);
	GET_NEXT_ARG(p1,any);
	int n = get_stream(q, pstr);
	stream *str = &q->pl->streams[n];
	char *line = NULL;
	size_t len = 0;

	if (isatty(fileno(str->fp))) {
		fprintf(str->fp_out, "%s", PROMPT);
		fflush(str->fp_out);
	}

	if (tpl_getline(&line, &len, q, str) == -1) {
		TPL_free(line);

		if (q->is_task && !feof(str->fp) && ferror(str->fp)) {
			clearerr(str->fp_in);
			return do_yield_on_stream(q, str, false);
		}

		if (errno == EINTR) {
			clearerr(str->fp_in);
			return throw_timeout(q);
		}

		cell tmp;
		make_atom(&tmp, g_eof_s);
		return unify(q, p1, p1_ctx, &tmp, q->st.cur_ctx);
	}


	if (errno == EINTR) {
		clearerr(str->fp_in);
		return throw_timeout(q);
	}

	len = strlen(line);

	if (len && (line[len-1] == '\n')) {
		line[len-1] = '\0';
		len--;
	}

	if (len && (line[len-1] == '\r')) {
		line[len-1] = '\0';
		len--;
	}

	cell tmp;
	make_string(&tmp, line);
	tmp.flags |= FLAG_CSTR_CODES;
	TPL_free(line);
	bool ok = unify(q, p1, p1_ctx, &tmp, q->st.cur_ctx);
	unshare_cell(&tmp);
	return ok;
}

static bool bif_read_file_to_string_3(query *q)
{
	GET_FIRST_ARG(p1,source_sink);
	GET_NEXT_ARG(p2,var);
	GET_NEXT_ARG(p3,list_or_nil);
	char *filename;
	char *src = NULL;

	if (is_iso_list(p1)) {
		size_t len = scan_is_chars_list(q, p1, p1_ctx, true);

		if (!len)
			return throw_error(q, p1, p1_ctx, "type_error", "atom");

		src = chars_list_to_string(q, p1, p1_ctx);
		filename = src;
	} else
		filename = src = DUP_STRING(q, p1);

	bool bom_specified = false, use_bom = false, is_binary = false;
	PROLOG_LIST_HANDLER(p3);

	while (is_list(p3)) {
		cell *h = PROLOG_LIST_HEAD(p3);
		cell *c = deref(q, h, p3_ctx);

		if (is_var(c))
			return throw_error(q, c, q->latest_ctx, "instantiation_error", "args_not_sufficiently_instantiated");

		if (is_compound(c) && (get_arity(c) == 1)) {
			cell *name = c + 1;
			name = deref(q, name, q->latest_ctx);

			if (!CMP_STRING_TO_CSTR(q, c, "type")) {
				if (is_atom(name) && !CMP_STRING_TO_CSTR(q, name, "binary")) {
					is_binary = true;
				} else if (is_atom(name) && !CMP_STRING_TO_CSTR(q, name, "text"))
					is_binary = false;
				else
					return throw_error(q, c, q->latest_ctx, "domain_error", "stream_option");
			} else if (!CMP_STRING_TO_CSTR(q, c, "bom")) {
				bom_specified = true;

				if (is_atom(name) && !CMP_STRING_TO_CSTR(q, name, "true"))
					use_bom = true;
				else if (is_atom(name) && !CMP_STRING_TO_CSTR(q, name, "false"))
					use_bom = false;
			}
		} else
			return throw_error(q, c, q->latest_ctx, "domain_error", "stream_option");

		p3 = PROLOG_LIST_TAIL(p3);
		p3 = deref(q, p3, p3_ctx);
		p3_ctx = q->latest_ctx;

		if (is_var(p3))
			return throw_error(q, p3, p3_ctx, "instantiation_error", "args_not_sufficiently_instantiated");
	}

	FILE *fp = fopen(filename, is_binary?"rb":"r");
	TPL_free(src);

	if (!fp)
		return throw_error(q, p1, p1_ctx, "existence_error", "cannot_open_file");

	// Check for a BOM

	size_t offset = 0;

	if (!is_binary && (!bom_specified || use_bom)) {
		int ch = getc_utf8(fp);

		if ((unsigned)ch != 0xFEFF)
			fseek(fp, 0, SEEK_SET);
		else
			offset = 3;
	}

	struct stat st = {0};

	if (fstat(fileno(fp), &st)) {
		fclose(fp);	// FIX: don't leak fp on fstat failure
		return false;
	}

	size_t len = st.st_size - offset;
	char *s = TPL_malloc(len+1);
	CHECKED(s, fclose(fp));

	if (fread(s, 1, len, fp) != (size_t)len) {
		TPL_free(s);
		fclose(fp);
		return throw_error(q, p1, p1_ctx, "domain_error", "cannot_read");
	}

	s[len] = '\0';	// FIX: buffer is (st_size-offset)+1; s[st.st_size] overruns by 'offset' bytes when a BOM is present
	fclose(fp);
	cell tmp;
	make_stringn(&tmp, s, len);
	bool ok = unify(q, p2, p2_ctx, &tmp, q->st.cur_ctx);
	unshare_cell(&tmp);
	TPL_free(s);
	return ok;
}

bool do_load_file(query *q, cell *p1, pl_ctx p1_ctx)
{
	if (is_atom(p1)) {
		char *src = DUP_STRING(q, p1);
		char *filename = relative_to(q->st.m->filename, src);
		unload_file(q->st.m, filename);
		TPL_free(src);

		if (!load_file(q->st.m, filename, false, true)) {
			TPL_free(filename);
			return throw_error(q, p1, p1_ctx, "existence_error", "source_sink");
		}

		TPL_free(filename);
		return true;
	}

	if (!is_compound(p1))
		return throw_error(q, p1, p1_ctx, "type_error", "atom");

	if (CMP_STRING_TO_CSTR(q, p1, ":"))
		return throw_error(q, p1, p1_ctx, "type_error", "atom");

	cell *mod = deref(q, p1+1, p1_ctx);
	cell *file = deref(q, p1+2, p1_ctx);

	if (!is_atom(mod) || !is_atom(file))
		return throw_error(q, p1, p1_ctx, "type_error", "atom");

	module *tmp_m = module_create(q->pl, C_STR(q, mod));
	CHECKED(tmp_m);
	char *src = DUP_STRING(q, file);
	char *filename = relative_to(q->st.m->filename, src);
	TPL_free(src);
	unload_file(tmp_m, filename);

	if (!load_file(tmp_m, filename, false, true)) {
		module_destroy(tmp_m);
		TPL_free(filename);
		return throw_error(q, p1, p1_ctx, "existence_error", "source_sink");
	}

	TPL_free(filename);
	return true;
}

static bool do_unload_file(query *q, cell *p1, pl_ctx p1_ctx)
{
	if (is_atom(p1)) {
		char *src = DUP_STRING(q, p1);
		char *filename = relative_to(q->st.m->filename, src);
		TPL_free(src);
		unload_file(q->st.m, filename);
		TPL_free(filename);
		return true;
	}

	if (!is_compound(p1))
		return throw_error(q, p1, p1_ctx, "type_error", "source_sink");

	if (CMP_STRING_TO_CSTR(q, p1, ":"))
		return throw_error(q, p1, p1_ctx, "type_error", "source_sink");

	cell *mod = deref(q, p1+1, p1_ctx);
	cell *file = deref(q, p1+2, p1_ctx);

	if (!is_atom(mod) || !is_atom(file))
		return throw_error(q, p1, p1_ctx, "type_error", "source_sink");

	char *src = DUP_STRING(q, file);
	char *filename = relative_to(q->st.m->filename, src);
	TPL_free(src);
	unload_file(q->st.m, filename);
	TPL_free(filename);
	return true;
}

static bool bif_load_files_2(query *q)
{
	GET_FIRST_ARG(p1,source_sink);
	GET_NEXT_ARG(p2,list_or_nil);

	if (is_atom(p1))
		return do_load_file(q, p1, p1_ctx);

	PROLOG_LIST_HANDLER(p1);

	while (is_list(p1)) {
		cell *h = PROLOG_LIST_HEAD(p1);
		cell *c = deref(q, h, p1_ctx);
		pl_ctx c_ctx = q->latest_ctx;

		if (!do_load_file(q, c, c_ctx))
			return false;

		p1 = PROLOG_LIST_TAIL(p1);
		p1 = deref(q, p1, p1_ctx);
		p1_ctx = q->latest_ctx;
	}

	return true;
}

static bool bif_unload_files_1(query *q)
{
	GET_FIRST_ARG(p1,source_sink);

	if (is_atom(p1))
		return do_unload_file(q, p1, p1_ctx);

	PROLOG_LIST_HANDLER(p1);

	while (is_list(p1)) {
		cell *h = PROLOG_LIST_HEAD(p1);
		cell *c = deref(q, h, p1_ctx);
		pl_ctx c_ctx = q->latest_ctx;

		if (!do_unload_file(q, c, c_ctx))
			return false;

		p1 = PROLOG_LIST_TAIL(p1);
		p1 = deref(q, p1, p1_ctx);
		p1_ctx = q->latest_ctx;
	}

	return true;
}

static bool bif_make_0(query *q)
{
	for (module *m = list_front(&q->pl->modules);
		m; m = list_next(m)) {
		make(m);
	}

	return true;
}

static bool bif_savefile_2(query *q)
{
	GET_FIRST_ARG(p1,source_sink);
	GET_NEXT_ARG(p2,atom);
	char *filename;

	if (is_iso_list(p1)) {
		size_t len = scan_is_chars_list(q, p1, p1_ctx, true);

		if (!len)
			return throw_error(q, p1, p1_ctx, "type_error", "atom");

		filename = chars_list_to_string(q, p1, p1_ctx);
	} else
		filename = DUP_STRING(q, p1);

	FILE *fp = fopen(filename, "wb");
	CHECKED(fp, TPL_free(filename));	// FIX: free filename if open fails
	fwrite(C_STR(q, p2), 1, C_STRLEN(q, p2), fp);
	fclose(fp);
	TPL_free(filename);
	return true;
}

static bool bif_loadfile_2(query *q)
{
	GET_FIRST_ARG(p1,source_sink);
	GET_NEXT_ARG(p2,var);
	char *filename;

	if (is_iso_list(p1)) {
		size_t len = scan_is_chars_list(q, p1, p1_ctx, true);

		if (!len)
			return throw_error(q, p1, p1_ctx, "type_error", "atom");

		filename = chars_list_to_string(q, p1, p1_ctx);
	} else
		filename = DUP_STRING(q, p1);

	FILE *fp = fopen(filename, "rb");
	TPL_free(filename);

	if (!fp)
		return throw_error(q, p1, p1_ctx, "existence_error", "cannot_open_file");

	// Check for a BOM

	int ch = getc_utf8(fp), offset = 0;

	if ((unsigned)ch != 0xFEFF)
		fseek(fp, 0, SEEK_SET);
	else
		offset = 3;

	struct stat st = {0};

	if (fstat(fileno(fp), &st)) {
		fclose(fp);	// FIX: don't leak fp on fstat failure
		return false;
	}

	size_t len = st.st_size - offset;
	char *s = TPL_malloc(len+1);
	CHECKED(s, fclose(fp));

	if (fread(s, 1, len, fp) != (size_t)len) {
		TPL_free(s);
		fclose(fp);
		return throw_error(q, p1, p1_ctx, "domain_error", "cannot_read");
	}

	s[len] = '\0';	// FIX: buffer is (st_size-offset)+1; s[st.st_size] overruns by 'offset' bytes when a BOM is present
	fclose(fp);
	cell tmp;

	if (!len)
		make_atom(&tmp, g_nil_s);
	else
		make_stringn(&tmp, s, len);

	bool ok = unify(q, p2, p2_ctx, &tmp, q->st.cur_ctx);
	unshare_cell(&tmp);
	TPL_free(s);
	return ok;
}

static bool bif_getfile_2(query *q)
{
	GET_FIRST_ARG(p1,source_sink);
	GET_NEXT_ARG(p2,var);
	char *filename;

	if (is_iso_list(p1)) {
		size_t len = scan_is_chars_list(q, p1, p1_ctx, true);

		if (!len)
			return throw_error(q, p1, p1_ctx, "type_error", "atom");

		filename = chars_list_to_string(q, p1, p1_ctx);
	} else
		filename = DUP_STRING(q, p1);

	FILE *fp = fopen(filename, "r");
	TPL_free(filename);

	if (!fp)
		return throw_error(q, p1, p1_ctx, "existence_error", "cannot_open_file");

	// Check for a BOM

	int ch = getc_utf8(fp);

	if ((unsigned)ch != 0xFEFF)
		fseek(fp, 0, SEEK_SET);

	char *line = NULL;
	size_t len = 0;
	CHECKED(init_tmp_heap(q));

	while (tpl_getline_fp(&line, &len, fp) != -1) {
		int len = strlen(line);

		if (len && (line[len-1] == '\n')) {
			line[len-1] = '\0';
			len--;
		}

		if (len && (line[len-1] == '\r')) {
			line[len-1] = '\0';
			len--;
		}

		cell tmp;
		make_stringn(&tmp, line, len);
		append_list(q, &tmp);
	}

	TPL_free(line);
	fclose(fp);
	cell *l = end_list(q);
	CHECKED(l);
	unify(q, p2, p2_ctx, l, q->st.cur_ctx);
	return true;
}

static bool get_terminator(query *q, cell *l, pl_ctx l_ctx)
{
	bool terminator = false;
	PROLOG_LIST_HANDLER(l);

	while (is_iso_list(l)) {
		cell *h = PROLOG_LIST_HEAD(l);
		h = deref(q, h, l_ctx);
		pl_ctx h_ctx = q->latest_ctx;

		if (is_compound(h)) {
			if (!CMP_STRING_TO_CSTR(q, h, "terminator")) {
				h = h + 1;
				h = deref(q, h, h_ctx);

				if (is_atom(h))
					terminator = !CMP_STRING_TO_CSTR(q, h, "true");
			}
		}

		l = PROLOG_LIST_TAIL(l);
		l = deref(q, l, l_ctx);
		l_ctx = q->latest_ctx;
	}

	return terminator;
}

static bool get_empty(query *q, cell *l, pl_ctx l_ctx)
{
	bool empty = false;
	PROLOG_LIST_HANDLER(l);

	while (is_iso_list(l)) {
		cell *h = PROLOG_LIST_HEAD(l);
		h = deref(q, h, l_ctx);
		pl_ctx h_ctx = q->latest_ctx;

		if (is_compound(h)) {
			if (!CMP_STRING_TO_CSTR(q, h, "empty")) {
				h = h + 1;
				h = deref(q, h, h_ctx);

				if (is_atom(h))
					empty = !CMP_STRING_TO_CSTR(q, h, "true");
			}
		}

		l = PROLOG_LIST_TAIL(l);
		l = deref(q, l, l_ctx);
		l_ctx = q->latest_ctx;
	}

	return empty;
}

static bool bif_getfile_3(query *q)
{
	GET_FIRST_ARG(p1,source_sink);
	GET_NEXT_ARG(p2,var);
	GET_NEXT_ARG(p3,list_or_nil);
	char *filename;
	bool terminator = get_terminator(q, p3, p3_ctx);

	if (is_iso_list(p1)) {
		size_t len = scan_is_chars_list(q, p1, p1_ctx, true);

		if (!len)
			return throw_error(q, p1, p1_ctx, "type_error", "atom");

		filename = chars_list_to_string(q, p1, p1_ctx);
	} else
		filename = DUP_STRING(q, p1);

	FILE *fp = fopen(filename, "r");
	TPL_free(filename);

	if (!fp)
		return throw_error(q, p1, p1_ctx, "existence_error", "cannot_open_file");

	// Check for a BOM

	int ch = getc_utf8(fp);

	if ((unsigned)ch != 0xFEFF)
		fseek(fp, 0, SEEK_SET);

	char *line = NULL;
	size_t len = 0;
	CHECKED(init_tmp_heap(q));

	while (tpl_getline_fp(&line, &len, fp) != -1) {
		int len = strlen(line);

		if (!terminator) {
			if (len && (line[len-1] == '\n')) {
				line[len-1] = '\0';
				len--;
			}

			if (len && (line[len-1] == '\r')) {
				line[len-1] = '\0';
				len--;
			}
		}

		cell tmp;
		make_stringn(&tmp, line, len);
		append_list(q, &tmp);
	}

	TPL_free(line);
	fclose(fp);
	cell *l = end_list(q);
	CHECKED(l);
	unify(q, p2, p2_ctx, l, q->st.cur_ctx);
	return true;
}

static bool bif_getlines_1(query *q)
{
	GET_NEXT_ARG(p1,var);
	int n = q->pl->current_input;
	stream *str = &q->pl->streams[n];
	char *line = NULL;
	size_t len = 0;
	CHECKED(init_tmp_heap(q));

	while (tpl_getline_fp(&line, &len, str->fp) != -1) {
		int len = strlen(line);

		if (len && (line[len-1] == '\n')) {
			line[len-1] = '\0';
			len--;
		}

		if (len && (line[len-1] == '\r')) {
			line[len-1] = '\0';
			len--;
		}

		cell tmp;
		make_stringn(&tmp, line, len);
		append_list(q, &tmp);
	}

	TPL_free(line);
	cell *l = end_list(q);
	CHECKED(l);
	unify(q, p1, p1_ctx, l, q->st.cur_ctx);
	return true;
}

static bool bif_getlines_2(query *q)
{
	GET_FIRST_ARG(pstr,stream_or_alias);
	GET_NEXT_ARG(p1,var);
	int n = get_stream(q, pstr);
	stream *str = &q->pl->streams[n];
	char *line = NULL;
	size_t len = 0;
	CHECKED(init_tmp_heap(q));

	while (tpl_getline(&line, &len, q, str) != -1) {
		int len = strlen(line);

		if (len && (line[len-1] == '\n')) {
			line[len-1] = '\0';
			len--;
		}

		if (len && (line[len-1] == '\r')) {
			line[len-1] = '\0';
			len--;
		}

		cell tmp;
		make_stringn(&tmp, line, len);
		append_list(q, &tmp);
	}

	TPL_free(line);
	cell *l = end_list(q);
	CHECKED(l);
	unify(q, p1, p1_ctx, l, q->st.cur_ctx);
	return true;
}

static bool bif_getlines_3(query *q)
{
	GET_FIRST_ARG(pstr,stream_or_alias);
	GET_NEXT_ARG(p1,var);
	GET_NEXT_ARG(p2,list_or_nil);
	int n = get_stream(q, pstr);
	stream *str = &q->pl->streams[n];
	char *line = NULL;
	size_t len = 0;
	bool terminator = get_terminator(q, p2, p2_ctx);
	bool empty = get_empty(q, p2, p2_ctx);
	CHECKED(init_tmp_heap(q));

	while (tpl_getline(&line, &len, q, str) != -1) {
		int len = strlen(line);

		if (empty) {
			if (!len || (line[len-1] == '\n'))
				break;
		}

		if (!terminator) {
			if (len && (line[len-1] == '\n')) {
				line[len-1] = '\0';
				len--;
			}

			if (len && (line[len-1] == '\r')) {
				line[len-1] = '\0';
				len--;
			}
		}

		cell tmp;
		make_stringn(&tmp, line, len);
		append_list(q, &tmp);
	}

	TPL_free(line);
	cell *l = end_list(q);
	CHECKED(l);
	unify(q, p1, p1_ctx, l, q->st.cur_ctx);
	return true;
}

static char *fixup(const char *srcptr)
{
	char *tmpbuf = TPL_strdup(srcptr);
	const char *src = srcptr;
	char *dst = tmpbuf;

	while (*src) {
		if ((src[0] == '.') && (src[1] == '.') && ((src[2] == '/') || (src[2] == '\\'))) {
			dst -= 2;

			while ((dst != tmpbuf) && ((*dst != '/') && (*dst != '\\')
#ifdef _WIN32
				&& (*dst != ':')
#endif
				))
				dst--;

			src += 2;
			dst++;
		} else if ((src[0] == '.') && ((src[1] == '/') || (src[1] == '\\')
#ifdef _WIN32
				|| (src[1] == ':')
#endif
			))
			src += 1;
		else
			*dst++ = *src;

		src++;
	}

	*dst = '\0';
	return tmpbuf;
}

static bool bif_is_absolute_file_name_1(query *q)
{
	GET_FIRST_ARG(p1,atom);
	const char *filename = C_STR(q, p1);
	return *filename == '/';
}

static bool bif_absolute_file_name_3(query *q)
{
	GET_FIRST_ARG(p1,atom);
	GET_NEXT_ARG(p2,atom_or_var);
	GET_NEXT_ARG(p_opts,list_or_nil);
	bool expand = false;
	char *filename = NULL;
	char cwdbuf[1024*4];
	const char *cwd0 = getcwd(cwdbuf, sizeof(cwdbuf));	// FIX: getcwd may fail; don't TPL_strdup(NULL)
	if (!cwd0)
		return throw_error(q, p1, p1_ctx, "system_error", "getcwd");
	char *here = TPL_strdup(cwd0);
	CHECKED(here);
	char *cwd = here;

	if (is_iso_list(p1)) {
		size_t len = scan_is_chars_list(q, p1, p1_ctx, true);

		if (!len) {
			TPL_free(here);
			return throw_error(q, p1, p1_ctx, "type_error", "atom");
		}

		filename = chars_list_to_string(q, p1, p1_ctx);
	} else
		filename = DUP_STRING(q, p1);

	PROLOG_LIST_HANDLER(p_opts);

	while (is_list(p_opts)) {
		cell *h = PROLOG_LIST_HEAD(p_opts);
		h = deref(q, h, p_opts_ctx);

		if (is_compound(h) && (get_arity(h) == 1)) {
			if (!CMP_STRING_TO_CSTR(q, h, "expand")) {
				if (is_interned(h+1)) {
					if (!CMP_STRING_TO_CSTR(q, h+1, "true"))
						expand = true;
				}
			} else if (!CMP_STRING_TO_CSTR(q, h, "relative_to")) {
				if (is_atom(h+1))
					cwd = DUP_STRING(q, h+1);
			}
		}

		p_opts = PROLOG_LIST_TAIL(p_opts);
		p_opts = deref(q, p_opts, p_opts_ctx);
		p_opts_ctx = q->latest_ctx;
	}

	char *tmpbuf = NULL;
	const char *s = filename;

	if (expand && (*s == '$')) {
		char envbuf[PATH_MAX];
		char *dst = envbuf;
		s++;

		while (*s && (*s != '/') && (*s != '\\') && ((dst-envbuf-1) != sizeof(envbuf)))
			*dst++ = *s++;

		if ((*s == '\\') || (*s == '/'))
			s++;

		*dst = '\0';
		char *ptr = getenv(envbuf);

		if (!ptr) {
			TPL_free(here);
			TPL_free(filename);
			return throw_error(q, p1, p1_ctx, "existence_error", "environment_variable");
		}

		size_t buflen = strlen(ptr)+1+strlen(s)+1;
		tmpbuf = TPL_malloc(buflen);
		CHECKED(tmpbuf);
		snprintf(tmpbuf, buflen, "%s/%s", ptr, s);
		char *tmpbuf2;

		if ((tmpbuf2 = tpl_realpath(tmpbuf)) == NULL) {
		} else {
			TPL_free(tmpbuf);
			tmpbuf = tmpbuf2;
		}
	} else {
		if ((tmpbuf = tpl_realpath(s)) == NULL) {
			if ((tmpbuf = tpl_realpath(cwd)) == NULL)
				tmpbuf = tpl_realpath(".");

			CHECKED(tmpbuf);

			if ((*s != '/') && (*s != '\\')
#ifdef _WIN32
				&& (s[1] != ':')
#endif
				) {
				size_t buflen = strlen(tmpbuf)+1+strlen(s)+1;
				char *tmp = TPL_malloc(buflen);
				CHECKED(tmp, TPL_free(tmpbuf));
				snprintf(tmp, buflen, "%s/%s", tmpbuf, s);
				TPL_free(tmpbuf);
				tmpbuf = fixup(tmp);
				CHECKED(tmpbuf);
				TPL_free(tmp);
			} else {
				TPL_free(tmpbuf);
				tmpbuf = fixup(s);
				CHECKED(tmpbuf);
			}
		}
	}

	if (cwd != here)
		TPL_free(cwd);

	TPL_free(filename);
	TPL_free(here);
	cell tmp;

	if (is_string(p1))
		make_string(&tmp, tmpbuf);
	else
		make_cstring(&tmp, tmpbuf);

	TPL_free(tmpbuf);
	bool ok = unify(q, p2, p2_ctx, &tmp, q->st.cur_ctx);
	unshare_cell(&tmp);
	return ok;
}

static bool bif_getline_1(query *q)
{
	GET_FIRST_ARG(p1,any);
	int n = q->pl->current_input;
	stream *str = &q->pl->streams[n];
	char *line = NULL;
	size_t len = 0;

	if (isatty(fileno(str->fp))) {
		fprintf(str->fp_out, "%s", PROMPT);
		fflush(str->fp_out);
	}

	if (tpl_getline(&line, &len, q, str) == -1) {
		TPL_free(line);

		if (q->is_task && !feof(str->fp) && ferror(str->fp)) {
			clearerr(str->fp_in);
			return do_yield_on_stream(q, str, false);
		}

		if (errno == EINTR) {
			clearerr(str->fp_in);
			return throw_timeout(q);
		}

		return false;
	}

	if (errno == EINTR) {
		clearerr(str->fp_in);
		return throw_timeout(q);
	}

	len = strlen(line);

	if (len && (line[len-1] == '\n')) {
		line[len-1] = '\0';
		len--;
	}

	if (len && (line[len-1] == '\r')) {
		line[len-1] = '\0';
		len--;
	}

	cell tmp;
	make_string(&tmp, line);
	TPL_free(line);
	bool ok = unify(q, p1, p1_ctx, &tmp, q->st.cur_ctx);
	unshare_cell(&tmp);
	return ok;
}

static bool bif_getline_2(query *q)
{
	GET_FIRST_ARG(pstr,stream_or_alias);
	GET_NEXT_ARG(p1,any);
	int n = get_stream(q, pstr);
	stream *str = &q->pl->streams[n];
	char *line = NULL;
	size_t len = 0;

	if (isatty(fileno(str->fp))) {
		fprintf(str->fp_out, "%s", PROMPT);
		fflush(str->fp_out);
	}

	if (tpl_getline(&line, &len, q, str) == -1) {
		TPL_free(line);

		if (q->is_task && !feof(str->fp) && ferror(str->fp)) {
			clearerr(str->fp_in);
			return do_yield_on_stream(q, str, false);
		}

		return false;
	}

	if (errno == EINTR) {
		clearerr(str->fp_in);
		return throw_timeout(q);
	}

	len = strlen(line);

	if (line[strlen(line)-1] == '\n')
		line[strlen(line)-1] = '\0';

	if (line[strlen(line)-1] == '\r')
		line[strlen(line)-1] = '\0';

	cell tmp;
	make_string(&tmp, line);
	TPL_free(line);
	bool ok = unify(q, p1, p1_ctx, &tmp, q->st.cur_ctx);
	unshare_cell(&tmp);
	return ok;
}

static bool bif_getline_3(query *q)
{
	GET_FIRST_ARG(pstr,stream_or_alias);
	GET_NEXT_ARG(p1,any);
	GET_NEXT_ARG(p2,list_or_nil);
	int n = get_stream(q, pstr);
	stream *str = &q->pl->streams[n];
	char *line = NULL;
	size_t len = 0;
	bool terminator = get_terminator(q, p2, p2_ctx);

	if (isatty(fileno(str->fp))) {
		fprintf(str->fp_out, "%s", PROMPT);
		fflush(str->fp_out);
	}

	if (tpl_getline(&line, &len, q, str) == -1) {
		TPL_free(line);

		if (q->is_task && !feof(str->fp) && ferror(str->fp)) {
			clearerr(str->fp_in);
			return do_yield_on_stream(q, str, false);
		}

		return false;
	}

	if (errno == EINTR) {
		clearerr(str->fp_in);
		return throw_timeout(q);
	}

	len = strlen(line);

	if (!terminator) {
		if (line[strlen(line)-1] == '\n')
			line[strlen(line)-1] = '\0';

		if (line[strlen(line)-1] == '\r')
			line[strlen(line)-1] = '\0';
	}

	cell tmp;
	make_string(&tmp, line);
	TPL_free(line);
	bool ok = unify(q, p1, p1_ctx, &tmp, q->st.cur_ctx);
	unshare_cell(&tmp);
	return ok;
}

static bool bif_access_file_2(query *q)
{
	GET_FIRST_ARG(p1,source_sink);
	GET_NEXT_ARG(p2,atom);
	char *filename;

	if (is_iso_list(p1)) {
		size_t len = scan_is_chars_list(q, p1, p1_ctx, true);

		if (!len)
			return throw_error(q, p1, p1_ctx, "type_error", "atom");

		filename = chars_list_to_string(q, p1, p1_ctx);
	} else
		filename = DUP_STRING(q, p1);

	int amode = R_OK;

	if (!CMP_STRING_TO_CSTR(q, p2, "read"))
		amode = R_OK;
	else if (!CMP_STRING_TO_CSTR(q, p2, "write"))
		amode = W_OK;
	else if (!CMP_STRING_TO_CSTR(q, p2, "append"))
		amode = W_OK;
	else if (!CMP_STRING_TO_CSTR(q, p2, "execute"))
		amode = X_OK;
	else if (!CMP_STRING_TO_CSTR(q, p2, "none")) {
		TPL_free(filename);
		return true;
	} else {
		TPL_free(filename);
		return throw_error(q, p2, p2_ctx, "domain_error", "mode");
	}

	struct stat st = {0};
	int status = stat(filename, &st);

	if (status && (!CMP_STRING_TO_CSTR(q, p2, "read") || !CMP_STRING_TO_CSTR(q, p2, "exist") || !CMP_STRING_TO_CSTR(q, p2, "execute") || !CMP_STRING_TO_CSTR(q, p2, "none"))) {
		TPL_free(filename);
		return false;
	}

	if (status && (!CMP_STRING_TO_CSTR(q, p2, "write") || !CMP_STRING_TO_CSTR(q, p2, "append"))) {
		TPL_free(filename);
		return true;
	}

	int ok = !access(filename, amode);
	TPL_free(filename);
	return ok;
}

static bool bif_exists_file_1(query *q)
{
	GET_FIRST_ARG(p1,source_sink);
	char *filename;

	if (is_iso_list(p1)) {
		size_t len = scan_is_chars_list(q, p1, p1_ctx, true);

		if (!len)
			return throw_error(q, p1, p1_ctx, "type_error", "atom");

		filename = chars_list_to_string(q, p1, p1_ctx);
	} else
		filename = DUP_STRING(q, p1);

	struct stat st = {0};

	if (stat(filename, &st)) {
		TPL_free(filename);
		return false;
	}

	TPL_free(filename);

	if ((st.st_mode & S_IFMT) != S_IFREG)
		return false;

	return true;
}

#if TPL_FEATURE_FILESYSTEM
static bool bif_directory_files_2(query *q)
{
	GET_FIRST_ARG(p1,source_sink);
	GET_NEXT_ARG(p2,var);
	char *filename;

	if (is_iso_list(p1)) {
		size_t len = scan_is_chars_list(q, p1, p1_ctx, true);

		if (!len)
			return throw_error(q, p1, p1_ctx, "type_error", "atom");

		filename = chars_list_to_string(q, p1, p1_ctx);
	} else
		filename = DUP_STRING(q, p1);

	struct stat st = {0};

	if (stat(filename, &st)) {
		TPL_free(filename);
		return throw_error(q, p1, p1_ctx, "existence_error", "directory");
	}

	DIR *dirp = opendir(filename);

	if (!dirp) {
		TPL_free(filename);
		return throw_error(q, p1, p1_ctx, "existence_error", "directory");
	}

	struct dirent *dire = readdir(dirp);
	cell tmp;

	if (is_string(p1))
		make_string(&tmp, dire->d_name);
	else
		make_cstring(&tmp, dire->d_name);

	allocate_list(q, &tmp);

	for (dire = readdir(dirp); dire; dire = readdir(dirp)) {
		if (is_string(p1))
			make_string(&tmp, dire->d_name);
		else
			make_cstring(&tmp, dire->d_name);

		append_list(q, &tmp);
	}

	closedir(dirp);
	TPL_free(filename);
	cell *l = end_list(q);
	bool ok = unify(q, p2, p2_ctx, l, q->st.cur_ctx);
	return ok;
}
#endif

static bool bif_delete_file_1(query *q)
{
	GET_FIRST_ARG(p1,source_sink);
	char *filename;

	if (is_iso_list(p1)) {
		size_t len = scan_is_chars_list(q, p1, p1_ctx, true);

		if (!len)
			return throw_error(q, p1, p1_ctx, "type_error", "atom");

		filename = chars_list_to_string(q, p1, p1_ctx);
	} else
		filename = DUP_STRING(q, p1);

	struct stat st = {0};

	if (stat(filename, &st)) {
		TPL_free(filename);
		return throw_error(q, p1, p1_ctx, "existence_error", "file");
	}

	remove(filename);
	TPL_free(filename);
	return true;
}

static bool bif_rename_file_2(query *q)
{
	GET_FIRST_ARG(p1,source_sink);
	GET_NEXT_ARG(p2,source_sink);
	char *filename1, *filename2;

	if (is_iso_list(p1)) {
		size_t len = scan_is_chars_list(q, p1, p1_ctx, true);

		if (!len)
			return throw_error(q, p1, p1_ctx, "type_error", "atom");

		filename1 = chars_list_to_string(q, p1, p1_ctx);
	} else
		filename1 = DUP_STRING(q, p1);

	if (is_iso_list(p2)) {
		size_t len = scan_is_chars_list(q, p2, p2_ctx, true);

		if (!len) {
			TPL_free(filename1);
			return throw_error(q, p2, p2_ctx, "type_error", "atom");
		}

		filename2 = chars_list_to_string(q, p2, p2_ctx);
	} else
		filename2 = DUP_STRING(q, p2);

	struct stat st = {0};

	if (stat(filename1, &st)) {
		TPL_free(filename1);
		TPL_free(filename2);
		return throw_error(q, p1, p1_ctx, "existence_error", "file");
	}

	bool ok = !rename(filename1, filename2);
	TPL_free(filename1);
	TPL_free(filename2);
	return ok ? true : false;
}

static bool bif_copy_file_2(query *q)
{
	GET_FIRST_ARG(p1,source_sink);
	GET_NEXT_ARG(p2,source_sink);
	char *filename1, *filename2;

	if (is_iso_list(p1)) {
		size_t len = scan_is_chars_list(q, p1, p1_ctx, true);

		if (!len)
			return throw_error(q, p1, p1_ctx, "type_error", "atom");

		filename1 = chars_list_to_string(q, p1, p1_ctx);
	} else
		filename1 = DUP_STRING(q, p1);

	if (is_iso_list(p2)) {
		size_t len = scan_is_chars_list(q, p2, p2_ctx, true);

		if (!len) {
			TPL_free(filename1);
			return throw_error(q, p2, p2_ctx, "type_error", "atom");
		}

		filename2 = chars_list_to_string(q, p2, p2_ctx);
	} else
		filename2 = DUP_STRING(q, p2);

	FILE *fp1 = fopen(filename1, "rb");

	if (!fp1) {
		TPL_free(filename1);
		TPL_free(filename2);
		return throw_error(q, p1, p1_ctx, "existence_error", "file");
	}

	TPL_free(filename1);
	FILE *fp2 = fopen(filename2, "wb");

	if (!fp2) {
		fclose(fp1);
		TPL_free(filename2);
		return throw_error(q, p2, p2_ctx, "permission_error", "file");
	}

	TPL_free(filename2);
	char buffer[1024];
	size_t n;

	while ((n = fread(buffer, 1, sizeof(buffer), fp1)) > 0) {
		if (fwrite(buffer, 1, n, fp2) != n) {
			fclose(fp2);
			fclose(fp1);
			return throw_error(q, p2, p2_ctx, "system_error", "file");
		}
	}

	fclose(fp2);

	if (!feof(fp1)) {
		fclose(fp1);
		return throw_error(q, p1, p1_ctx, "system_error", "file");
	}

	fclose(fp1);
	return true;
}

static bool bif_time_file_2(query *q)
{
	GET_FIRST_ARG(p1,source_sink);
	GET_NEXT_ARG(p2,var);
	char *filename;

	if (is_iso_list(p1)) {
		size_t len = scan_is_chars_list(q, p1, p1_ctx, true);

		if (!len)
			return throw_error(q, p1, p1_ctx, "type_error", "atom");

		filename = chars_list_to_string(q, p1, p1_ctx);
	} else
		filename = DUP_STRING(q, p1);

	struct stat st = {0};

	if (stat(filename, &st)) {
		TPL_free(filename);
		return throw_error(q, p1, p1_ctx, "existence_error", "file");
	}

	TPL_free(filename);
	cell tmp;
	make_float(&tmp, st.st_mtime);
	return unify(q, p2, p2_ctx, &tmp, q->st.cur_ctx);
}

static bool bif_size_file_2(query *q)
{
	GET_FIRST_ARG(p1,source_sink);
	GET_NEXT_ARG(p2,integer_or_var);
	char *filename;

	if (is_iso_list(p1)) {
		size_t len = scan_is_chars_list(q, p1, p1_ctx, true);

		if (!len)
			return throw_error(q, p1, p1_ctx, "type_error", "atom");

		filename = chars_list_to_string(q, p1, p1_ctx);
	} else
		filename = DUP_STRING(q, p1);

	struct stat st = {0};

	if (stat(filename, &st)) {
		TPL_free(filename);
		return throw_error(q, p1, p1_ctx, "existence_error", "file");
	}

	TPL_free(filename);
	cell tmp;
	make_int(&tmp, st.st_size);
	return unify(q, p2, p2_ctx, &tmp, q->st.cur_ctx);
}

static bool bif_exists_directory_1(query *q)
{
	GET_FIRST_ARG(p1,source_sink);
	char *filename;

	if (is_iso_list(p1)) {
		size_t len = scan_is_chars_list(q, p1, p1_ctx, true);

		if (!len)
			return throw_error(q, p1, p1_ctx, "type_error", "atom");

		filename = chars_list_to_string(q, p1, p1_ctx);
	} else
		filename = DUP_STRING(q, p1);

	struct stat st = {0};

	if (stat(filename, &st)) {
		TPL_free(filename);
		return false;
	}

	TPL_free(filename);

	if ((st.st_mode & S_IFMT) != S_IFDIR)
		return false;

	return true;
}

static bool bif_make_directory_1(query *q)
{
	GET_FIRST_ARG(p1,source_sink);
	char *filename;

	if (is_iso_list(p1)) {
		size_t len = scan_is_chars_list(q, p1, p1_ctx, true);

		if (!len)
			return throw_error(q, p1, p1_ctx, "type_error", "atom");

		filename = chars_list_to_string(q, p1, p1_ctx);
	} else
		filename = DUP_STRING(q, p1);

	struct stat st = {0};

	if (!stat(filename, &st)) {
		TPL_free(filename);
		return throw_error(q, p1, p1_ctx, "existence_error", "file");
	}

	if (mkdir(filename, 0777)) {
		TPL_free(filename);
		return throw_error(q, p1, p1_ctx, "permission_error", "file");
	}

	TPL_free(filename);
	return true;
}

static bool bif_make_directory_path_1(query *q)
{
	GET_FIRST_ARG(p1,source_sink);
	char *filename;

	if (is_iso_list(p1)) {
		size_t len = scan_is_chars_list(q, p1, p1_ctx, true);

		if (!len)
			return throw_error(q, p1, p1_ctx, "type_error", "atom");

		filename = chars_list_to_string(q, p1, p1_ctx);
	} else
		filename = DUP_STRING(q, p1);

	struct stat st = {0};

	for (char *ptr = filename+1; *ptr; ptr++) {
		if (*ptr == '/') {
			*ptr = '\0';

			if (stat(filename, &st)) {
				if (mkdir(filename, 0777)) {
					TPL_free(filename);
					return throw_error(q, p1, p1_ctx, "permission_error", "directory");
				}
			}

			*ptr = '/';
		}
	}

	if (!stat(filename, &st)) {
		TPL_free(filename);
		return true;
	}

	if (mkdir(filename, 0777)) {
		TPL_free(filename);
		return throw_error(q, p1, p1_ctx, "permission_error", "directory");
	}

	TPL_free(filename);
	return true;
}

static bool bif_working_directory_2(query *q)
{
	GET_FIRST_ARG(p_old,var);
	GET_NEXT_ARG(p_new,atom_or_list_or_var);
	char tmpbuf[PATH_MAX], tmpbuf2[PATH_MAX];
	char *oldpath = getcwd(tmpbuf, sizeof(tmpbuf));

	if (!oldpath)	// FIX: getcwd may fail
		return throw_error(q, p_old, p_old_ctx, "system_error", "getcwd");

	snprintf(tmpbuf2, sizeof(tmpbuf2), "%s/", oldpath);
	oldpath = tmpbuf2;
	cell tmp;
	make_string(&tmp, oldpath);

	if (is_atom_or_list(p_new)) {
		char *filename;

		if (is_iso_list(p_new)) {
			size_t len = scan_is_chars_list(q, p_new, p_new_ctx, true);

			if (!len) {
				unshare_cell(&tmp);
				return throw_error(q, p_new, p_new_ctx, "type_error", "atom");
			}

			filename = chars_list_to_string(q, p_new, p_new_ctx);
		} else
			filename = DUP_STRING(q, p_new);

		if (chdir(filename)) {
			TPL_free(filename);	// FIX: free filename on chdir failure
			unshare_cell(&tmp);
			return throw_error(q, p_new, p_new_ctx, "existence_error", "path");
		}

		TPL_free(filename);
	}

	bool ok = unify(q, p_old, p_old_ctx, &tmp, q->st.cur_ctx);
	unshare_cell(&tmp);
	return ok;
}

static bool bif_chdir_1(query *q)
{
	GET_FIRST_ARG(p1,source_sink);
	char *filename;

	if (is_iso_list(p1)) {
		filename = chars_list_to_string(q, p1, p1_ctx);
	} else
		filename = DUP_STRING(q, p1);

	bool ok = !chdir(filename);
	TPL_free(filename);
	return ok;
}

static bool bif_sys_get_chars_3(query *q)
{
	GET_FIRST_ARG(pstr,stream_or_alias);
	GET_NEXT_ARG(p1,integer_or_var);
	GET_NEXT_ARG(p2,var);
	int n = get_stream(q, pstr);
	stream *str = &q->pl->streams[n];
	cell tmp;

	if (is_var(p1)) {
		size_t n_size = 1024;
		char *data = TPL_malloc(n_size);
		CHECKED(data);
		char *dst = data;
		unsigned len = 0;

		for (;;) {
			int ch = str->ungetch ? str->ungetch : xgetc_utf8_lax(tpl_getc, str);

			if (errno == EINTR) {
				clearerr(str->fp_in);
				return throw_timeout(q);
			}

			if (feof(str->fp)) {
				clearerr(str->fp_in);
				break;
			}

			str->ungetch = 0;
			size_t off = dst - data;

			if ((off + MAX_BYTES_PER_CODEPOINT) >= n_size) {
				data = TPL_realloc(data, n_size*=2);
				CHECKED(data);
				dst = data + off;
			}

			dst += put_char_utf8(dst, ch);
			len++;
		}

		make_uint(&tmp, len);
		bool ok = unify(q, p1, p1_ctx, &tmp, q->st.cur_ctx);

		if (!ok) {
			TPL_free(data);
			return false;
		}

		make_stringn(&tmp, data, dst-data);
		ok = unify(q, p2, p2_ctx, &tmp, q->st.cur_ctx);
		unshare_cell(&tmp);
		TPL_free(data);
		return ok;
	}

	if (is_bigint(p1))
		return throw_error(q, p1, p1_ctx, "domain_error", "small_integer_range");

	int len = get_smallint(p1);

	if (len < 0)
		return false;

	if (len == 0) {
		make_atom(&tmp, g_nil_s);
		return unify(q, p2, p2_ctx, &tmp, q->st.cur_ctx);
	}

	char *data = TPL_malloc(len*MAX_BYTES_PER_CODEPOINT+1);
	CHECKED(data);
	char *dst = data;

	while (len--) {
		int ch = str->ungetch ? str->ungetch : xgetc_utf8_lax(tpl_getc, str);
		str->ungetch = 0;

		if (errno == EINTR) {
			clearerr(str->fp_in);
			return throw_timeout(q);
		}

		if (feof(str->fp)) {
			clearerr(str->fp_in);
			break;
		}

		dst += put_char_utf8(dst, ch);
	}

	make_stringn(&tmp, data, dst-data);
	bool ok = unify(q, p2, p2_ctx, &tmp, q->st.cur_ctx);
	unshare_cell(&tmp);
	TPL_free(data);
	return ok;
}

static bool bif_sys_bread_3(query *q)
{
	GET_FIRST_ARG(pstr,stream_or_alias);
	GET_NEXT_ARG(p1,integer_or_var);
	GET_NEXT_ARG(p2,var);
	int n = get_stream(q, pstr);
	stream *str = &q->pl->streams[n];
	size_t len;

	if (is_bigint(p1))
		return throw_error(q, p1, p1_ctx, "domain_error", "small_integer_range");

	// str->data holds a partial read across a task's yields below, so a
	// second query arriving mid-read would append to - and mis-size - the
	// first one's buffer: the `Want - data_len` remaining-length is
	// unsigned, so a shorter second request underflows it into a huge
	// read straight past the end of that buffer. Two readers on one
	// stream have no coherent meaning anyway, so refuse rather than
	// corrupt. Writers are different, and allowed - see write_all().
	if (str->data && (str->data_owner != q))
		return throw_error(q, pstr, pstr_ctx, "permission_error", "input,busy_stream");

	str->data_owner = q;

	if (is_integer(p1) && is_positive(p1)) {
		if (!str->data) {
			str->data = TPL_malloc(get_smallint(p1)+1);
			CHECKED(str->data);
			str->data_len = 0;
		}

		if (str->ungetch) {
			unsigned n = put_char_utf8(str->data, str->ungetch);
			str->data_len += n;
			str->ungetch = 0;
		}

		for (;;) {
			len = get_smallint(p1) - str->data_len;
			size_t nbytes = tpl_read(str->data+str->data_len, len, str);

			if (errno == EINTR) {
				clearerr(str->fp_in);
				return throw_timeout(q);
			}

			str->data_len += nbytes;
			str->data[str->data_len] = '\0';

			if (nbytes == len)
				break;

			if (feof(str->fp)) {
				clearerr(str->fp_in);
				TPL_free(str->data);
				str->data = NULL;
				return false;
			}

			if (q->is_task) {
				clearerr(str->fp_in);
				return do_yield_on_stream(q, str, false);
			}

			// A non-task socket is non-blocking now (see bif_net.c), so a
			// short, non-EOF read here is routinely just EAGAIN - nothing
			// more arrived *yet* - not a real error. Without this, the
			// loop above spins at full CPU re-issuing tpl_read() until
			// data shows up, instead of waiting for it: this predicate
			// backs library(http)'s Content-Length and chunked body
			// reads, both of which pass a real positive Len and hit this
			// path on every ordinary POST/PUT or chunked response.
			if (ferror(str->fp) && ((errno == EAGAIN) || (errno == EWOULDBLOCK))) {
				clearerr(str->fp_in);

				if (!tpl_wait_fd_readable(q, fileno(str->fp_in))) {
					TPL_free(str->data);
					str->data = NULL;
					return throw_timeout(q);	// errno == EINTR
				}
			}
		}

		cell tmp;
		make_stringn(&tmp, str->data, str->data_len);
		bool ok = unify(q, p2, p2_ctx, &tmp, q->st.cur_ctx);
		unshare_cell(&tmp);
		TPL_free(str->data);
		str->data = NULL;
		return ok;
	}

	if (is_integer(p1)) {
		if (!str->data) {
			str->data = TPL_malloc((str->alloc_nbytes=1024)+1);
			CHECKED(str->data);
			str->data_len = 0;
		}

		if (str->ungetch) {
			unsigned n = put_char_utf8(str->data, str->ungetch);
			str->data_len += n;
			str->ungetch = 0;
		}

		size_t nbytes = tpl_read(str->data, str->alloc_nbytes, str);
		str->data[nbytes] = '\0';
		str->data = TPL_realloc(str->data, nbytes+1);
		CHECKED(str->data);
		cell tmp;
		make_stringn(&tmp, str->data, nbytes);
		bool ok = unify(q, p2, p2_ctx, &tmp, q->st.cur_ctx);
		unshare_cell(&tmp);
		TPL_free(str->data);
		str->data = NULL;
		return ok;
	}

	if (!str->data) {
		str->data = TPL_malloc((str->alloc_nbytes=1024)+1);
		CHECKED(str->data);
		str->data_len = 0;
	}

	if (str->ungetch) {
		unsigned n = put_char_utf8(str->data, str->ungetch);
		str->data_len += n;
		str->ungetch = 0;
	}

	for (;;) {
		size_t len = str->alloc_nbytes - str->data_len;
		size_t nbytes = tpl_read(str->data+str->data_len, len, str);
		str->data_len += nbytes;
		str->data[str->data_len] = '\0';

		if (!nbytes || feof(str->fp))
			break;

		if (str->alloc_nbytes == str->data_len) {
			str->data = TPL_realloc(str->data, (str->alloc_nbytes*=2)+1);
			CHECKED(str->data);
		}
	}

	cell tmp1;
	make_int(&tmp1, str->data_len);
	unify(q, p1, p1_ctx, &tmp1, q->st.cur_ctx);
	cell tmp2;

	if (str->data_len)
		make_stringn(&tmp2, str->data, str->data_len);
	else
		make_atom(&tmp2, g_nil_s);

	bool ok = unify(q, p2, p2_ctx, &tmp2, q->st.cur_ctx);
	unshare_cell(&tmp2);
	TPL_free(str->data);
	str->data = NULL;
	return ok;
}

static bool bif_sys_bflush_1(query *q)
{
	GET_FIRST_ARG(pstr,stream_or_alias);
	int n = get_stream(q, pstr);
	stream *str = &q->pl->streams[n];
	bool ok = flush_or_throw(q, str);
	TPL_free(str->data);
	str->data = NULL;
	str->data_len = 0;
	return ok;
}

// Resuming a parked task re-runs this call from scratch with the
// original arguments, so str->wbuf is the only record of progress.
static void free_wbuf(stream *str)
{
	TPL_free(str->wbuf);
	str->wbuf = NULL;
	str->wbuf_owner = NULL;
	str->wbuf_len = str->wbuf_pos = 0;
}

// Writes len bytes of src to str, waiting out real backpressure (EAGAIN)
// rather than dropping the remainder or busy-spinning. A resumed task
// uses str->wbuf instead of src/len - an expensive-to-derive caller
// should check str->wbuf first and skip that work too (see put_chars_2).
static bool write_all(query *q, stream *str, const char *src, size_t len)
{
	bool mine = resuming_write(q, str);

	if (mine) {
		src = str->wbuf + str->wbuf_pos;
		len = str->wbuf_len - str->wbuf_pos;
	}

	while (len) {
		size_t nbytes = tpl_write(src, len, str);
		FILE *fp_wr = str->fp_out ? str->fp_out : str->fp;

		if (!nbytes) {
			if (feof(fp_wr) || ferror(fp_wr)) {
				if ((errno == EAGAIN) || (errno == EWOULDBLOCK)) {
					clearerr(fp_wr);

					// Park only when this stream's parked write is ours,
					// or there is none: a task writing a stream another
					// task is already parked on can neither borrow that
					// buffer nor overwrite it, so it waits inline below
					// instead - rare, and correct.
					if (q->is_task && (mine || !str->wbuf)) {
						if (!mine) {
							str->wbuf = TPL_malloc(len);

							if (!str->wbuf)
								return false;

							memcpy(str->wbuf, src, len);
							str->wbuf_owner = q;
							str->wbuf_len = len;
							str->wbuf_pos = 0;
						}

						// do_yield_on_stream() re-reads errno to decide
						// whether it may park on POLLOUT, and the malloc
						// above is allowed to have clobbered it.
						errno = EAGAIN;
						return do_yield_on_stream(q, str, true);
					}

					if (!tpl_wait_fd_writable(q, fileno(fp_wr))) {
						if (mine) free_wbuf(str);
						return false;	// errno == EINTR
					}

					continue;
				}

				if (mine) free_wbuf(str);
				return false; // can feof() happen on writing?
			}
		}

		clearerr(fp_wr);
		len -= nbytes;
		src += nbytes;

		if (mine)
			str->wbuf_pos = (size_t)(src - str->wbuf);
	}

	if (mine)
		free_wbuf(str);

	return true;
}

static bool bif_sys_bwrite_2(query *q)
{
	GET_FIRST_ARG(pstr,stream_or_alias);
	GET_NEXT_ARG(p1,atom);
	int n = get_stream(q, pstr);
	stream *str = &q->pl->streams[n];
	return write_all(q, str, C_STR(q, p1), C_STRLEN(q, p1));
}

static bool bif_sys_readline_2(query *q)
{
	GET_FIRST_ARG(p1,string);
	GET_NEXT_ARG(p2,var);
	int n = q->pl->current_output;
	stream *str = &q->pl->streams[n];
	char *s = history_readline_eol(q->pl, C_STR(q, p1), '.');
	if (!s) return false;
	cell tmp;
	make_string(&tmp, s);
	TPL_free(s);
	return unify(q, p2, p2_ctx, &tmp, q->st.cur_ctx);
}

static bool bif_sys_put_chars_1(query *q)
{
	GET_FIRST_ARG(p1,any);
	int n = q->pl->current_output;
	stream *str = &q->pl->streams[n];

	if (resuming_write(q, str))
		return write_all(q, str, NULL, 0);

	if (is_cstring(p1)) {
		const char *src = C_STR(q, p1);
		size_t len = C_STRLEN(q, p1);
		return write_all(q, str, src, len);
	} else if ((scan_is_chars_list(q, p1, p1_ctx, true)) > 0) {
		char *src = chars_list_to_string(q, p1, p1_ctx);
		bool ok = write_all(q, str, src, strlen(src));
		TPL_free(src);
		return ok;
	} else if (is_nil(p1)) {
		;
	} else
		return throw_error(q, p1, p1_ctx, "type_error", "cchars");

	return true;
}

static bool bif_sys_put_chars_2(query *q)
{
	GET_FIRST_ARG(pstr,stream_or_alias);
	int n = get_stream(q, pstr);
	stream *str = &q->pl->streams[n];
	GET_NEXT_ARG(p1,list_or_nil);

	if (resuming_write(q, str))
		return write_all(q, str, NULL, 0);

	if (is_cstring(p1)) {
		const char *src = C_STR(q, p1);
		size_t len = C_STRLEN(q, p1);
		return write_all(q, str, src, len);
	} else if ((scan_is_chars_list(q, p1, p1_ctx, true)) > 0) {
		char *src = chars_list_to_string(q, p1, p1_ctx);
		bool ok = write_all(q, str, src, strlen(src));
		TPL_free(src);
		return ok;
	} else if (is_nil(p1)) {
		;
	} else
		return throw_error(q, p1, p1_ctx, "type_error", "chars");

	return true;
}

// Begin a capture on a stream. Starting one inside another does not
// stop the outer one: the two share the buffer, and the inner capture
// remembers how much of it was already there so it can take only what
// its own goal writes. Toggling is_memory instead - which is what this
// did - turned capturing off on the nested call and freed the outer
// buffer with it, so with_output_to/2 inside with_output_to/2 lost
// both and let the text through to the real output.

static bool begin_capture(query *q, stream *str)
{
	capture *c = TPL_malloc(sizeof(capture));

	if (!c)
		return throw_error(q, q->st.instr, q->st.cur_ctx, "resource_error", "memory");

	if (!str->captures) {
		SB_init(str->sb);
		str->is_memory = true;
	}

	c->at = SB_strlen(str->sb);
	c->prev = str->captures;
	str->captures = c;
	return true;
}

// Where the innermost capture's text starts in the buffer.

static size_t capture_start(stream *str)
{
	return str->captures ? str->captures->at : 0;
}

// End it, once its text has been taken: an outer capture carries on
// with the buffer truncated to where this one began, and the last one
// out gives the stream back to its file.

static void end_capture(stream *str)
{
	capture *c = str->captures;

	if (!c) {							// unpaired, as a toggle-off used to be
		str->is_memory = false;
		SB_free(str->sb);
		return;
	}

	size_t at = c->at;
	str->captures = c->prev;
	TPL_free(c);

	if (str->captures) {
		SB_setlen(str->sb, at);			// the capture around this must not see it
	} else {
		str->is_memory = false;
		SB_free(str->sb);
	}
}

static bool bif_sys_capture_output_0(query *q)
{
	int n = q->pl->current_output;
	stream *str = &q->pl->streams[n];
	return begin_capture(q, str);
}

// A capture that caught nothing is the empty character list, not the
// empty atom: make_stringn() maps a zero length to '', which is what
// the atom and string builtins want but not a list of characters.

static bool bif_sys_capture_output_to_chars_1(query *q)
{
	GET_FIRST_ARG(p1,var);
	int n = q->pl->current_output;
	stream *str = &q->pl->streams[n];
	size_t at = capture_start(str);
	const char *buf = SB_cstr(str->sb);		// a conditional expression, so bind it first
	const char *src = buf + at;
	size_t len = SB_strlen(str->sb) - at;
	cell tmp;

	if (len)
		make_stringn(&tmp, src, len);
	else
		make_atom(&tmp, g_nil_s);

	end_capture(str);
	bool ok = unify(q, p1, p1_ctx, &tmp, q->st.cur_ctx);
	unshare_cell(&tmp);
	return ok;
}

static bool bif_sys_capture_output_to_atom_1(query *q)
{
	GET_FIRST_ARG(p1,var);
	int n = q->pl->current_output;
	stream *str = &q->pl->streams[n];
	size_t at = capture_start(str);
	const char *buf = SB_cstr(str->sb);		// a conditional expression, so bind it first
	const char *src = buf + at;
	size_t len = SB_strlen(str->sb) - at;
	cell tmp;
	make_cstringn(&tmp, src, len);
	end_capture(str);
	bool ok = unify(q, p1, p1_ctx, &tmp, q->st.cur_ctx);
	unshare_cell(&tmp);
	return ok;
}

static bool bif_sys_capture_error_0(query *q)
{
	int n = q->pl->current_error;
	stream *str = &q->pl->streams[n];
	return begin_capture(q, str);
}

static bool bif_sys_capture_error_to_chars_1(query *q)
{
	GET_FIRST_ARG(p1,var);
	int n = q->pl->current_error;
	stream *str = &q->pl->streams[n];
	size_t at = capture_start(str);
	const char *buf = SB_cstr(str->sb);		// a conditional expression, so bind it first
	const char *src = buf + at;
	size_t len = SB_strlen(str->sb) - at;
	cell tmp;

	if (len)
		make_stringn(&tmp, src, len);
	else
		make_atom(&tmp, g_nil_s);

	end_capture(str);
	bool ok = unify(q, p1, p1_ctx, &tmp, q->st.cur_ctx);
	unshare_cell(&tmp);
	return ok;
}

static bool bif_sys_capture_error_to_atom_1(query *q)
{
	GET_FIRST_ARG(p1,var);
	int n = q->pl->current_error;
	stream *str = &q->pl->streams[n];
	size_t at = capture_start(str);
	const char *buf = SB_cstr(str->sb);		// a conditional expression, so bind it first
	const char *src = buf + at;
	size_t len = SB_strlen(str->sb) - at;
	cell tmp;
	make_cstringn(&tmp, src, len);
	end_capture(str);
	bool ok = unify(q, p1, p1_ctx, &tmp, q->st.cur_ctx);
	unshare_cell(&tmp);
	return ok;
}

typedef struct {
	size_t size1, stride;
	double *data;
	void *gsl_block;
	int owner;
} gsl_vector;

static bool bif_sys_gsl_vector_write_2(query *q)
{
	GET_FIRST_ARG(p1,integer);
	gsl_vector *v = (void*)(size_t)get_smalluint(p1);
	GET_NEXT_ARG(p2,stream);
	int n = q->pl->current_output;
	stream *str = &q->pl->streams[n];

	fprintf(str->fp_out, "#%llu,%ux1,%.1f\n",
		(unsigned long long)v->size1,
		(unsigned)v->size1,
		0.0);

	for (unsigned i = 0; i < v->size1; i++) {
		double val = v->data[i * v->stride];

		if (i != 0)
			fprintf(str->fp_out, ",");

		fprintf(str->fp_out, "%g", val);
	}

	fprintf(str->fp_out, "\n");
	return true;
}

static bool bif_sys_gsl_vector_alloc_2(query *q)
{
	GET_FIRST_ARG(p1,stream);
	int n = get_stream(q, p1);
	stream *str = &q->pl->streams[n];
	GET_NEXT_ARG(p2,var);

	unsigned long long tot = 0;
	unsigned rows = 0, cols = 0;
	double def_value = 0.0;
	char tmpbuf[128];
	tmpbuf[0] = '\0';

	if ((n = fscanf(str->fp, "%*c%llu,%u%*c%u,%lg,%127s\n", &tot, &rows, &cols, &def_value, tmpbuf)) < 3)
		return throw_error(q, p1, p1_ctx, "domain_error", "header_error");

	tmpbuf[sizeof(tmpbuf)-1] = '\0';
	cell tmp;
	make_uint(&tmp, rows);
	unify(q, p2, p2_ctx, &tmp, q->st.cur_ctx);
	return true;
}

static bool bif_sys_gsl_vector_read_2(query *q)
{
	GET_FIRST_ARG(p0,integer);
	gsl_vector *v = (void*)(size_t)get_smalluint(p0);
	GET_NEXT_ARG(p1,stream);
	int n = get_stream(q, p1);
	stream *str = &q->pl->streams[n];

	for (unsigned i = 0; i < v->size1; i++) {
		double val;

		if (fscanf(str->fp, "%lg", &val) <= 0)
			return false;

		v->data[i * v->stride] = val;

		int ch = fgetc(str->fp);

		if (ch == '\r')
			fgetc(str->fp);
	}

	return true;
}

static bool bif_sys_gsl_vector_size_2(query *q)
{
	GET_FIRST_ARG(p1,integer);
	gsl_vector *v = (void*)(size_t)get_smalluint(p1);
	GET_NEXT_ARG(p2,integer_or_var);
	cell tmp;
	make_uint(&tmp, v->size1);
	return unify(q, p2, p2_ctx, &tmp, q->st.cur_ctx);
}

typedef struct {
	size_t size1, size2, tda;
	double *data;
	void *gsl_block;
	int owner;
} gsl_matrix;

static bool bif_sys_gsl_matrix_write_2(query *q)
{
	GET_FIRST_ARG(p1,integer);
	gsl_matrix *m = (void*)(size_t)get_smalluint(p1);
	GET_NEXT_ARG(p2,stream);
	int n = q->pl->current_output;
	stream *str = &q->pl->streams[n];

	fprintf(str->fp_out, "#%llu,%ux%u,%.1f\n",
		(unsigned long long)m->size1*m->size2,
		(unsigned)m->size1,
		(unsigned)m->size2,
		0.0);

	for (unsigned i = 0; i < m->size1; i++) {
		for (unsigned j = 0; j < m->size2; j++) {
			if (j != 0)
				fprintf(str->fp_out, ",");

			double val = m->data[i * m->tda + j];
			fprintf(str->fp_out, "%g", val);
		}

		fprintf(str->fp_out, "\n");
	}

	return true;
}

static bool bif_sys_gsl_matrix_alloc_3(query *q)
{
	GET_FIRST_ARG(p1,stream);
	int n = get_stream(q, p1);
	stream *str = &q->pl->streams[n];
	GET_NEXT_ARG(p2,var);
	GET_NEXT_ARG(p3,var);

	unsigned long long tot = 0;
	long rows = 0, cols = 0;
	double def_value = 0.0;
	char tmpbuf[128];
	tmpbuf[0] = '\0';

	if ((n = fscanf(str->fp, "%*c%llu,%ld%*c%ld,%lg,%127s\n", &tot, &rows, &cols, &def_value, tmpbuf)) < 3)
		return throw_error(q, p1, p1_ctx, "domain_error", "header_error");

	if ((rows <= 0) || (cols <= 0))
		return throw_error(q, p1, p1_ctx, "domain_error", "matrix_dimensions");

	if ((rows >= INT32_MAX) || (cols >= INT32_MAX))
		return throw_error(q, p1, p1_ctx, "domain_error", "matrix_dimensions");

	tmpbuf[sizeof(tmpbuf)-1] = '\0';
	cell tmp;
	make_int(&tmp, rows);
	unify(q, p2, p2_ctx, &tmp, q->st.cur_ctx);
	make_int(&tmp, cols);
	unify(q, p3, p3_ctx, &tmp, q->st.cur_ctx);
	return true;
}

static bool bif_sys_gsl_matrix_read_2(query *q)
{
	GET_FIRST_ARG(p0,integer);
	gsl_matrix *m = (void*)(size_t)get_smalluint(p0);
	GET_NEXT_ARG(p1,stream);
	int n = get_stream(q, p1);
	stream *str = &q->pl->streams[n];

	for (unsigned i = 0; i < m->size1; i++) {
		for (unsigned j = 0; j < m->size2; j++) {
			double val;

			if (fscanf(str->fp, "%lg", &val) <= 0)
				return false;

			m->data[i * m->tda + j] = val;

			int ch = fgetc(str->fp);

			if (ch == '\r')
				fgetc(str->fp);
		}
	}

	return true;
}

static bool bif_sys_gsl_matrix_size_3(query *q)
{
	GET_FIRST_ARG(p1,integer);
	gsl_matrix *m = (void*)(size_t)get_smalluint(p1);
	GET_NEXT_ARG(p2,integer_or_var);
	GET_NEXT_ARG(p3,integer_or_var);
	cell tmp;
	make_uint(&tmp, m->size1);

	if (!unify(q, p2, p2_ctx, &tmp, q->st.cur_ctx))
		return false;

	make_uint(&tmp, m->size2);
	return unify(q, p3, p3_ctx, &tmp, q->st.cur_ctx);
}

static bool bif_set_stream_2(query *q)
{
	GET_FIRST_ARG(pstr,stream_or_alias);
	int n = get_stream(q, pstr);
	stream *str = &q->pl->streams[n];
	GET_NEXT_ARG(p1,any);

	if (is_nil(p1))
		return true;

	if (is_iso_list(p1)) {
		PROLOG_LIST_HANDLER(p1);

		while (is_iso_list(p1)) {
			cell *h = PROLOG_LIST_HEAD(p1);
			cell *c = deref(q, h, p1_ctx);
			pl_ctx c_ctx = q->latest_ctx;
			cell *name = c + 1;
			name = deref(q, name, c_ctx);

			if (is_var(c))
				return throw_error(q, c, c_ctx, "instantiation_error", "args_not_sufficiently_instantiated");

			if (!CMP_STRING_TO_CSTR(q, c, "alias")) {
				if (is_var(name))
					return throw_error(q, name, c_ctx, "instantiation_error", "stream_option");

				if (!is_atom(name))
					return throw_error(q, c, c_ctx, "domain_error", "stream_property");

				if (!CMP_STRING_TO_CSTR(q, name, "current_input")) {
					q->pl->current_input = n;
				} else if (!CMP_STRING_TO_CSTR(q, name, "current_output")) {
					q->pl->current_output = n;
				} else if (!CMP_STRING_TO_CSTR(q, name, "current_error")) {
					q->pl->current_error = n;
				} else {
					int n2 = get_named_stream(q->pl, C_STR(q, name), C_STRLEN(q, name));

					// An alias already held by a DIFFERENT stream used
					// to be sl_del()'d from that stream and handed
					// over silently, so the original lost its name
					// with no indication - a typo'd alias would
					// quietly steal an open stream's identity.
					// open/4 refuses the same situation with a
					// permission_error; do the same here.
					//
					// n2 == n is re-aliasing a stream to a name it
					// already holds: a no-op, not an error, and not a
					// second skiplist entry either.

					if (n2 >= 0) {
						if (n2 != n)
							return throw_error(q, c, c_ctx, "permission_error", "modify,stream_alias");
					} else
						sl_app(str->alias, DUP_STRING(q, name), NULL);
				}

				return true;
			}

			if (!CMP_STRING_TO_CSTR(q, c, "type")) {
				if (is_var(name))
					return throw_error(q, name, c_ctx, "instantiation_error", "stream_option");

				if (!is_atom(name))
					return throw_error(q, c, c_ctx, "domain_error", "stream_property");

				if (!CMP_STRING_TO_CSTR(q, name, "binary")) {
					str->binary = true;
				} else if (!CMP_STRING_TO_CSTR(q, name, "text")) {
					str->binary = false;
				}

				return true;
			}

			if (!CMP_STRING_TO_CSTR(q, c, "timeout")) {
				if (is_var(name))
					return throw_error(q, name, c_ctx, "instantiation_error", "stream_option");

				if (!is_number(name))
					return throw_error(q, c, c_ctx, "domain_error", "stream_property");

				if (is_float(name))
					str->timeout_ms = get_float(name) * 1000;
				else
					str->timeout_ms = get_smallint(name) * 1000;
			}

			p1 = PROLOG_LIST_TAIL(p1);
			p1 = deref(q, p1, p1_ctx);
			p1_ctx = q->latest_ctx;

			if (is_var(p1))
				return throw_error(q, p1, p1_ctx, "instantiation_error", "args_not_sufficiently_instantiated");
		}

		return true;
	}

	if (!is_structure(p1))
		return throw_error(q, p1, p1_ctx, "domain_error", "stream_property");

	cell *name = p1 + 1;
	name = deref(q, name, p1_ctx);

	if (!CMP_STRING_TO_CSTR(q, p1, "alias")) {
		if (is_var(name))
			return throw_error(q, name, q->latest_ctx, "instantiation_error", "stream_option");

		if (!is_atom(name))
			return throw_error(q, p1, p1_ctx, "domain_error", "stream_property");

		if (!CMP_STRING_TO_CSTR(q, name, "current_input")) {
			q->pl->current_input = n;
		} else if (!CMP_STRING_TO_CSTR(q, name, "current_output")) {
			q->pl->current_output = n;
		} else if (!CMP_STRING_TO_CSTR(q, name, "current_error")) {
			q->pl->current_error = n;
		} else {
			int n2 = get_named_stream(q->pl, C_STR(q, name), C_STRLEN(q, name));

			if (n2 >= 0) {
				stream *str2 = &q->pl->streams[n2];
				sl_del(str2->alias, C_STR(q, name));
			}

			sl_app(str->alias, DUP_STRING(q, name), NULL);
		}

		return true;
	}

	if (!CMP_STRING_TO_CSTR(q, p1, "type")) {
		if (is_var(name))
			return throw_error(q, name, q->latest_ctx, "instantiation_error", "stream_option");

		if (!is_atom(name))
			return throw_error(q, p1, p1_ctx, "domain_error", "stream_property");

		if (!CMP_STRING_TO_CSTR(q, name, "binary")) {
			str->binary = true;
		} else if (!CMP_STRING_TO_CSTR(q, name, "text")) {
			str->binary = false;
		}

		return true;
	}

	if (!CMP_STRING_TO_CSTR(q, p1, "timeout")) {
		if (is_var(name))
			return throw_error(q, name, q->latest_ctx, "instantiation_error", "stream_option");

		if (!is_number(name))
			return throw_error(q, p1, p1_ctx, "domain_error", "stream_property");

		if (is_float(name))
			str->timeout_ms = get_float(name) * 1000;
		else
			str->timeout_ms = get_smallint(name) * 1000;
	}

	return true;
}

static bool bif_portray_clause_1(query *q)
{
	GET_FIRST_ARG(p1,any);
	int n = q->pl->current_output;
	stream *str = &q->pl->streams[n];
	q->quoted = 1;
	q->portray_vars = true;
	q->print_idx = q->name_idx = 0;
	q->double_quotes = true;
	q->max_depth = 0;
	q->fullstop = q->nl = true;
	print_term_to_stream(q, str, p1, p1_ctx, 1);
	q->quoted = 0;
	q->portray_vars = false;
	clear_write_options(q);
	return true;
}

static bool bif_portray_clause_2(query *q)
{
	GET_FIRST_ARG(pstr,stream_or_alias);
	int n = get_stream(q, pstr);
	stream *str = &q->pl->streams[n];
	GET_NEXT_ARG(p1,any);
	q->quoted = 1;
	q->portray_vars = true;
	q->print_idx = q->name_idx = 0;
	q->max_depth = 0;
	q->fullstop = q->nl = true;
	print_term_to_stream(q, str, p1, p1_ctx, 1);
	q->quoted = 0;
	q->portray_vars = false;
	clear_write_options(q);
	return true;
}

static bool bif_is_stream_1(query *q)
{
	GET_FIRST_ARG(p1,any);
	return is_stream(p1) && !(p1->flags & FLAG_INT_ALIAS);
}

static bool bif_alias_2(query *q)
{
	GET_FIRST_ARG(p1,integer_or_var);

	if (is_integer(p1)) {
		GET_NEXT_ARG(p2,atom);
		int n = new_stream(q->pl);

		if (n < 0)
			return throw_error(q, p1, p1_ctx, "resource_error", "too_many_streams");

		stream *str = &q->pl->streams[n];
		CHECKED(str->alias = sl_create((void*)fake_strcmp, (void*)keyfree, NULL));
		sl_app(str->alias, DUP_STRING(q, p2), NULL);
		str->is_alias = true;
		str->handle = (void*)(size_t)get_smalluint(p1);
		return true;
	}

	GET_NEXT_ARG(p2,stream);
	int n = get_stream(q, p2);
	stream *str = &q->pl->streams[n];
	cell tmp;
	make_uint(&tmp, (size_t)str->handle);

	if (str->is_map)
		tmp.flags |= FLAG_INT_STREAM | FLAG_INT_MAP;
	else if (str->is_alias)
		tmp.flags |= FLAG_INT_ALIAS;
	else
		tmp.flags |= FLAG_INT_STREAM;

	return unify(q, p1, p1_ctx, &tmp, q->st.cur_ctx);
}

static bool bif_sys_stream_to_file_2(query *q)
{
	GET_FIRST_ARG(p1,stream);
	int n = get_stream(q, p1);
	stream *str = &q->pl->streams[n];
	GET_NEXT_ARG(p2,var);
	cell tmp;
	make_ptr(&tmp, str->fp);
	return unify(q, p2, p2_ctx, &tmp, q->st.cur_ctx);
}

builtins g_streams_bifs[] =
{
	// ISO...

#if TPL_FEATURE_FILESYSTEM
	{"open", 4, bif_iso_open_4, "+source_sink,+mode,--stream,+list", true, false, BLAH},
#endif
	{"close", 1, bif_iso_close_1, "+stream", true, false, BLAH},
	{"close", 2, bif_iso_close_2, "+stream,+opts", true, false, BLAH},
	{"read_term", 2, bif_iso_read_term_2, "+stream,-term", true, false, BLAH},
	{"read_term", 3, bif_iso_read_term_3, "+stream,-term,+list", true, false, BLAH},
	{"read", 1, bif_iso_read_1, "-term", true, false, BLAH},
	{"read", 2, bif_iso_read_2, "+stream,-term", true, false, BLAH},
	{"write_canonical", 1, bif_iso_write_canonical_1, "+term", true, false, BLAH},
	{"write_canonical", 2, bif_iso_write_canonical_2, "+stream,+term", true, false, BLAH},
	{"write_term", 2, bif_iso_write_term_2, "+stream,+term", true, false, BLAH},
	{"write_term", 3, bif_iso_write_term_3, "+stream,+term,+list", true, false, BLAH},
	{"writeq", 1, bif_iso_writeq_1, "+term", true, false, BLAH},
	{"writeq", 2, bif_iso_writeq_2, "+stream,+term", true, false, BLAH},
	{"write", 1, bif_iso_write_1, "+term", true, false, BLAH},
	{"write", 2, bif_iso_write_2, "+stream,+term", true, false, BLAH},
	{"nl", 0, bif_iso_nl_0, NULL, true, false, BLAH},
	{"nl", 1, bif_iso_nl_1, "+stream", true, false, BLAH},
	{"at_end_of_stream", 0, bif_iso_at_end_of_stream_0, NULL, true, false, BLAH},
	{"at_end_of_stream", 1, bif_iso_at_end_of_stream_1, "+stream", true, false, BLAH},
	{"set_stream_position", 2, bif_iso_set_stream_position_2, "+stream,+integer", true, false, BLAH},
	{"flush_output", 0, bif_iso_flush_output_0, NULL, true, false, BLAH},
	{"flush_output", 1, bif_iso_flush_output_1, "+stream", true, false, BLAH},
	{"put_char", 1, bif_iso_put_char_1, "+integer", true, false, BLAH},
	{"put_char", 2, bif_iso_put_char_2, "+stream,+integer", true, false, BLAH},
	{"put_code", 1, bif_iso_put_code_1, "+integer", true, false, BLAH},
	{"put_code", 2, bif_iso_put_code_2, "+stream,+integer", true, false, BLAH},
	{"put_byte", 1, bif_iso_put_byte_1, "+integer", true, false, BLAH},
	{"put_byte", 2, bif_iso_put_byte_2, "+stream,+integer", true, false, BLAH},
	{"get_char", 1, bif_iso_get_char_1, "-integer", true, false, BLAH},
	{"get_char", 2, bif_iso_get_char_2, "+stream,-integer", true, false, BLAH},
	{"get_code", 1, bif_iso_get_code_1, "-integer", true, false, BLAH},
	{"get_code", 2, bif_iso_get_code_2, "+stream,-integer", true, false, BLAH},
	{"get_byte", 1, bif_iso_get_byte_1, "-integer", true, false, BLAH},
	{"get_byte", 2, bif_iso_get_byte_2, "+stream,-integer", true, false, BLAH},
	{"peek_char", 1, bif_iso_peek_char_1, "-integer", true, false, BLAH},
	{"peek_char", 2, bif_iso_peek_char_2, "+stream,-integer", true, false, BLAH},
	{"peek_code", 1, bif_iso_peek_code_1, "-integer", true, false, BLAH},
	{"peek_code", 2, bif_iso_peek_code_2, "+stream,-integer", true, false, BLAH},
	{"peek_byte", 1, bif_iso_peek_byte_1, "-integer", true, false, BLAH},
	{"peek_byte", 2, bif_iso_peek_byte_2, "+stream,-integer", true, false, BLAH},
	{"current_input", 1, bif_iso_current_input_1, "--stream", true, false, BLAH},
	{"current_output", 1, bif_iso_current_output_1, "--stream", true, false, BLAH},
	{"current_error", 1, bif_iso_current_error_1, "--stream", true, false, BLAH},
	{"set_input", 1, bif_iso_set_input_1, "+stream", true, false, BLAH},
	{"set_output", 1, bif_iso_set_output_1, "+stream", true, false, BLAH},
	{"set_error", 1, bif_iso_set_output_1, "+stream", true, false, BLAH},
	{"stream_property", 2, bif_iso_stream_property_2, "+stream,-compound", true, false, BLAH},


	// Edinburgh...

#if TPL_FEATURE_FILESYSTEM
	{"seeing", 1, bif_edin_seeing_1, "-atom", false, false, BLAH},
	{"telling", 1, bif_edin_telling_1, "-atom", false, false, BLAH},
	{"seen", 0, bif_edin_seen_0, NULL, false, false, BLAH},
	{"told", 0, bif_edin_told_0, NULL, false, false, BLAH},
#endif
	{"redo", 1, bif_edin_redo_1, "+integer", false, false, BLAH},
	{"redo", 2, bif_edin_redo_2, "+stream,+integer", false, false, BLAH},
	{"tab", 1, bif_edin_tab_1, "+integer", false, false, BLAH},
	{"tab", 2, bif_edin_tab_2, "+stream,+integer", false, false, BLAH},
	{"portray_clause", 1, bif_portray_clause_1, "+term", false, false, BLAH},
	{"portray_clause", 2, bif_portray_clause_2, "+stream,+term", false, false, BLAH},

	// Other...

	{"is_stream", 1, bif_is_stream_1, "+term", false, false, BLAH},
	{"unget_char", 1, bif_unget_char_1, "+character", true, false, BLAH},
	{"unget_char", 2, bif_unget_char_2, "+stream,+character", true, false, BLAH},
	{"unget_code", 1, bif_unget_code_1, "+integer", true, false, BLAH},
	{"unget_code", 2, bif_unget_code_2, "+stream,+integer", true, false, BLAH},
	{"unget_byte", 1, bif_unget_byte_1, "+integer", true, false, BLAH},
	{"unget_byte", 2, bif_unget_byte_2, "+stream,+integer", true, false, BLAH},
	{"set_stream", 2, bif_set_stream_2, "+stream,+term", true, false, BLAH},
	{"getline", 1, bif_getline_1, "-atom", false, false, BLAH},
	{"getline", 2, bif_getline_2, "+stream,-string", false, false, BLAH},
	{"getline", 3, bif_getline_3, "+stream,-string,+list", false, false, BLAH},
	{"getlines", 1, bif_getlines_1, "-list", false, false, BLAH},
	{"getlines", 2, bif_getlines_2, "+stream,-list", false, false, BLAH},
	{"getlines", 3, bif_getlines_3, "+stream,-list,+list", false, false, BLAH},
#if TPL_FEATURE_FILESYSTEM
	{"load_files", 2, bif_load_files_2, "+atom,+list", false, false, BLAH},
	{"unload_files", 1, bif_unload_files_1, "+atom", false, false, BLAH},
	{"make", 0, bif_make_0, NULL, false, false, BLAH},
	{"getfile", 2, bif_getfile_2, "+source_sink,-list", false, false, BLAH},
	{"getfile", 3, bif_getfile_3, "+source_sink,-list,+list", false, false, BLAH},
	{"loadfile", 2, bif_loadfile_2, "+source_sink,-atom", false, false, BLAH},
	{"savefile", 2, bif_savefile_2, "+source_sink,+source_sink", false, false, BLAH},
	{"rename_file", 2, bif_rename_file_2, "+source_sink,+source_sink", false, false, BLAH},
	{"copy_file", 2, bif_copy_file_2, "+source_sink,+source_sink", false, false, BLAH},
	{"directory_files", 2, bif_directory_files_2, "+source_sink,-list", false, false, BLAH},
	{"delete_file", 1, bif_delete_file_1, "+source_sink", false, false, BLAH},
	{"exists_file", 1, bif_exists_file_1, "+source_sink", false, false, BLAH},
	{"access_file", 2, bif_access_file_2, "+source_sink,+atom", false, false, BLAH},
	{"time_file", 2, bif_time_file_2, "+source_sink,-float", false, false, BLAH},
	{"size_file", 2, bif_size_file_2, "+source_sink,-integer", false, false, BLAH},
	{"exists_directory", 1, bif_exists_directory_1, "+source_sink", false, false, BLAH},
	{"make_directory", 1, bif_make_directory_1, "+source_sink", false, false, BLAH},
	{"make_directory_path", 1, bif_make_directory_path_1, "+source_sink", false, false, BLAH},
	{"working_directory", 2, bif_working_directory_2, "-atom,+source_sink", false, false, BLAH},
	{"absolute_file_name", 3, bif_absolute_file_name_3, "+source_sink,-atom,+list", false, false, BLAH},
	{"is_absolute_file_name", 1, bif_is_absolute_file_name_1, "+source_sink", false, false, BLAH},
	{"chdir", 1, bif_chdir_1, "+source_sink", false, false, BLAH},
#endif
	{"$put_chars", 1, bif_sys_put_chars_1, "+string", false, false, BLAH},
	{"$put_chars", 2, bif_sys_put_chars_2, "+stream,+string", false, false, BLAH},
	{"read_term_from_atom", 3, bif_read_term_from_atom_3, "+atom,?term,+list", false, false, BLAH},
	{"read_term_from_chars", 3, bif_read_term_from_chars_3, "+string,?term,+list", false, false, BLAH},
	{"$read_term_from_chars", 4, bif_sys_read_term_from_chars_4, "?term,+list,+string,-string", false, false, BLAH},
	{"write_term_to_atom", 3, bif_write_term_to_atom_3, "?atom,?term,+list", false, false, BLAH},
	{"write_canonical_to_atom", 3, bif_write_canonical_to_chars_3, "?atom,?term,+list", false, false, BLAH},
	{"write_term_to_chars", 3, bif_write_term_to_chars_3, "?term,+list,?string", false, false, BLAH},
	{"write_canonical_to_chars", 3, bif_write_canonical_to_chars_3, "?string,?term,+list", false, false, BLAH},
	{"read_line_to_codes", 2, bif_read_line_to_codes_2, "+stream,-list", false, false, BLAH},
	{"read_line_to_string", 2, bif_read_line_to_string_2, "+stream,-string", false, false, BLAH},
#if TPL_FEATURE_FILESYSTEM
	{"read_file_to_string", 3, bif_read_file_to_string_3, "+source_sink,-string,+options", false, false, BLAH},
#endif
	{"alias", 2, bif_alias_2, "+blob,+atom", false, false, BLAH},

#if TPL_FEATURE_FILESYSTEM
	{"$stream_to_file", 2, bif_sys_stream_to_file_2, "+stream,-integer", false, false, BLAH},
#endif
	{"$capture_output", 0, bif_sys_capture_output_0, NULL, false, false, BLAH},
	{"$capture_output_to_chars", 1, bif_sys_capture_output_to_chars_1, "-string", false, false, BLAH},
	{"$capture_output_to_atom", 1, bif_sys_capture_output_to_atom_1, "-atom", false, false, BLAH},
	{"$capture_error", 0, bif_sys_capture_error_0, NULL, false, false, BLAH},
	{"$capture_error_to_chars", 1, bif_sys_capture_error_to_chars_1, "-string", false, false, BLAH},
	{"$capture_error_to_atom", 1, bif_sys_capture_error_to_atom_1, "-atom", false, false, BLAH},
	{"$readline", 2, bif_sys_readline_2, "+string,-string", false, false, BLAH},
	{"$get_chars", 3, bif_sys_get_chars_3, "+stream,+integer,-string", false, false, BLAH},
	{"$bread", 3, bif_sys_bread_3, "+stream,+integer,-string", false, false, BLAH},
	{"$bflush", 1, bif_sys_bflush_1, "+stream", false, false, BLAH},
	{"$bwrite", 2, bif_sys_bwrite_2, "+stream,-string", false, false, BLAH},

	{"$gsl_vector_write", 2, bif_sys_gsl_vector_write_2, "+integer,+stream", false, false, BLAH},
	{"$gsl_vector_alloc", 2, bif_sys_gsl_vector_alloc_2, "+stream,-integer", false, false, BLAH},
	{"$gsl_vector_read", 2, bif_sys_gsl_vector_read_2, "+integer,+stream", false, false, BLAH},
	{"$gsl_vector_size", 2, bif_sys_gsl_vector_size_2, "+integer,-integer", false, false, BLAH},

	{"$gsl_matrix_write", 2, bif_sys_gsl_matrix_write_2, "+integer,+stream", false, false, BLAH},
	{"$gsl_matrix_alloc", 3, bif_sys_gsl_matrix_alloc_3, "+stream,-integer,-integer", false, false, BLAH},
	{"$gsl_matrix_read", 2, bif_sys_gsl_matrix_read_2, "+integer,+stream", false, false, BLAH},
	{"$gsl_matrix_size", 3, bif_sys_gsl_matrix_size_3, "+integer,-integer,-integer", false, false, BLAH},

	{0}
};
