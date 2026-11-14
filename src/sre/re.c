/*
 *
 * Mini regex-module inspired by Rob Pike's regex code described in:
 *
 * http://www.cs.princeton.edu/courses/archive/spr09/cos333/beautiful.html
 *
 *
 *
 * Supports:
 * ---------
 *   '.'        Dot, matches any character
 *   '^'        Start anchor, matches beginning of string
 *   '$'        End anchor, matches end of string
 *   '*'        Asterisk, match zero or more (greedy)
 *   '+'        Plus, match one or more (greedy)
 *   '?'        Question, match zero or one (non-greedy)
 *   '[abc]'    Character class, match if one of {'a', 'b', 'c'}
 *   '[^abc]'   Inverted class, match if NOT one of {'a', 'b', 'c'} -- NOTE: feature is currently broken!
 *   '[a-zA-Z]' Character ranges, the character set of the ranges { a-z | A-Z }
 *   '\s'       Whitespace, \t \f \r \n \v and spaces
 *   '\S'       Non-whitespace
 *   '\w'       Alphanumeric, [a-zA-Z0-9_]
 *   '\W'       Non-alphanumeric
 *   '\d'       Digits, [0-9]
 *   '\D'       Non-digits
 *
 *
 */



#include "re.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <ctype.h>

#include "../allocator.h"

/* Definitions: */

#define MAX_REGEXP_OBJECTS      30    /* Max number of regex symbols in expression. */
#define MAX_CHAR_CLASS_LEN      40    /* Max length of character-class buffer in.   */


enum { UNUSED, DOT, BEGIN, END, QUESTIONMARK, STAR, PLUS, CHAR, CHAR_CLASS, INV_CHAR_CLASS, DIGIT, NOT_DIGIT, ALPHA, NOT_ALPHA, WHITESPACE, NOT_WHITESPACE, /* BRANCH */ };

typedef struct regex_t
{
  unsigned char  type;   /* CHAR, STAR, etc.                      */
  union
  {
    unsigned       cp;   /*      the character itself (code point)*/
    unsigned char* ccl;  /*  OR  a pointer to characters in class */
  } u;
} regex_t;



/* Private function declarations: */
static int matchpattern(regex_t* pattern, const char* text, int* matchlength);
static int matchcharclass(unsigned c, const char* str);
static int matchstar(regex_t p, regex_t* pattern, const char* text, int* matchlength);
static int matchplus(regex_t p, regex_t* pattern, const char* text, int* matchlength);
static int matchone(regex_t p, const char* text, int* n);
static int matchdigit(unsigned c);
static int matchalpha(unsigned c);
static int matchwhitespace(unsigned c);
static int matchmetachar(unsigned c, const char* str);
static int matchrange(unsigned c, const char* str);
static int matchdot(unsigned c);
static int ismetachar(unsigned c);

/* Decode the UTF-8 character at s; a byte that starts no valid sequence is one character above U+10FFFF */
static int utf8_char(const char* s, unsigned* cp)
{
  const unsigned char* u = (const unsigned char*)s;
  int n = u[0] < 0x80 ? 1 : u[0] < 0xC2 ? 0 : u[0] < 0xE0 ? 2 : u[0] < 0xF0 ? 3 : u[0] < 0xF5 ? 4 : 0;
  unsigned c = u[0] & (0x7F >> n);
  int i;

  if (n == 1) { *cp = u[0]; return 1; }
  for (i = 1; i < n; i++)
  {
    if ((u[i] & 0xC0) != 0x80)
      break;
    c = (c << 6) | (u[i] & 0x3F);
  }
  if (!n || (i < n)) { *cp = 0x110000 + u[0]; return 1; }
  *cp = c;
  return n;
}

/* Step back from t to the start of the character that ends there, as utf8_char() split the text going forward */
static const char* utf8_prev(const char* start, const char* t)
{
  unsigned cp;
  int k;
  if ((unsigned char)t[-1] < 0x80)
    return t - 1;
  for (k = 4; k > 1; k--)
    if ((t - start >= k) && (utf8_char(t - k, &cp) == k))
      return t - k;
  return t - 1;
}



/* Public functions: */
int re_match(const char* pattern, const char* text, int* matchlength)
{
  /* re_compile() returns 0 on an invalid pattern and does NOT write
     *buf on those paths, so buf must start NULL. Without this, an
     invalid pattern - e.g. an unterminated char class from
     sre_match/4 - reached TPL_free() with an indeterminate stack value:
     "attempting free on address which was not TPL_malloc()-ed". */
  regex_t *tmp;
  unsigned char *buf = NULL;
  int ok = re_matchp(tmp = re_compile(pattern, &buf), text, matchlength);
  TPL_free(buf);
  TPL_free(tmp);
  return ok;
}

int re_matchp(re_t pattern, const char* text, int* matchlength)
{
  *matchlength = 0;
  if (pattern != 0)
  {
    if (pattern[0].type == BEGIN)
    {
      return ((matchpattern(&pattern[1], text, matchlength)) ? 0 : -1);
    }
    else
    {
      int idx = 0;
      unsigned cp;

      do
      {
        if (matchpattern(pattern, text, matchlength))
        {
          if (text[0] == '\0')
            return -1;

          return idx;
        }
        if (text[0] == '\0')
          break;
        int n = utf8_char(text, &cp);
        text += n;
        idx += n;
      }
      while (1);
    }
  }
  return -1;
}

re_t re_compile(const char* pattern, unsigned char** buf)
{
  /* The sizes of the two static arrays below substantiates the static RAM usage of this module.
     MAX_REGEXP_OBJECTS is the max number of symbols in the expression.
     MAX_CHAR_CLASS_LEN determines the size of buffer for chars in all char-classes in the expression. */
  regex_t re_compiled[MAX_REGEXP_OBJECTS];
  static unsigned char tmp_ccl_buf[MAX_CHAR_CLASS_LEN];
  unsigned char *ccl_buf = TPL_malloc(sizeof(tmp_ccl_buf));
  int ccl_bufidx = 1;

  char c;     /* current char in pattern   */
  int i = 0;  /* index into pattern        */
  int j = 0;  /* index into re_compiled    */

  while (pattern[i] != '\0' && (j+1 < MAX_REGEXP_OBJECTS))
  {
    c = pattern[i];

    switch (c)
    {
      /* Meta-characters: */
      case '^': {    re_compiled[j].type = BEGIN;           } break;
      case '$': {    re_compiled[j].type = END;             } break;
      case '.': {    re_compiled[j].type = DOT;             } break;
      case '*': {    re_compiled[j].type = STAR;            } break;
      case '+': {    re_compiled[j].type = PLUS;            } break;
      case '?': {    re_compiled[j].type = QUESTIONMARK;    } break;
/*    case '|': {    re_compiled[j].type = BRANCH;          } break; <-- not working properly */

      /* Escaped character-classes (\s \w ...): */
      case '\\':
      {
        if (pattern[i+1] != '\0')
        {
          /* Skip the escape-char '\\' */
          i += 1;
          /* ... and check the next */
          switch (pattern[i])
          {
            /* Meta-character: */
            case 'd': {    re_compiled[j].type = DIGIT;            } break;
            case 'D': {    re_compiled[j].type = NOT_DIGIT;        } break;
            case 'w': {    re_compiled[j].type = ALPHA;            } break;
            case 'W': {    re_compiled[j].type = NOT_ALPHA;        } break;
            case 's': {    re_compiled[j].type = WHITESPACE;       } break;
            case 'S': {    re_compiled[j].type = NOT_WHITESPACE;   } break;

            /* Escaped character, e.g. '.' or '$' */
            default:
            {
              re_compiled[j].type = CHAR;
              i += utf8_char(&pattern[i], &re_compiled[j].u.cp) - 1;
            } break;
          }
        }
        /* '\\' as last char in pattern -> invalid regular expression. */
/*
        else
        {
          re_compiled[j].type = CHAR;
          re_compiled[j].ch = pattern[i];
        }
*/
      } break;

      /* Character class: */
      case '[':
      {
        /* Remember where the char-buffer starts. */
        int buf_begin = ccl_bufidx;

        /* Look-ahead to determine if negated */
        if (pattern[i+1] == '^')
        {
          re_compiled[j].type = INV_CHAR_CLASS;
          i += 1; /* Increment i to avoid including '^' in the char-buffer */
          if (pattern[i+1] == 0) /* incomplete pattern, missing non-zero char after '^' */
          {
			TPL_free(ccl_buf);
            *buf = NULL;
            return 0;
          }
        }
        else
        {
          re_compiled[j].type = CHAR_CLASS;
        }

        /* Copy characters inside [..] to buffer */
        while (    (pattern[++i] != ']')
                && (pattern[i]   != '\0')) /* Missing ] */
        {
          if (pattern[i] == '\\')
          {
            if (ccl_bufidx >= MAX_CHAR_CLASS_LEN - 1)
            {
              //fputs("exceeded internal buffer!\n", stderr);
			  TPL_free(ccl_buf);
              *buf = NULL;
              return 0;
            }
            if (pattern[i+1] == 0) /* incomplete pattern, missing non-zero char after '\\' */
            {
			  TPL_free(ccl_buf);
              *buf = NULL;
              return 0;
            }
            ccl_buf[ccl_bufidx++] = pattern[i++];
          }
          else if (ccl_bufidx >= MAX_CHAR_CLASS_LEN)
          {
              //fputs("exceeded internal buffer!\n", stderr);
			  TPL_free(ccl_buf);
              *buf = NULL;
              return 0;
          }
          ccl_buf[ccl_bufidx++] = pattern[i];
        }
        if (ccl_bufidx >= MAX_CHAR_CLASS_LEN)
        {
            /* Catches cases such as [00000000000000000000000000000000000000][ */
            //fputs("exceeded internal buffer!\n", stderr);
			TPL_free(ccl_buf);
            *buf = NULL;
            return 0;
        }
        /* Null-terminate string end */
        ccl_buf[ccl_bufidx++] = 0;
        re_compiled[j].u.ccl = &ccl_buf[buf_begin];
      } break;

      /* Other characters: */
      default:
      {
        re_compiled[j].type = CHAR;
        i += utf8_char(&pattern[i], &re_compiled[j].u.cp) - 1;
      } break;
    }
    /* no buffer-out-of-bounds access on invalid patterns - see https://github.com/kokke/tiny-regex-c/commit/1a279e04014b70b0695fba559a7c05d55e6ee90b */
    if (pattern[i] == 0)
    {
	  TPL_free(ccl_buf);
      *buf = NULL;
      return 0;
    }

    i += 1;
    j += 1;
  }
  /* 'UNUSED' is a sentinel used to indicate end-of-pattern */
  re_compiled[j].type = UNUSED;

  regex_t *tmp = TPL_malloc(sizeof(re_compiled));
  memcpy(tmp, re_compiled, sizeof(re_compiled));
  *buf = ccl_buf;
  return (re_t) tmp;
}

void re_print(regex_t* pattern)
{
  const char* types[] = { "UNUSED", "DOT", "BEGIN", "END", "QUESTIONMARK", "STAR", "PLUS", "CHAR", "CHAR_CLASS", "INV_CHAR_CLASS", "DIGIT", "NOT_DIGIT", "ALPHA", "NOT_ALPHA", "WHITESPACE", "NOT_WHITESPACE", "BRANCH" };

  int i;
  int j;
  char c;
  for (i = 0; i < MAX_REGEXP_OBJECTS; ++i)
  {
    if (pattern[i].type == UNUSED)
    {
      break;
    }

    printf("type: %s", types[pattern[i].type]);
    if (pattern[i].type == CHAR_CLASS || pattern[i].type == INV_CHAR_CLASS)
    {
      printf(" [");
      for (j = 0; j < MAX_CHAR_CLASS_LEN; ++j)
      {
        c = pattern[i].u.ccl[j];
        if ((c == '\0') || (c == ']'))
        {
          break;
        }
        printf("%c", c);
      }
      printf("]");
    }
    else if (pattern[i].type == CHAR)
    {
      printf(" U+%04X", pattern[i].u.cp);
    }
    printf("\n");
  }
}



/* Private functions: */
static int matchdigit(unsigned c)
{
  return (c < 0x80) && isdigit(c);  /* ASCII only */
}
static int matchalpha(unsigned c)
{
  return (c < 0x80) && isalpha(c);  /* ASCII only */
}
static int matchwhitespace(unsigned c)
{
  return (c < 0x80) && isspace(c);  /* ASCII only */
}
static int matchalphanum(unsigned c)
{
  return ((c == '_') || matchalpha(c) || matchdigit(c));
}
static int matchrange(unsigned c, const char* str)
{
  unsigned lo, hi;
  int n = utf8_char(str, &lo);
  if ((c == '-') || (str[0] == '\0') || (str[0] == '-') || (str[n] != '-') || (str[n+1] == '\0'))
    return 0;
  utf8_char(&str[n+1], &hi);
  return (c >= lo) && (c <= hi);
}
static int matchdot(unsigned c)
{
#if defined(RE_DOT_MATCHES_NEWLINE) && (RE_DOT_MATCHES_NEWLINE == 1)
  (void)c;
  return 1;
#else
  return c != '\n' && c != '\r';
#endif
}
static int ismetachar(unsigned c)
{
  return ((c == 's') || (c == 'S') || (c == 'w') || (c == 'W') || (c == 'd') || (c == 'D'));
}

static int matchmetachar(unsigned c, const char* str)
{
  unsigned sc;
  switch (str[0])
  {
    case 'd': return  matchdigit(c);
    case 'D': return !matchdigit(c);
    case 'w': return  matchalphanum(c);
    case 'W': return !matchalphanum(c);
    case 's': return  matchwhitespace(c);
    case 'S': return !matchwhitespace(c);
    default:  utf8_char(str, &sc); return (c == sc);
  }
}

static int matchcharclass(unsigned c, const char* str)
{
  unsigned sc;
  int n;
  do
  {
    n = utf8_char(str, &sc);
    if (matchrange(c, str))
    {
      return 1;
    }
    else if (str[0] == '\\')
    {
      /* Escape-char: increment str-ptr and match on next char */
      str += 1;
      n = utf8_char(str, &sc);
      if (matchmetachar(c, str))
      {
        return 1;
      }
      else if ((c == sc) && !ismetachar(c))
      {
        return 1;
      }
    }
    else if (c == sc)
    {
      if (c == '-')
      {
        return ((str[-1] == '\0') || (str[1] == '\0'));
      }
      else
      {
        return 1;
      }
    }
    str += n - 1;
  }
  while (*str++ != '\0');

  return 0;
}

/* Match the character at text (not its end), whose length goes in *n */
static int matchone(regex_t p, const char* text, int* n)
{
  unsigned c;
  *n = utf8_char(text, &c);
  switch (p.type)
  {
    case DOT:            return matchdot(c);
    case CHAR_CLASS:     return  matchcharclass(c, (const char*)p.u.ccl);
    case INV_CHAR_CLASS: return !matchcharclass(c, (const char*)p.u.ccl);
    case DIGIT:          return  matchdigit(c);
    case NOT_DIGIT:      return !matchdigit(c);
    case ALPHA:          return  matchalphanum(c);
    case NOT_ALPHA:      return !matchalphanum(c);
    case WHITESPACE:     return  matchwhitespace(c);
    case NOT_WHITESPACE: return !matchwhitespace(c);
    default:             return  (p.u.cp == c);
  }
}

static int matchstar(regex_t p, regex_t* pattern, const char* text, int* matchlength)
{
  int prelen = *matchlength;
  const char* prepoint = text;
  int n;
  while ((text[0] != '\0') && matchone(p, text, &n))
    text += n;
  while (1)
  {
    *matchlength = prelen + (int)(text - prepoint);
    if (matchpattern(pattern, text, matchlength))
      return 1;
    if (text == prepoint)
      break;
    text = utf8_prev(prepoint, text);
  }

  *matchlength = prelen;
  return 0;
}

static int matchplus(regex_t p, regex_t* pattern, const char* text, int* matchlength)
{
  int prelen = *matchlength;
  const char* prepoint = text;
  int n;
  while ((text[0] != '\0') && matchone(p, text, &n))
    text += n;
  while (text > prepoint)
  {
    *matchlength = prelen + (int)(text - prepoint);
    if (matchpattern(pattern, text, matchlength))
      return 1;
    text = utf8_prev(prepoint, text);
  }

  *matchlength = prelen;
  return 0;
}

static int matchquestion(regex_t p, regex_t* pattern, const char* text, int* matchlength)
{
  if (p.type == UNUSED)
    return 1;
  if (matchpattern(pattern, text, matchlength))
      return 1;
  int n;
  if (*text && matchone(p, text, &n))
  {
    if (matchpattern(pattern, text + n, matchlength))
    {
      *matchlength += n;
      return 1;
    }
  }
  return 0;
}


#if 0

/* Recursive matching */
static int matchpattern(regex_t* pattern, const char* text, int *matchlength)
{
  int pre = *matchlength;
  if ((pattern[0].type == UNUSED) || (pattern[1].type == QUESTIONMARK))
  {
    return matchquestion(pattern[1], &pattern[2], text, matchlength);
  }
  else if (pattern[1].type == STAR)
  {
    return matchstar(pattern[0], &pattern[2], text, matchlength);
  }
  else if (pattern[1].type == PLUS)
  {
    return matchplus(pattern[0], &pattern[2], text, matchlength);
  }
  else if ((pattern[0].type == END) && pattern[1].type == UNUSED)
  {
    return text[0] == '\0';
  }
  else if ((text[0] != '\0') && matchone(pattern[0], text[0]))
  {
    (*matchlength)++;
    return matchpattern(&pattern[1], text+1);
  }
  else
  {
    *matchlength = pre;
    return 0;
  }
}

#else

/* Iterative matching; a failure restores *matchlength, or the next attempt starts from a stale count */
static int matchpattern(regex_t* pattern, const char* text, int* matchlength)
{
  int pre = *matchlength;
  do
  {
    if ((pattern[0].type == UNUSED) || (pattern[1].type == QUESTIONMARK))
    {
      if (matchquestion(pattern[0], &pattern[2], text, matchlength))
        return 1;
      break;
    }
    else if (pattern[1].type == STAR)
    {
      if (matchstar(pattern[0], &pattern[2], text, matchlength))
        return 1;
      break;
    }
    else if (pattern[1].type == PLUS)
    {
      if (matchplus(pattern[0], &pattern[2], text, matchlength))
        return 1;
      break;
    }
    else if ((pattern[0].type == END) && pattern[1].type == UNUSED)
    {
      if (text[0] == '\0')
        return 1;
      break;
    }
/*  Branching is not working properly
    else if (pattern[1].type == BRANCH)
    {
      return (matchpattern(pattern, text) || matchpattern(&pattern[2], text));
    }
*/
    int n;
    if ((text[0] == '\0') || !matchone(pattern[0], text, &n))
      break;
    pattern++;
    text += n;
    *matchlength += n;
  }
  while (1);

  *matchlength = pre;
  return 0;
}

#endif
