#pragma once

#include <stdio.h>
#include <string.h>

#include "tpl_features.h"

// A dynamically allocating string buffer.
//
// tmpbuf is a small-buffer optimisation: nothing shorter than this ever
// allocates. It is embedded in a stream, a query and a parser, and 32 more
// live on the stack behind SB(), so the size is a straight trade of memory
// against malloc traffic - and a freestanding build wants that trade the
// other way round, on a board where a fixed stack has no guard page.

#if TPL_FREESTANDING
#define SB_LEN 256			// Initial size
#else
#define SB_LEN 1024			// Initial size
#endif

typedef struct {
	char tmpbuf[SB_LEN];	// No allocs if less than this
	char *buf, *dst;
	size_t buf_size;
	bool oom;				// A try-append hit a failed allocation
} stringbuf;

static inline bool sb_try_grow(stringbuf *sb, size_t len)
{
	size_t offset = sb->dst - sb->buf;

	if (len < (sb->buf_size - offset))
		return true;

	if (len > ((size_t)-1 - sb->buf_size - 1024)) {
		sb->oom = true;
		return false;
	}

	size_t new_size = sb->buf_size + len + 1024;
	char *buf = sb->buf == sb->tmpbuf
		? TPL_malloc(new_size)
		: TPL_realloc(sb->buf, new_size);

	if (!buf) {
		sb->oom = true;
		return false;
	}

	if (sb->buf == sb->tmpbuf)
		memcpy(buf, sb->tmpbuf, offset+1);

	sb->buf = buf;
	sb->dst = buf + offset;
	sb->buf_size = new_size;
	return true;
}

static inline bool sb_try_strcatn(stringbuf *sb, const char *src, size_t len)
{
	if (!sb_try_grow(sb, len))
		return false;

	memcpy(sb->dst, src, len);
	sb->dst += len;
	*sb->dst = '\0';
	return true;
}

static inline bool sb_try_strcat(stringbuf *sb, const char *src)
{
	return sb_try_strcatn(sb, src, strlen(src));
}

// Step back over the last byte written. Never grows, so never fails.

static inline void sb_unget(stringbuf *sb)
{
	if (sb->dst != sb->buf)
		*--sb->dst = '\0';
}

static inline bool sb_try_putchar(stringbuf *sb, int ch)
{
	if (!sb_try_grow(sb, MAX_BYTES_PER_CODEPOINT+1))
		return false;

	sb->dst += put_char_utf8(sb->dst, ch);
	*sb->dst = '\0';
	return true;
}

#define SB(pr) stringbuf pr##_buf; SB_init(pr);

#define SB_init(pr) {											\
	pr##_buf.buf_size = sizeof(pr##_buf.tmpbuf);				\
	pr##_buf.buf = pr##_buf.tmpbuf;								\
	pr##_buf.dst = pr##_buf.buf;								\
	pr##_buf.oom = false;										\
	if (pr##_buf.buf) pr##_buf.dst[0] = '\0';					\
}

#define SB_alloc(pr,len) stringbuf pr##_buf; 					\
	pr##_buf.buf_size = len;									\
	pr##_buf.buf = TPL_malloc((len)+1024+1);								\
	ENSURE(pr##_buf.buf);										\
	pr##_buf.dst = pr##_buf.buf;								\
	pr##_buf.oom = false;										\
	*pr##_buf.dst = '\0';

#define SB_check(pr,len) {												\
	if ((pr##_buf.dst + (len)) >= (pr##_buf.buf + pr##_buf.buf_size)) {	\
		size_t offset = SB_strlen(pr);									\
		if (pr##_buf.buf != pr##_buf.tmpbuf) {							\
			pr##_buf.buf = TPL_realloc(pr##_buf.buf, pr##_buf.buf_size += (len + 1024));	\
			ENSURE(pr##_buf.buf);										\
		} else {														\
			pr##_buf.buf = TPL_malloc(pr##_buf.buf_size += (len + 1024));	\
			ENSURE(pr##_buf.buf);										\
			memcpy(pr##_buf.buf, pr##_buf.tmpbuf, offset+1);			\
		}																\
		pr##_buf.dst = pr##_buf.buf + offset;							\
	}																	\
}

#define SB_strlen(pr) (pr##_buf.dst - pr##_buf.buf)
#define SB_strlen_utf8(pr) strlen_utf8(pr##_buf.buf)

#define SB_truncate(pr,len) {									\
	const char *src = pr##_buf.buf;								\
	for (unsigned i = 0; i < len; i++) {						\
		int ch = get_char_utf8(&src);							\
		if (!ch) break;											\
	}															\
}

#define SB_trim(pr,ch) {										\
	if (SB_strlen(pr)) {										\
		if (pr##_buf.dst[-1] == (ch)) 							\
			*--pr##_buf.dst = '\0';								\
	}															\
}

#define SB_trim_all(pr,ch) {									\
	while (SB_strlen(pr)) {										\
		if (pr##_buf.dst[-1] != (ch)) 							\
			break;												\
		*--pr##_buf.dst = '\0';									\
	}															\
}

#define SB_trim_ws(pr) {										\
	while (SB_strlen(pr)) {										\
		if (!isspace(pr##_buf.dst[-1]))							\
			break;												\
		*--pr##_buf.dst = '\0';									\
	}															\
}

#define SB_strcpy(pr,s) {										\
	size_t len = strlen(s);										\
	SB_check(pr, len);											\
	pr##_buf.dst = pr##_buf.buf;								\
	SB_strcatn(pr,s,len);										\
}

#define SB_strcpy_and_free(pr,s) {								\
	char *s2 = (s);												\
	if (s2) {													\
		SB_strcpy(pr, s2);										\
		TPL_free(s2);												\
	}															\
}

#define SB_strcat(pr,s) SB_strcatn(pr,s,strlen(s))

#define SB_strcatn(pr,s,len) {									\
	SB_check(pr, len);											\
	memcpy(pr##_buf.dst, s, len);								\
	pr##_buf.dst += len;										\
	*pr##_buf.dst = '\0';										\
}

#define SB_strcat_and_free(pr,s) {								\
	char *s2 = (s);												\
	if (s2) {													\
		SB_strcat(pr, s2);										\
		TPL_free(s2);												\
	}															\
}

#define SB_fwrite(pr,ptr,buf_size) {							\
	size_t len = buf_size;										\
	SB_check(pr, len);											\
	memcpy(pr##_buf.dst, ptr, len);								\
	pr##_buf.dst += len;										\
	*pr##_buf.dst = '\0';										\
}

#define SB_sprintf(pr,fmt,...) {								\
	size_t len = snprintf(NULL, 0, fmt, __VA_ARGS__);			\
	SB_check(pr, len);											\
	snprintf(pr##_buf.dst, (pr##_buf.buf_size-(pr##_buf.dst-pr##_buf.buf))+1, fmt, __VA_ARGS__);\
	pr##_buf.dst += len;										\
	*pr##_buf.dst = '\0';										\
}

#define SB_ungetchar(pr) {										\
	SB_check(pr, 6);											\
	if (pr##_buf.dst != pr##_buf.buf) pr##_buf.dst--;			\
}

#define SB_putchar(pr,ch) {										\
	SB_check(pr, MAX_BYTES_PER_CODEPOINT+1);											\
	pr##_buf.dst += put_char_utf8(pr##_buf.dst, ch);			\
	*pr##_buf.dst = '\0'; 										\
}

// Drop everything past an offset taken earlier with SB_strlen()...

#define SB_setlen(pr,n) {										\
	pr##_buf.dst = pr##_buf.buf + (n);							\
	*pr##_buf.dst = '\0';										\
}

#define SB_try_strcatn(pr,s,len) sb_try_strcatn(&pr##_buf, s, len)
#define SB_try_strcat(pr,s) sb_try_strcat(&pr##_buf, s)
#define SB_unget(pr) sb_unget(&pr##_buf)
#define SB_oom(pr) (pr##_buf.oom)
#define SB_try_putchar(pr,ch) sb_try_putchar(&pr##_buf, ch)

#define SB_cstr(pr) (const char*) pr##_buf.buf ? pr##_buf.buf : ""
#define SB_strcmp(pr,s) strcmp(pr##_buf.buf?pr##_buf.buf:"", s)

#define SB_free(pr) {											\
	if (pr##_buf.buf != pr##_buf.tmpbuf)						\
		TPL_free(pr##_buf.buf);										\
	SB_init(pr);												\
}
