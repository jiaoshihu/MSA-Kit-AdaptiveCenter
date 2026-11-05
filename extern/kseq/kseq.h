/* The MIT License
   Copyright (c) 2008, 2009, 2011 Attractive Chaos <attractor@live.co.uk>
   Permission is hereby granted, free of charge, to any person obtaining
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
   MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
   NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
   BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
   ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
   CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
   SOFTWARE.
*/

/* Last Modified upstream: 30JAN2023
 * MSA-Kit safety patch: checked allocation/length arithmetic, partial-init
 * cleanup, and propagation of read/expansion errors. Normal record parsing
 * follows HAlign-5 b0d5608811d44e30bc323da54f0583ceaaf684d2/include/kseq.h.
 * KSEQ_* allocation hooks are compile-time test seams, not runtime options.
 */

#ifndef AC_KSEQ_H
#define AC_KSEQ_H

#include <ctype.h>
#include <limits.h>
#include <string.h>
#include <stdlib.h>

#ifndef KSEQ_MALLOC
#define KSEQ_MALLOC malloc
#endif
#ifndef KSEQ_CALLOC
#define KSEQ_CALLOC calloc
#endif
#ifndef KSEQ_REALLOC
#define KSEQ_REALLOC realloc
#endif
#ifndef KSEQ_FREE
#define KSEQ_FREE free
#endif

#define KS_SEP_SPACE 0 /* isspace(): \t, \n, \v, \f, \r */
#define KS_SEP_TAB   1 /* isspace() && !' ' */
#define KS_SEP_LINE  2 /* "\n" or "\r\n" */
#define KS_SEP_MAX   2

#define __KS_TYPE(type_t)                                          \
    typedef struct __kstream_t {                                  \
        unsigned char *buf;                                       \
        int begin, end, is_eof;                                   \
        type_t f;                                                \
    } kstream_t;

#define ks_err(ks) ((ks)->end == -1)
#define ks_eof(ks) ((ks)->is_eof && (ks)->begin >= (ks)->end)
#define ks_rewind(ks) ((ks)->is_eof = (ks)->begin = (ks)->end = 0)

#define __KS_BASIC(type_t, __bufsize)                              \
    static inline kstream_t *ks_init(type_t f)                     \
    {                                                            \
        kstream_t *ks = (kstream_t*)KSEQ_CALLOC(1, sizeof(kstream_t)); \
        if (!ks) return 0;                                        \
        ks->f = f;                                               \
        ks->buf = (unsigned char*)KSEQ_MALLOC(__bufsize);           \
        if (!ks->buf) { KSEQ_FREE(ks); return 0; }                 \
        return ks;                                               \
    }                                                            \
    static inline void ks_destroy(kstream_t *ks)                  \
    {                                                            \
        if (ks) { KSEQ_FREE(ks->buf); KSEQ_FREE(ks); }              \
    }

#define __KS_GETC(__read, __bufsize)                               \
    static inline int ks_getc(kstream_t *ks)                       \
    {                                                            \
        if (ks_err(ks)) return -3;                                \
        if (ks->is_eof && ks->begin >= ks->end) return -1;          \
        if (ks->begin >= ks->end) {                               \
            ks->begin = 0;                                       \
            ks->end = __read(ks->f, ks->buf, __bufsize);            \
            if (ks->end == 0) { ks->is_eof = 1; return -1; }       \
            if (ks->end < 0 || ks->end > (__bufsize)) {            \
                ks->end = -1; ks->is_eof = 1; return -3;           \
            }                                                    \
        }                                                        \
        return (int)ks->buf[ks->begin++];                          \
    }

#ifndef KSTRING_T
#define KSTRING_T kstring_t
typedef struct __kstring_t {
    size_t l, m;
    char *s;
} kstring_t;
#endif

/* kseq's public lengths are int. Check BEFORE addition or narrowing, and
 * preserve the owning pointer and capacity if realloc fails. */
static inline int kseq_reserve(kstring_t *str, size_t additional)
{
    const size_t limit = (size_t)INT_MAX;
    size_t needed, capacity;
    char *replacement;
    if (str->l > limit || additional > limit - str->l) return -4;
    needed = str->l + additional + 1;
    if (str->m >= needed) return 0;
    capacity = str->m ? str->m : 1;
    while (capacity < needed) {
        if (capacity > (limit + 1) / 2) capacity = limit + 1;
        else capacity *= 2;
    }
    replacement = (char*)KSEQ_REALLOC(str->s, capacity);
    if (!replacement) return -4;
    str->s = replacement;
    str->m = capacity;
    return 0;
}

#define __KS_GETUNTIL(__read, __bufsize)                           \
    static int ks_getuntil2(kstream_t *ks, int delimiter,          \
                            kstring_t *str, int *dret, int append) \
    {                                                            \
        int gotany = 0;                                          \
        if (dret) *dret = 0;                                      \
        str->l = append ? str->l : 0;                             \
        if (str->l > (size_t)INT_MAX) return -4;                   \
        while (1) {                                              \
            int i;                                               \
            if (ks_err(ks)) return -3;                            \
            if (ks->begin >= ks->end) {                           \
                if (!ks->is_eof) {                               \
                    ks->begin = 0;                               \
                    ks->end = __read(ks->f, ks->buf, __bufsize);    \
                    if (ks->end == 0) { ks->is_eof = 1; break; }   \
                    if (ks->end < 0 || ks->end > (__bufsize)) {    \
                        ks->end = -1; ks->is_eof = 1; return -3;   \
                    }                                            \
                } else break;                                    \
            }                                                    \
            if (delimiter == KS_SEP_LINE) {                       \
                for (i = ks->begin; i < ks->end; ++i)              \
                    if (ks->buf[i] == '\n') break;                \
            } else if (delimiter > KS_SEP_MAX) {                  \
                for (i = ks->begin; i < ks->end; ++i)              \
                    if (ks->buf[i] == delimiter) break;           \
            } else if (delimiter == KS_SEP_SPACE) {               \
                for (i = ks->begin; i < ks->end; ++i)              \
                    if (isspace(ks->buf[i])) break;               \
            } else if (delimiter == KS_SEP_TAB) {                 \
                for (i = ks->begin; i < ks->end; ++i)              \
                    if (isspace(ks->buf[i]) && ks->buf[i] != ' ')  \
                        break;                                   \
            } else i = 0; /* never come here */                   \
            if (kseq_reserve(str, (size_t)(i - ks->begin)) < 0)    \
                return -4;                                       \
            gotany = 1;                                          \
            memcpy(str->s + str->l, ks->buf + ks->begin,           \
                   (size_t)(i - ks->begin));                      \
            str->l += (size_t)(i - ks->begin);                    \
            ks->begin = i + 1;                                   \
            if (i < ks->end) {                                   \
                if (dret) *dret = ks->buf[i];                      \
                break;                                           \
            }                                                    \
        }                                                        \
        if (!gotany && ks_eof(ks)) return -1;                      \
        if (kseq_reserve(str, 0) < 0) return -4;                   \
        if (delimiter == KS_SEP_LINE && str->l > 1 &&              \
            str->s[str->l-1] == '\r') --str->l;                    \
        str->s[str->l] = '\0';                                    \
        return (int)str->l;                                       \
    }                                                            \
    static inline int ks_getuntil(kstream_t *ks, int delimiter,   \
                                  kstring_t *str, int *dret)      \
    { return ks_getuntil2(ks, delimiter, str, dret, 0); }

#define KSTREAM_INIT(type_t, __read, __bufsize)                    \
    __KS_TYPE(type_t)                                            \
    __KS_BASIC(type_t, __bufsize)                                 \
    __KS_GETC(__read, __bufsize)                                  \
    __KS_GETUNTIL(__read, __bufsize)

#define kseq_rewind(ks) ((ks)->last_char = (ks)->f->is_eof = (ks)->f->begin = (ks)->f->end = 0)

#define __KSEQ_BASIC(SCOPE, type_t)                               \
    SCOPE kseq_t *kseq_init(type_t fd)                            \
    {                                                           \
        kseq_t *s = (kseq_t*)KSEQ_CALLOC(1, sizeof(kseq_t));        \
        if (!s) return 0;                                        \
        s->f = ks_init(fd);                                      \
        if (!s->f) { KSEQ_FREE(s); return 0; }                    \
        return s;                                               \
    }                                                           \
    SCOPE void kseq_destroy(kseq_t *ks)                           \
    {                                                           \
        if (!ks) return;                                         \
        KSEQ_FREE(ks->name.s); KSEQ_FREE(ks->comment.s);            \
        KSEQ_FREE(ks->seq.s); KSEQ_FREE(ks->qual.s);                \
        ks_destroy(ks->f);                                       \
        KSEQ_FREE(ks);                                           \
    }

/* Return value:
   >=0  length of the sequence (normal)
   -1   end-of-file
   -2   truncated/mismatched quality string
   -3   error reading stream
   -4   allocation failure or length outside the int return range
 */
#define __KSEQ_READ(SCOPE)                                       \
    SCOPE int kseq_read(kseq_t *seq)                              \
    {                                                           \
        int c, r;                                               \
        kstream_t *ks = seq->f;                                  \
        if (seq->last_char == 0) {                               \
            while ((c = ks_getc(ks)) >= 0 && c != '>' && c != '@'); \
            if (c < 0) return c;                                 \
            seq->last_char = c;                                 \
        }                                                       \
        seq->comment.l = seq->seq.l = seq->qual.l = 0;            \
        if ((r = ks_getuntil(ks, 0, &seq->name, &c)) < 0) return r; \
        if (c != '\n') {                                        \
            r = ks_getuntil(ks, KS_SEP_LINE, &seq->comment, 0);    \
            if (r < -1) return r;                                \
        }                                                       \
        if (seq->seq.s == 0) {                                   \
            seq->seq.s = (char*)KSEQ_MALLOC(256);                 \
            if (!seq->seq.s) return -4;                          \
            seq->seq.m = 256;                                    \
        }                                                       \
        while ((c = ks_getc(ks)) >= 0 && c != '>' && c != '+' && c != '@') { \
            if (c == '\n') continue;                            \
            if (kseq_reserve(&seq->seq, 1) < 0) return -4;        \
            seq->seq.s[seq->seq.l++] = (char)c;                   \
            r = ks_getuntil2(ks, KS_SEP_LINE, &seq->seq, 0, 1);    \
            if (r < -1) return r;                                \
        }                                                       \
        if (c < -1) return c;                                    \
        if (c == '>' || c == '@') seq->last_char = c;             \
        if (kseq_reserve(&seq->seq, 0) < 0) return -4;             \
        seq->seq.s[seq->seq.l] = 0;                              \
        if (c != '+') return (int)seq->seq.l;                      \
        if (seq->qual.m < seq->seq.m) {                           \
            char *replacement = (char*)KSEQ_REALLOC(seq->qual.s, seq->seq.m); \
            if (!replacement) return -4;                         \
            seq->qual.s = replacement;                           \
            seq->qual.m = seq->seq.m;                            \
        }                                                       \
        while ((c = ks_getc(ks)) >= 0 && c != '\n');               \
        if (c < -1) return c;                                    \
        if (c == -1) return -2;                                  \
        do {                                                    \
            c = ks_getuntil2(ks, KS_SEP_LINE, &seq->qual, 0, 1);   \
        } while (c >= 0 && seq->qual.l < seq->seq.l);              \
        if (c < -1) return c;                                    \
        seq->last_char = 0;                                      \
        if (seq->seq.l != seq->qual.l) return -2;                  \
        return (int)seq->seq.l;                                  \
    }

#define __KSEQ_TYPE(type_t)                                      \
    typedef struct {                                            \
        kstring_t name, comment, seq, qual;                       \
        int last_char;                                          \
        kstream_t *f;                                            \
    } kseq_t;

#define KSEQ_INIT2(SCOPE, type_t, __read)                          \
    KSTREAM_INIT(type_t, __read, 16384)                           \
    __KSEQ_TYPE(type_t)                                          \
    __KSEQ_BASIC(SCOPE, type_t)                                   \
    __KSEQ_READ(SCOPE)

#define KSEQ_INIT(type_t, __read) KSEQ_INIT2(static, type_t, __read)

#define KSEQ_DECLARE(type_t)                                     \
    __KS_TYPE(type_t)                                            \
    __KSEQ_TYPE(type_t)                                          \
    extern kseq_t *kseq_init(type_t fd);                          \
    void kseq_destroy(kseq_t *ks);                                \
    int kseq_read(kseq_t *seq);

#endif
