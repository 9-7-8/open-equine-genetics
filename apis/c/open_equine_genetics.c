/* Open Equine Genetics: see open_equine_genetics.h. C99, standard library only. */

#if defined(_WIN32) && !defined(_CRT_SECURE_NO_WARNINGS)
#define _CRT_SECURE_NO_WARNINGS
#endif

#include "open_equine_genetics.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <windows.h>
#else
#include <dirent.h>
#endif

/* ---- Arena: everything a database holds is freed at once ---- */

typedef struct block {
    struct block *next;
    size_t used, size;
} block;

typedef union { long double ld; void *p; long long ll; double d; } max_align;
#define ALIGN sizeof(max_align)
#define HEADER (((sizeof(block) + ALIGN - 1) / ALIGN) * ALIGN)
#define BLOCK_SIZE 65536

typedef struct {
    block *head;
} arena;

static void *arena_alloc(arena *a, size_t n)
{
    block *b = a->head;
    char *p;
    n = ((n + ALIGN - 1) / ALIGN) * ALIGN;
    if (n == 0)
        n = ALIGN;
    if (b == NULL || b->size - b->used < n) {
        size_t size = n > BLOCK_SIZE ? n : BLOCK_SIZE;
        b = (block *)malloc(HEADER + size);
        if (b == NULL)
            return NULL;
        b->used = 0;
        b->size = size;
        if (n > BLOCK_SIZE && a->head != NULL) {
            /* a large one goes behind the current block, which still has room */
            b->next = a->head->next;
            a->head->next = b;
        } else {
            b->next = a->head;
            a->head = b;
        }
    }
    p = (char *)b + HEADER + b->used;
    b->used += n;
    memset(p, 0, n);
    return p;
}

static void arena_free(arena *a)
{
    while (a->head != NULL) {
        block *next = a->head->next;
        free(a->head);
        a->head = next;
    }
}

/* ---- JSON ---- */

typedef enum { J_NULL, J_FALSE, J_TRUE, J_NUM, J_STR, J_ARR, J_OBJ } jtype;

typedef struct jval {
    jtype type;
    double num;
    const char *str;
    size_t count;               /* items of an array or object */
    struct jval *items;
    const char **keys;          /* an object's keys, in file order */
} jval;

typedef struct {
    const char *p;
    arena *a;
    const char *error;
    int depth;
} jparser;

static void skip_ws(jparser *j)
{
    while (*j->p == ' ' || *j->p == '\t' || *j->p == '\n' || *j->p == '\r')
        j->p++;
}

static int fail(jparser *j, const char *msg)
{
    if (j->error == NULL)
        j->error = msg;
    return 0;
}

static int hex4(const char *p, unsigned *out)
{
    unsigned v = 0;
    int i;
    for (i = 0; i < 4; i++) {
        char c = p[i];
        v <<= 4;
        if (c >= '0' && c <= '9') v |= (unsigned)(c - '0');
        else if (c >= 'a' && c <= 'f') v |= (unsigned)(c - 'a' + 10);
        else if (c >= 'A' && c <= 'F') v |= (unsigned)(c - 'A' + 10);
        else return 0;
    }
    *out = v;
    return 1;
}

static char *put_utf8(char *o, unsigned c)
{
    if (c < 0x80) {
        *o++ = (char)c;
    } else if (c < 0x800) {
        *o++ = (char)(0xC0 | (c >> 6));
        *o++ = (char)(0x80 | (c & 0x3F));
    } else if (c < 0x10000) {
        *o++ = (char)(0xE0 | (c >> 12));
        *o++ = (char)(0x80 | ((c >> 6) & 0x3F));
        *o++ = (char)(0x80 | (c & 0x3F));
    } else {
        *o++ = (char)(0xF0 | (c >> 18));
        *o++ = (char)(0x80 | ((c >> 12) & 0x3F));
        *o++ = (char)(0x80 | ((c >> 6) & 0x3F));
        *o++ = (char)(0x80 | (c & 0x3F));
    }
    return o;
}

/* Reads a string at j->p (on the opening quote). Decoded text is never longer than the
   escaped text, so one allocation of that length is enough. */
static int parse_string(jparser *j, const char **out)
{
    const char *s = ++j->p, *e = s;
    char *o, *buf;
    while (*e != '"') {
        if (*e == '\0')
            return fail(j, "unterminated string");
        if ((unsigned char)*e < 0x20)
            return fail(j, "control character in string");
        if (*e == '\\' && e[1] != '\0')
            e++;
        e++;
    }
    buf = o = (char *)arena_alloc(j->a, (size_t)(e - s) + 1);
    if (buf == NULL)
        return fail(j, "out of memory");
    while (j->p < e) {
        char c = *j->p++;
        unsigned u, lo;
        if (c != '\\') {
            *o++ = c;
            continue;
        }
        c = *j->p++;
        switch (c) {
        case '"': *o++ = '"'; break;
        case '\\': *o++ = '\\'; break;
        case '/': *o++ = '/'; break;
        case 'b': *o++ = '\b'; break;
        case 'f': *o++ = '\f'; break;
        case 'n': *o++ = '\n'; break;
        case 'r': *o++ = '\r'; break;
        case 't': *o++ = '\t'; break;
        case 'u':
            if (e - j->p < 4 || !hex4(j->p, &u))
                return fail(j, "bad \\u escape");
            j->p += 4;
            if (u >= 0xD800 && u < 0xDC00 && e - j->p >= 6 && j->p[0] == '\\' && j->p[1] == 'u'
                && hex4(j->p + 2, &lo) && lo >= 0xDC00 && lo < 0xE000) {
                u = 0x10000 + ((u - 0xD800) << 10) + (lo - 0xDC00);
                j->p += 6;
            }
            o = put_utf8(o, u);
            break;
        default:
            return fail(j, "bad escape");
        }
    }
    *o = '\0';
    j->p = e + 1;
    *out = buf;
    return 1;
}

static int parse_value(jparser *j, jval *v);

/* Parses an array or object. Items are gathered in a growing buffer, then copied into the
   arena once their count is known. */
static int parse_container(jparser *j, jval *v, int object)
{
    char close = object ? '}' : ']';
    jval *items = NULL;
    const char **keys = NULL;
    size_t n = 0, cap = 0;
    int ok = 0;
    if (++j->depth > 256)
        return fail(j, "nested too deeply");
    j->p++;
    skip_ws(j);
    if (*j->p == close) {
        j->p++;
        ok = 1;
    }
    while (!ok) {
        if (n == cap) {
            size_t ncap = cap ? cap * 2 : 8;
            jval *ni = (jval *)realloc(items, ncap * sizeof *ni);
            if (ni == NULL) { fail(j, "out of memory"); break; }
            items = ni;
            if (object) {
                const char **nk = (const char **)realloc((void *)keys, ncap * sizeof *nk);
                if (nk == NULL) { fail(j, "out of memory"); break; }
                keys = nk;
            }
            cap = ncap;
        }
        skip_ws(j);
        if (object) {
            if (*j->p != '"') { fail(j, "expected a key"); break; }
            if (!parse_string(j, &keys[n])) break;
            skip_ws(j);
            if (*j->p != ':') { fail(j, "expected ':'"); break; }
            j->p++;
        }
        if (!parse_value(j, &items[n])) break;
        n++;
        skip_ws(j);
        if (*j->p == ',') {
            j->p++;
        } else if (*j->p == close) {
            j->p++;
            ok = 1;
        } else {
            fail(j, object ? "expected ',' or '}'" : "expected ',' or ']'");
            break;
        }
    }
    if (ok && n > 0) {
        v->items = (jval *)arena_alloc(j->a, n * sizeof *v->items);
        if (object)
            v->keys = (const char **)arena_alloc(j->a, n * sizeof *v->keys);
        if (v->items == NULL || (object && v->keys == NULL)) {
            ok = fail(j, "out of memory");
        } else {
            memcpy(v->items, items, n * sizeof *items);
            if (object)
                memcpy((void *)v->keys, (const void *)keys, n * sizeof *keys);
        }
    }
    free(items);
    free((void *)keys);
    v->type = object ? J_OBJ : J_ARR;
    v->count = n;
    j->depth--;
    return ok;
}

static int parse_number(jparser *j, jval *v)
{
    const char *p = j->p;
    char *end;
    if (*p == '-') p++;
    if (*p < '0' || *p > '9')
        return fail(j, "unexpected character");
    while (*p >= '0' && *p <= '9') p++;
    if (*p == '.') {
        p++;
        if (*p < '0' || *p > '9') return fail(j, "bad number");
        while (*p >= '0' && *p <= '9') p++;
    }
    if (*p == 'e' || *p == 'E') {
        p++;
        if (*p == '+' || *p == '-') p++;
        if (*p < '0' || *p > '9') return fail(j, "bad number");
        while (*p >= '0' && *p <= '9') p++;
    }
    v->type = J_NUM;
    v->num = strtod(j->p, &end);
    if (end != p)
        return fail(j, "bad number");
    j->p = p;
    return 1;
}

static int parse_word(jparser *j, const char *word, jtype type, jval *v)
{
    size_t n = strlen(word);
    if (strncmp(j->p, word, n) != 0)
        return fail(j, "unexpected character");
    j->p += n;
    v->type = type;
    return 1;
}

static int parse_value(jparser *j, jval *v)
{
    memset(v, 0, sizeof *v);
    skip_ws(j);
    switch (*j->p) {
    case '{': return parse_container(j, v, 1);
    case '[': return parse_container(j, v, 0);
    case '"': v->type = J_STR; return parse_string(j, &v->str);
    case 't': return parse_word(j, "true", J_TRUE, v);
    case 'f': return parse_word(j, "false", J_FALSE, v);
    case 'n': return parse_word(j, "null", J_NULL, v);
    default: return parse_number(j, v);
    }
}

static const jval *jget(const jval *obj, const char *key)
{
    size_t i;
    if (obj == NULL || obj->type != J_OBJ)
        return NULL;
    for (i = 0; i < obj->count; i++)
        if (strcmp(obj->keys[i], key) == 0)
            return &obj->items[i];
    return NULL;
}

/* ---- Loading ---- */

typedef struct {
    arena a;
    const char *file;           /* the file being read, for messages */
    char *err;
    size_t errlen;
    int failed;
} loader;

static void report(loader *L, const char *fmt, ...)
{
    va_list ap;
    if (L->failed)
        return;
    L->failed = 1;
    if (L->err == NULL || L->errlen == 0)
        return;
    va_start(ap, fmt);
    vsnprintf(L->err, L->errlen, fmt, ap);
    va_end(ap);
}

static void *lalloc(loader *L, size_t n)
{
    void *p = arena_alloc(&L->a, n);
    if (p == NULL)
        report(L, "out of memory");
    return p;
}

/* Required fields report an error when missing; optional ones give NULL or the default. */
static const char *req_str(loader *L, const jval *obj, const char *key)
{
    const jval *v = jget(obj, key);
    if (v == NULL || v->type != J_STR) {
        report(L, "%s: \"%s\" is missing or not a string", L->file, key);
        return "";
    }
    return v->str;
}

static const char *opt_str(const jval *obj, const char *key)
{
    const jval *v = jget(obj, key);
    return v != NULL && v->type == J_STR ? v->str : NULL;
}

static int opt_bool(const jval *obj, const char *key, int dflt)
{
    const jval *v = jget(obj, key);
    if (v == NULL || (v->type != J_TRUE && v->type != J_FALSE))
        return dflt;
    return v->type == J_TRUE;
}

static int req_bool(loader *L, const jval *obj, const char *key)
{
    const jval *v = jget(obj, key);
    if (v == NULL || (v->type != J_TRUE && v->type != J_FALSE))
        report(L, "%s: \"%s\" is missing or not true or false", L->file, key);
    return v != NULL && v->type == J_TRUE;
}

static double req_num(loader *L, const jval *obj, const char *key)
{
    const jval *v = jget(obj, key);
    if (v == NULL || v->type != J_NUM) {
        report(L, "%s: \"%s\" is missing or not a number", L->file, key);
        return 0;
    }
    return v->num;
}

/* An array field; a missing optional one is empty. */
static const jval *arr(loader *L, const jval *obj, const char *key, int required)
{
    static const jval empty = { J_ARR, 0, NULL, 0, NULL, NULL };
    const jval *v = jget(obj, key);
    if (v != NULL && v->type == J_ARR)
        return v;
    if (required || v != NULL)
        report(L, "%s: \"%s\" is missing or not a list", L->file, key);
    return &empty;
}

static oeg_pair read_pair(loader *L, const jval *v)
{
    oeg_pair p;
    p.a = p.b = "";
    if (v == NULL || v->type != J_ARR || v->count != 2
        || v->items[0].type != J_STR || v->items[1].type != J_STR) {
        report(L, "%s: an allele pair is not a list of two ids", L->file);
        return p;
    }
    p.a = v->items[0].str;
    p.b = v->items[1].str;
    return p;
}

static void read_table(loader *L, const jval *d, const char *locus, const char *population, oeg_table *t)
{
    const jval *rows = arr(L, d, "genotypes", 1);
    oeg_share *s = (oeg_share *)lalloc(L, rows->count * sizeof *s);
    size_t i;
    t->locus = locus;
    t->population = population;
    t->evidence = req_str(L, d, "evidence");
    if (s == NULL)
        return;
    for (i = 0; i < rows->count; i++) {
        s[i].alleles = read_pair(L, jget(&rows->items[i], "alleles"));
        s[i].percent = req_num(L, &rows->items[i], "percent");
    }
    t->genotypes = s;
    t->genotype_count = rows->count;
}

static const oeg_table *read_pools(loader *L, const jval *list, size_t *count)
{
    oeg_table *t = (oeg_table *)lalloc(L, list->count * sizeof *t);
    size_t i;
    *count = 0;
    if (t == NULL)
        return NULL;
    for (i = 0; i < list->count; i++)
        read_table(L, &list->items[i], req_str(L, &list->items[i], "locus"), NULL, &t[i]);
    *count = list->count;
    return t;
}

static const oeg_citation *read_citations(loader *L, const jval *d, size_t *count)
{
    const jval *list = arr(L, d, "citations", 0);
    oeg_citation *c = (oeg_citation *)lalloc(L, list->count * sizeof *c);
    size_t i;
    *count = 0;
    if (c == NULL)
        return NULL;
    for (i = 0; i < list->count; i++) {
        const jval *x = &list->items[i];
        c[i].source = req_str(L, x, "source");
        c[i].label = req_str(L, x, "label");
        c[i].url = req_str(L, x, "url");
        c[i].verified = req_bool(L, x, "verified");
    }
    *count = list->count;
    return c;
}

static const oeg_phenotype *find_phenotype(const oeg_gene *g, const char *id)
{
    size_t i;
    if (id == NULL)
        return NULL;
    for (i = 0; i < g->phenotype_count; i++)
        if (strcmp(g->phenotypes[i].id, id) == 0)
            return &g->phenotypes[i];
    return NULL;
}

static const oeg_condition *find_condition(const oeg_gene *g, const char *id)
{
    size_t i;
    for (i = 0; i < g->condition_count; i++)
        if (strcmp(g->conditions[i].id, id) == 0)
            return &g->conditions[i];
    return NULL;
}

static const oeg_genotype_row *read_rows(loader *L, const oeg_gene *g, const jval *list, size_t *count)
{
    oeg_genotype_row *r = (oeg_genotype_row *)lalloc(L, list->count * sizeof *r);
    size_t i, k;
    *count = 0;
    if (r == NULL)
        return NULL;
    for (i = 0; i < list->count; i++) {
        const jval *x = &list->items[i];
        const jval *conds = arr(L, x, "conditions", 0);
        const oeg_condition **c = (const oeg_condition **)lalloc(L, conds->count * sizeof *c);
        r[i].alleles = read_pair(L, jget(x, "alleles"));
        r[i].phenotype = find_phenotype(g, opt_str(x, "phenotype"));
        if (c == NULL)
            return NULL;
        for (k = 0; k < conds->count; k++) {
            if (conds->items[k].type != J_STR || (c[k] = find_condition(g, conds->items[k].str)) == NULL)
                report(L, "%s: a genotype names an undeclared condition", L->file);
        }
        r[i].conditions = c;
        r[i].condition_count = conds->count;
    }
    *count = list->count;
    return r;
}

static void read_interactions(loader *L, oeg_gene *g, const jval *d)
{
    const jval *list, *dep;
    const char **depends;
    oeg_interaction_row *r;
    size_t i, k;
    if (d == NULL || d->type != J_OBJ)
        return;
    dep = arr(L, d, "depends_on", 1);
    list = arr(L, d, "table", 1);
    depends = (const char **)lalloc(L, dep->count * sizeof *depends);
    r = (oeg_interaction_row *)lalloc(L, list->count * sizeof *r);
    if (depends == NULL || r == NULL)
        return;
    for (i = 0; i < dep->count; i++)
        depends[i] = dep->items[i].type == J_STR ? dep->items[i].str : "";
    for (i = 0; i < list->count; i++) {
        const jval *x = &list->items[i];
        const jval *with = jget(x, "with");
        oeg_locus_pair *w;
        r[i].alleles = read_pair(L, jget(x, "alleles"));
        r[i].phenotype = find_phenotype(g, opt_str(x, "phenotype"));
        if (with == NULL || with->type != J_OBJ) {
            report(L, "%s: an interaction has no \"with\"", L->file);
            continue;
        }
        w = (oeg_locus_pair *)lalloc(L, with->count * sizeof *w);
        if (w == NULL)
            return;
        for (k = 0; k < with->count; k++) {
            w[k].locus = with->keys[k];
            w[k].pair = read_pair(L, &with->items[k]);
        }
        r[i].with = w;
        r[i].with_count = with->count;
    }
    g->depends_on = depends;
    g->depends_on_count = dep->count;
    g->interactions = r;
    g->interaction_count = list->count;
}

static void read_gene(loader *L, const jval *d, oeg_gene *g)
{
    const jval *list;
    oeg_allele *al;
    oeg_phenotype *ph;
    oeg_condition *co;
    oeg_table *fr;
    size_t i;

    g->id = req_str(L, d, "id");
    g->name = req_str(L, d, "name");
    g->symbol = opt_str(d, "symbol");
    g->chromosome = opt_str(d, "chromosome");
    g->evidence = req_str(L, d, "evidence");
    g->summary = req_str(L, d, "summary");
    g->inheritance = req_str(L, d, "inheritance");
    g->affects_coat = req_bool(L, d, "affects_coat");

    list = arr(L, d, "alleles", 1);
    if ((al = (oeg_allele *)lalloc(L, list->count * sizeof *al)) == NULL)
        return;
    for (i = 0; i < list->count; i++) {
        const jval *x = &list->items[i];
        al[i].id = req_str(L, x, "id");
        al[i].name = req_str(L, x, "name");
        al[i].wild_type = req_bool(L, x, "wild_type");
        al[i].quality = opt_str(x, "quality");
        al[i].placeholder = opt_str(x, "placeholder");
    }
    g->alleles = al;
    g->allele_count = list->count;

    list = arr(L, d, "phenotypes", 1);
    if ((ph = (oeg_phenotype *)lalloc(L, list->count * sizeof *ph)) == NULL)
        return;
    for (i = 0; i < list->count; i++) {
        const jval *x = &list->items[i];
        ph[i].id = req_str(L, x, "id");
        ph[i].name = req_str(L, x, "name");
        ph[i].description = req_str(L, x, "description");
        ph[i].changes_coat = req_bool(L, x, "changes_coat");
        ph[i].documented = opt_bool(x, "documented", 1);
    }
    g->phenotypes = ph;
    g->phenotype_count = list->count;

    list = arr(L, d, "conditions", 0);
    if ((co = (oeg_condition *)lalloc(L, list->count * sizeof *co)) == NULL)
        return;
    for (i = 0; i < list->count; i++) {
        const jval *x = &list->items[i];
        co[i].id = req_str(L, x, "id");
        co[i].name = req_str(L, x, "name");
        co[i].description = req_str(L, x, "description");
        co[i].severity = req_str(L, x, "severity");
    }
    g->conditions = co;
    g->condition_count = list->count;

    list = arr(L, d, "frequencies", 1);
    if ((fr = (oeg_table *)lalloc(L, list->count * sizeof *fr)) == NULL)
        return;
    for (i = 0; i < list->count; i++)
        read_table(L, &list->items[i], g->id, req_str(L, &list->items[i], "population"), &fr[i]);
    g->frequencies = fr;
    g->frequency_count = list->count;

    g->citations = read_citations(L, d, &g->citation_count);
    g->genotypes = read_rows(L, g, arr(L, d, "genotypes", 1), &g->genotype_count);
    g->nonviable_genotypes = read_rows(L, g, arr(L, d, "nonviable_genotypes", 0), &g->nonviable_count);
    read_interactions(L, g, jget(d, "interactions"));
}

static oeg_score read_score(loader *L, const jval *scores, const char *key)
{
    oeg_score s;
    const jval *v = jget(scores, key);
    s.low = s.high = 0;
    if (v != NULL && v->type == J_NUM) {
        s.low = s.high = v->num;
    } else if (v != NULL && v->type == J_ARR && v->count == 2
               && v->items[0].type == J_NUM && v->items[1].type == J_NUM) {
        s.low = v->items[0].num;
        s.high = v->items[1].num;
    } else {
        report(L, "%s: score \"%s\" is missing or not a number or [low, high]", L->file, key);
    }
    return s;
}

static void read_breed(loader *L, const jval *d, oeg_breed *b)
{
    const jval *scores = jget(d, "scores");
    const jval *list;
    oeg_strain *st;
    size_t i;

    b->id = req_str(L, d, "id");
    b->name = req_str(L, d, "name");
    b->country = req_str(L, d, "country");
    b->description = opt_str(d, "description");
    if (scores == NULL || scores->type != J_OBJ) {
        report(L, "%s: \"scores\" is missing", L->file);
    } else {
        b->scores.evidence = req_str(L, scores, "evidence");
        b->scores.speed = read_score(L, scores, "speed");
        b->scores.jump = read_score(L, scores, "jump");
        b->scores.health = read_score(L, scores, "health");
        b->scores.pull = read_score(L, scores, "pull");
    }
    b->pools = read_pools(L, arr(L, d, "pools", 1), &b->pool_count);

    list = arr(L, d, "strains", 0);
    if ((st = (oeg_strain *)lalloc(L, list->count * sizeof *st)) == NULL)
        return;
    for (i = 0; i < list->count; i++) {
        const jval *x = &list->items[i];
        st[i].name = req_str(L, x, "name");
        st[i].share = req_num(L, x, "share");
        st[i].pools = read_pools(L, arr(L, x, "pools", 1), &st[i].pool_count);
    }
    b->strains = st;
    b->strain_count = list->count;
    b->citations = read_citations(L, d, &b->citation_count);
}

/* ---- Files ---- */

typedef struct {
    char **names;
    size_t count, cap;
} name_list;

static void names_free(name_list *n)
{
    size_t i;
    for (i = 0; i < n->count; i++)
        free(n->names[i]);
    free(n->names);
    n->names = NULL;
    n->count = n->cap = 0;
}

static int names_add(name_list *n, const char *name)
{
    size_t len = strlen(name);
    char *copy;
    if (len < 5 || strcmp(name + len - 5, ".json") != 0)
        return 1;
    if (n->count == n->cap) {
        size_t ncap = n->cap ? n->cap * 2 : 64;
        char **nn = (char **)realloc(n->names, ncap * sizeof *nn);
        if (nn == NULL)
            return 0;
        n->names = nn;
        n->cap = ncap;
    }
    if ((copy = (char *)malloc(len + 1)) == NULL)
        return 0;
    memcpy(copy, name, len + 1);
    n->names[n->count++] = copy;
    return 1;
}

/* The names of the .json files in a folder. The only platform code in this file. */
static int list_json(const char *dir, name_list *out)
{
#ifdef _WIN32
    WIN32_FIND_DATAA fd;
    HANDLE h;
    size_t len = strlen(dir);
    char *pattern = (char *)malloc(len + 3);
    int ok = 1;
    if (pattern == NULL)
        return 0;
    memcpy(pattern, dir, len);
    memcpy(pattern + len, "/*", 3);
    h = FindFirstFileA(pattern, &fd);
    free(pattern);
    if (h == INVALID_HANDLE_VALUE)
        return 0;
    do {
        if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) && !names_add(out, fd.cFileName))
            ok = 0;
    } while (ok && FindNextFileA(h, &fd));
    FindClose(h);
    return ok;
#else
    DIR *d = opendir(dir);
    struct dirent *e;
    int ok = 1;
    if (d == NULL)
        return 0;
    while (ok && (e = readdir(d)) != NULL)
        ok = names_add(out, e->d_name);
    closedir(d);
    return ok;
#endif
}

static int cmp_names(const void *x, const void *y)
{
    return strcmp(*(char *const *)x, *(char *const *)y);
}

static char *read_file(const char *path, size_t *len)
{
    FILE *f = fopen(path, "rb");
    char *buf = NULL;
    size_t n = 0, cap = 0;
    if (f == NULL)
        return NULL;
    for (;;) {
        size_t got;
        if (cap - n < 4096) {
            char *nb = (char *)realloc(buf, cap ? cap * 2 : 16384);
            if (nb == NULL) {
                free(buf);
                fclose(f);
                return NULL;
            }
            buf = nb;
            cap = cap ? cap * 2 : 16384;
        }
        got = fread(buf + n, 1, cap - n - 1, f);
        n += got;
        if (got == 0)
            break;
    }
    if (ferror(f)) {
        free(buf);
        fclose(f);
        return NULL;
    }
    fclose(f);
    buf[n] = '\0';
    *len = n;
    return buf;
}

static char *join(const char *a, const char *b)
{
    size_t la = strlen(a), lb = strlen(b);
    char *p = (char *)malloc(la + lb + 2);
    if (p == NULL)
        return NULL;
    memcpy(p, a, la);
    p[la] = '/';
    memcpy(p + la + 1, b, lb + 1);
    return p;
}

/* Parses every .json file in root/sub, in name order, and hands each to read. */
static void *read_dir(loader *L, const char *root, const char *sub, size_t item_size, size_t *count,
                      void (*read)(loader *, const jval *, void *))
{
    char *dir = join(root, sub);
    name_list names = { NULL, 0, 0 };
    char *items = NULL;
    size_t i;
    *count = 0;
    if (dir == NULL) {
        report(L, "out of memory");
        return NULL;
    }
    if (!list_json(dir, &names)) {
        report(L, "cannot list %s", dir);
        goto done;
    }
    qsort(names.names, names.count, sizeof *names.names, cmp_names);
    if ((items = (char *)lalloc(L, names.count * item_size)) == NULL)
        goto done;
    for (i = 0; i < names.count && !L->failed; i++) {
        char *path = join(dir, names.names[i]);
        size_t len = 0;
        char *text = path ? read_file(path, &len) : NULL;
        jparser j;
        jval v;
        L->file = names.names[i];
        if (text == NULL) {
            report(L, "cannot read %s/%s", sub, names.names[i]);
            free(path);
            break;
        }
        j.p = text;
        j.a = &L->a;
        j.error = NULL;
        j.depth = 0;
        if (parse_value(&j, &v)) {
            skip_ws(&j);
            if (*j.p != '\0' || (size_t)(j.p - text) != len)
                fail(&j, "text after the value");
        }
        if (j.error != NULL)
            report(L, "%s/%s: %s at byte %lu", sub, names.names[i], j.error, (unsigned long)(j.p - text));
        else if (v.type != J_OBJ)
            report(L, "%s/%s: not a JSON object", sub, names.names[i]);
        else
            read(L, &v, items + i * item_size);
        free(text);
        free(path);
    }
    *count = names.count;
done:
    names_free(&names);
    free(dir);
    return items;
}

static void read_gene_cb(loader *L, const jval *d, void *out) { read_gene(L, d, (oeg_gene *)out); }
static void read_breed_cb(loader *L, const jval *d, void *out) { read_breed(L, d, (oeg_breed *)out); }

static int cmp_genes(const void *x, const void *y)
{
    return strcmp(((const oeg_gene *)x)->id, ((const oeg_gene *)y)->id);
}

static int cmp_breeds(const void *x, const void *y)
{
    return strcmp(((const oeg_breed *)x)->id, ((const oeg_breed *)y)->id);
}

oeg_database *oeg_load(const char *root, char *err, size_t errlen)
{
    loader L;
    oeg_database *db;
    oeg_gene *genes = NULL;
    oeg_breed *breeds = NULL;
    memset(&L, 0, sizeof L);
    L.err = err;
    L.errlen = errlen;
    if (err != NULL && errlen > 0)
        err[0] = '\0';
    db = (oeg_database *)lalloc(&L, sizeof *db);
    if (db != NULL) {
        genes = (oeg_gene *)read_dir(&L, root, "genes", sizeof(oeg_gene), &db->gene_count, read_gene_cb);
        if (!L.failed)
            breeds = (oeg_breed *)read_dir(&L, root, "breeds", sizeof(oeg_breed), &db->breed_count, read_breed_cb);
        if (!L.failed) {
            qsort(genes, db->gene_count, sizeof *genes, cmp_genes);
            qsort(breeds, db->breed_count, sizeof *breeds, cmp_breeds);
            db->genes = genes;
            db->breeds = breeds;
        }
    }
    if (L.failed) {
        arena_free(&L.a);
        return NULL;
    }
    /* the arena's first block holds db itself, so it keeps a copy of its own head */
    db->internal = lalloc(&L, sizeof(arena));
    if (db->internal == NULL) {
        arena_free(&L.a);
        return NULL;
    }
    *(arena *)db->internal = L.a;
    return db;
}

void oeg_free(oeg_database *db)
{
    if (db != NULL) {
        arena a = *(arena *)db->internal;
        arena_free(&a);
    }
}

/* ---- Queries ---- */

const oeg_gene *oeg_gene_find(const oeg_database *db, const char *id)
{
    size_t lo = 0, hi;
    if (db == NULL || id == NULL)
        return NULL;
    hi = db->gene_count;
    while (lo < hi) {
        size_t mid = lo + (hi - lo) / 2;
        int c = strcmp(db->genes[mid].id, id);
        if (c == 0)
            return &db->genes[mid];
        if (c < 0) lo = mid + 1;
        else hi = mid;
    }
    return NULL;
}

const oeg_breed *oeg_breed_find(const oeg_database *db, const char *id)
{
    size_t lo = 0, hi;
    if (db == NULL || id == NULL)
        return NULL;
    hi = db->breed_count;
    while (lo < hi) {
        size_t mid = lo + (hi - lo) / 2;
        int c = strcmp(db->breeds[mid].id, id);
        if (c == 0)
            return &db->breeds[mid];
        if (c < 0) lo = mid + 1;
        else hi = mid;
    }
    return NULL;
}

const oeg_allele *oeg_gene_allele(const oeg_gene *gene, const char *id)
{
    size_t i;
    for (i = 0; i < gene->allele_count; i++)
        if (strcmp(gene->alleles[i].id, id) == 0)
            return &gene->alleles[i];
    return NULL;
}

const oeg_phenotype *oeg_gene_phenotype(const oeg_gene *gene, const char *id)
{
    return find_phenotype(gene, id);
}

const oeg_condition *oeg_gene_condition(const oeg_gene *gene, const char *id)
{
    return find_condition(gene, id);
}

const oeg_allele *oeg_gene_wild_type(const oeg_gene *gene)
{
    size_t i;
    for (i = 0; i < gene->allele_count; i++)
        if (gene->alleles[i].wild_type)
            return &gene->alleles[i];
    return NULL;
}

const oeg_allele *oeg_gene_placeholder(const oeg_gene *gene)
{
    size_t i;
    for (i = 0; i < gene->allele_count; i++)
        if (gene->alleles[i].placeholder != NULL && gene->alleles[i].placeholder[0] != '\0')
            return &gene->alleles[i];
    return NULL;
}

const oeg_strain *oeg_breed_strain(const oeg_breed *breed, const char *name)
{
    size_t i;
    for (i = 0; i < breed->strain_count; i++)
        if (strcmp(breed->strains[i].name, name) == 0)
            return &breed->strains[i];
    return NULL;
}

int oeg_same_pair(oeg_pair x, oeg_pair y)
{
    return (strcmp(x.a, y.a) == 0 && strcmp(x.b, y.b) == 0)
        || (strcmp(x.a, y.b) == 0 && strcmp(x.b, y.a) == 0);
}

static const oeg_pair *context_pair(const oeg_genotype *context, const char *locus)
{
    size_t i;
    for (i = 0; i < context->count; i++)
        if (strcmp(context->pairs[i].locus, locus) == 0)
            return &context->pairs[i].pair;
    return NULL;
}

static void make_outcome(const oeg_gene *g, const oeg_genotype_row *row, int viable,
                         const oeg_genotype *context, oeg_pair_outcome *out)
{
    const oeg_phenotype *phenotype = row->phenotype;
    size_t i, k;
    if (viable && g->interaction_count > 0 && context != NULL && context->count > 0) {
        int all = 1;
        for (i = 0; i < g->depends_on_count && all; i++)
            all = context_pair(context, g->depends_on[i]) != NULL;
        for (i = 0; all && i < g->interaction_count; i++) {
            const oeg_interaction_row *e = &g->interactions[i];
            int match = oeg_same_pair(e->alleles, row->alleles);
            for (k = 0; match && k < e->with_count; k++) {
                const oeg_pair *p = context_pair(context, e->with[k].locus);
                match = p != NULL && oeg_same_pair(e->with[k].pair, *p);
            }
            if (match) {
                phenotype = e->phenotype;
                break;
            }
        }
    }
    out->alleles = row->alleles;
    out->phenotype = phenotype;
    out->conditions = row->conditions;
    out->condition_count = row->condition_count;
    out->viable = viable;
}

int oeg_outcome(const oeg_gene *gene, const char *a, const char *b,
                const oeg_genotype *context, oeg_pair_outcome *out)
{
    oeg_pair p;
    size_t i;
    p.a = a;
    p.b = b;
    for (i = 0; i < gene->genotype_count; i++)
        if (oeg_same_pair(gene->genotypes[i].alleles, p)) {
            make_outcome(gene, &gene->genotypes[i], 1, context, out);
            return 1;
        }
    for (i = 0; i < gene->nonviable_count; i++)
        if (oeg_same_pair(gene->nonviable_genotypes[i].alleles, p)) {
            make_outcome(gene, &gene->nonviable_genotypes[i], 0, context, out);
            return 1;
        }
    memset(out, 0, sizeof *out);
    return 0;
}

size_t oeg_cross(const oeg_gene *gene, oeg_pair dam, oeg_pair sire, oeg_offspring *out, size_t max)
{
    oeg_offspring result[4];
    const char *ds[2], *ss[2];
    size_t n = 0, i, x, y;
    ds[0] = dam.a; ds[1] = dam.b;
    ss[0] = sire.a; ss[1] = sire.b;
    for (x = 0; x < 2; x++) {
        for (y = 0; y < 2; y++) {
            oeg_pair p;
            int seen = 0;
            p.a = ds[x];
            p.b = ss[y];
            for (i = 0; i < n && !seen; i++)
                if (oeg_same_pair(result[i].alleles, p)) {
                    result[i].probability += 0.25;
                    seen = 1;
                }
            if (seen)
                continue;
            result[n].found = oeg_outcome(gene, p.a, p.b, NULL, &result[n].outcome);
            result[n].alleles = result[n].found ? result[n].outcome.alleles : p;
            result[n].probability = 0.25;
            n++;
        }
    }
    if (n > max)
        n = max;
    for (i = 0; i < n; i++)
        out[i] = result[i];
    return n;
}

const oeg_table *oeg_frequency_table(const oeg_gene *gene, const char *population)
{
    size_t i;
    if (population == NULL)
        population = OEG_GENERAL_POPULATION;
    for (i = 0; i < gene->frequency_count; i++)
        if (gene->frequencies[i].population != NULL && strcmp(gene->frequencies[i].population, population) == 0)
            return &gene->frequencies[i];
    return NULL;
}

const oeg_table *oeg_breed_pool(const oeg_breed *breed, const char *locus, const char *strain)
{
    const oeg_strain *s = strain != NULL ? oeg_breed_strain(breed, strain) : NULL;
    size_t i;
    if (s != NULL)
        for (i = 0; i < s->pool_count; i++)
            if (strcmp(s->pools[i].locus, locus) == 0)
                return &s->pools[i];
    for (i = 0; i < breed->pool_count; i++)
        if (strcmp(breed->pools[i].locus, locus) == 0)
            return &breed->pools[i];
    return NULL;
}

const oeg_table *oeg_genotype_table(const oeg_database *db, const char *locus,
                                    const char *breed, const char *strain)
{
    const oeg_breed *b = breed != NULL ? oeg_breed_find(db, breed) : NULL;
    const oeg_table *table = b != NULL ? oeg_breed_pool(b, locus, strain) : NULL;
    const oeg_gene *g;
    if (table == NULL && (g = oeg_gene_find(db, locus)) != NULL)
        table = oeg_frequency_table(g, NULL);
    return table;
}

/* The sampling rule of apis/README.md, over the rows a filter keeps: placeholder NULL keeps
   every row, else a row is kept when it holds the placeholder exactly when male is set. */
static const oeg_pair *sample_rows(const oeg_table *t, const char *placeholder, int male,
                                   oeg_rng rng, void *state)
{
    double total = 0, target, running = 0;
    const oeg_pair *last = NULL;
    size_t i;
    for (i = 0; i < t->genotype_count; i++) {
        const oeg_pair *p = &t->genotypes[i].alleles;
        if (placeholder == NULL || ((strcmp(p->a, placeholder) == 0 || strcmp(p->b, placeholder) == 0) == male))
            total += t->genotypes[i].percent;
    }
    if (total <= 0)
        return NULL;
    target = rng(state) * total;
    for (i = 0; i < t->genotype_count; i++) {
        const oeg_pair *p = &t->genotypes[i].alleles;
        if (placeholder == NULL || ((strcmp(p->a, placeholder) == 0 || strcmp(p->b, placeholder) == 0) == male)) {
            running += t->genotypes[i].percent;
            last = p;
            if (target < running)
                return p;
        }
    }
    return last;
}

const oeg_pair *oeg_sample(const oeg_table *table, oeg_rng rng, void *rng_state)
{
    return sample_rows(table, NULL, 0, rng, rng_state);
}

static uint64_t fallback_state = 0x9E3779B97F4A7C15u;

int oeg_random_horse(const oeg_database *db, const char *breed, oeg_sex sex, const char *strain,
                     oeg_rng rng, void *rng_state, oeg_horse *horse)
{
    const oeg_breed *b = breed != NULL ? oeg_breed_find(db, breed) : NULL;
    oeg_locus_pair *pairs;
    size_t i;
    memset(horse, 0, sizeof *horse);
    if (rng == NULL) {
        rng = oeg_default_rng;
        rng_state = &fallback_state;
    }
    if (b != NULL && b->strain_count > 0 && strain == NULL) {
        double total = 0, target, running = 0;
        for (i = 0; i < b->strain_count; i++)
            total += b->strains[i].share;
        if (total > 0) {
            target = rng(rng_state) * total;
            strain = b->strains[b->strain_count - 1].name;
            for (i = 0; i < b->strain_count; i++) {
                running += b->strains[i].share;
                if (target < running) {
                    strain = b->strains[i].name;
                    break;
                }
            }
        }
    } else if (b != NULL && strain != NULL) {
        const oeg_strain *s = oeg_breed_strain(b, strain);
        strain = s != NULL ? s->name : NULL;
    } else {
        strain = NULL;
    }
    pairs = (oeg_locus_pair *)malloc((db->gene_count ? db->gene_count : 1) * sizeof *pairs);
    if (pairs == NULL)
        return 0;
    for (i = 0; i < db->gene_count; i++) {
        const oeg_gene *g = &db->genes[i];
        const oeg_table *table = oeg_genotype_table(db, g->id, b != NULL ? b->id : NULL, strain);
        const oeg_allele *placeholder = oeg_gene_placeholder(g);
        const oeg_pair *p = NULL;
        pairs[i].locus = g->id;
        if (table != NULL)
            p = sample_rows(table, placeholder != NULL ? placeholder->id : NULL, sex == OEG_STALLION, rng, rng_state);
        if (p != NULL) {
            pairs[i].pair = *p;
        } else {
            const oeg_allele *wild = oeg_gene_wild_type(g);
            const char *w = wild != NULL ? wild->id : g->allele_count > 0 ? g->alleles[0].id : "";
            pairs[i].pair.a = w;
            pairs[i].pair.b = placeholder != NULL && sex == OEG_STALLION ? placeholder->id : w;
        }
    }
    horse->sex = sex;
    horse->breed = b != NULL ? b->id : NULL;
    horse->strain = strain;
    horse->genotype.pairs = pairs;
    horse->genotype.count = db->gene_count;
    return 1;
}

void oeg_horse_free(oeg_horse *horse)
{
    if (horse != NULL) {
        free((void *)horse->genotype.pairs);
        horse->genotype.pairs = NULL;
        horse->genotype.count = 0;
    }
}

size_t oeg_outcomes(const oeg_database *db, const oeg_genotype *genotype, oeg_pair_outcome *out)
{
    size_t i, found = 0;
    for (i = 0; i < genotype->count; i++) {
        const oeg_locus_pair *lp = &genotype->pairs[i];
        const oeg_gene *g = oeg_gene_find(db, lp->locus);
        if (g != NULL && oeg_outcome(g, lp->pair.a, lp->pair.b, genotype, &out[i]))
            found++;
        else
            memset(&out[i], 0, sizeof out[i]);
    }
    return found;
}

double oeg_default_rng(void *state)
{
    uint64_t *s = (uint64_t *)state;
    uint64_t z = (*s += 0x9E3779B97F4A7C15u);
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9u;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBu;
    z ^= z >> 31;
    return (double)(z >> 11) / 9007199254740992.0;
}
