/* $Id$ */

#include <assert.h>
#include <stdio.h>
#include <errno.h>
#include <ctype.h>

#include <adt/set.h>

#include <fsm/out.h>
#include <fsm/pred.h>
#include <fsm/walk.h>
#include <fsm/graph.h>

#include "libfsm/internal.h" /* XXX */

#include "../ast.h"
#include "../internal.h"

#include "out.h"

static int
skip(const struct fsm *fsm, const struct fsm_state *state)
{
	struct ast_mapping *m;

	assert(fsm != NULL);
	assert(state != NULL);

	if (!fsm_isend(fsm, state)) {
		return 1;
	}

	m = state->opaque;

	assert(m != NULL);

	if (m->token == NULL) {
		return 1;
	}

	return 0;
}

/* TODO: centralise */
static void
out_escstr(FILE *f, const char *s)
{
	const char *p;

	assert(f != NULL);
	assert(s != NULL);

	for (p = s; *p != '\0'; p++) {
		if (!isprint((unsigned char) *p)) {
			fprintf(f, "\\x%02x", *p);
			continue;
		}

		switch (*p) {
		case '\\': fprintf(f, "\\\\");   continue;
		case '"':  fprintf(f, "\\\"");   continue;

		case '\t': fprintf(f, "\\t");    continue;
		case '\n': fprintf(f, "\\n");    continue;
		case '\v': fprintf(f, "\\v");    continue;
		case '\r': fprintf(f, "\\r");    continue;
		case '\f': fprintf(f, "\\f");    continue;

		default:   fprintf(f, "%c", *p); continue;
		}
	}
}

/* TODO: centralise */
static void
out_esctok(FILE *f, const char *s)
{
	const char *p;

	assert(f != NULL);
	assert(s != NULL);

	for (p = s; *p != '\0'; p++) {
		fputc(isalnum(*p) ? toupper(*p) : '_', f);
	}
}

/* TODO: centralise with libfsm */
static unsigned int
indexof(const struct fsm *fsm, const struct fsm_state *state)
{
	struct fsm_state *s;
	unsigned int i;

	assert(fsm != NULL);
	assert(state != NULL);

	for (s = fsm->sl, i = 1; s != NULL; s = s->next, i++) {
		if (s == state) {
			return i;
		}
	}

	assert(!"unreached");
	return 0;
}

static unsigned int
zindexof(const struct ast *ast, const struct ast_zone *zone)
{
	struct ast_zone *z;
	unsigned int i;

	assert(ast != NULL);
	assert(zone != NULL);

	for (z = ast->zl, i = 1; z != NULL; z = z->next, i++) {
		if (z == zone) {
			return i;
		}
	}

	assert(!"unreached");
	return 0;
}

static void
escputc(char c, FILE *f)
{
	assert(f != NULL);

	if (!isprint(c) && !isspace(c)) {
		fprintf(f, "\\x%x", (unsigned char) c);
		return;
	}

	switch (c) {
	case '\\': fprintf(f, "\\\\"); return;
	case '\"': fprintf(f, "\\\""); return;
	case '\'': fprintf(f, "\\\'"); return;
	case '\t': fprintf(f, "\\t");  return;
	case '\n': fprintf(f, "\\n");  return;
	case '\v': fprintf(f, "\\v");  return;
	case '\f': fprintf(f, "\\f");  return;
	case '\r': fprintf(f, "\\r");  return;

		/* TODO: others */

	default:
		putc(c, f);
	}
}

/* XXX: centralise */
static int
xset_contains(const struct state_set *set, const struct fsm_state *state)
{
	const struct state_set *s;

	for (s = set; s != NULL; s = s->next) {
		if (s->state == state) {
			return 1;
		}
	}

	return 0;
}

/* Return true if the edges after o contains state */
/* TODO: centralise */
static int
contains(struct fsm_edge edges[], int o, struct fsm_state *state)
{
	int i;

	assert(edges != NULL);
	assert(state != NULL);

	for (i = o; i <= UCHAR_MAX; i++) {
		if (xset_contains(edges[i].sl, state)) {
			return 1;
		}
	}

	return 0;
}

/*
 * Given a token, find one of its accepting states which gives
 * the shortest fsm_example(). This is pretty expensive.
 */
static const struct fsm_state *
shortest_example(const struct fsm *fsm, const struct ast_token *token)
{
	const struct fsm_state *goal;
	struct fsm_state *s;
	int min;

	assert(fsm != NULL);
	assert(token != NULL);

	/*
	 * We're nominating fsm->start to mean the given token was not present
	 * in this FSM; this is on the premise that the start state cannot
	 * accept, because lx does not permit empty regexps.
	 */
	goal = fsm->start;
	min  = INT_MAX;

	for (s = fsm->sl; s != NULL; s = s->next) {
		const struct ast_mapping *m;
		int n;

		m = s->opaque;
		if (m == NULL) {
			continue;
		}

		if (m->token != token) {
			continue;
		}

		n = fsm_example(fsm, s, NULL, 0);
		if (-1 == n) {
			perror("fsm_example");
			return NULL;
		}

		if (n < min) {
			min = n;
			goal = s;
		}
	}

	return goal;
}

static void
singlecase(FILE *f, const struct ast *ast, const struct ast_zone *z,
	const struct fsm *fsm, struct fsm_state *state)
{
	struct fsm_state *mode;
	int i;

	assert(fsm != NULL);
	assert(ast != NULL);
	assert(z != NULL);
	assert(f != NULL);
	assert(state != NULL);

	/* TODO: assert that there are never no edges? */
	/* TODO: if greedy and state is an end state, skip this state */

	/* TODO: could centralise this with libfsm with internal options passed, perhaps */

	fprintf(f, "\t\t\tswitch (c) {\n");

	mode = fsm_iscomplete(fsm, state) ? fsm_findmode(state) : NULL;

	for (i = 0; i <= UCHAR_MAX; i++) {
		if (state->edges[i].sl == NULL) {
			continue;
		}

		assert(state->edges[i].sl->state != NULL);
		assert(state->edges[i].sl->next  == NULL);

		if (state->edges[i].sl->state == mode) {
			continue;
		}

		fprintf(f, "\t\t\tcase '");
		escputc(i, f);
		fprintf(f, "':");

		/* non-unique states fall through */
/* XXX: this is an incorrect optimisation; to re-enable when fixed
		if (contains(state->edges, i + 1, state->edges[i].sl->state)) {
			fprintf(f, "\n");
			continue;
		}
*/

		/* TODO: pad S%u out to maximum state width */
		if (state->edges[i].sl->state != state) {
			fprintf(f, " state = S%u;      continue;\n", indexof(fsm, state->edges[i].sl->state));
		} else {
			fprintf(f, "	          continue;\n");
		}

		/* TODO: if greedy, and fsm_isend(fsm, state->edges[i].sl->state) then:
			fprintf(f, "	     return TOK_%s;\n", state->edges[i].sl->state's token);
		 */
	}

	if (mode != NULL) {
		/* TODO: state-change as for typical cases */
		/* TODO: i think... */
		fprintf(f, "\t\t\tdefault:  state = S%u;     continue;\n",  indexof(fsm, mode));

		goto done;
	}

	if (!fsm_isend(fsm, state)) {
		/* XXX: don't need this if complete */
		fprintf(f, "\t\t\tdefault:  lx->lgetc = NULL; return TOK_UNKNOWN;\n");
	} else {
		const struct ast_mapping *m;

		m = state->opaque;

		assert(m != NULL);

		/* XXX: don't need this if complete */
		fprintf(f, "\t\t\tdefault:  %sungetc(lx, c); return ", prefix);
		if (m->to != NULL) {
			fprintf(f, "lx->z = z%u, ", zindexof(ast, m->to));
		}
		if (m->token != NULL) {
			fprintf(f, "TOK_");
			out_esctok(f, m->token->s);
		} else {
			fprintf(f, "lx->z(lx)");
		}
		fprintf(f, ";\n");
	}

done:

	fprintf(f, "\t\t\t}\n");
}

/* TODO: centralise with libfsm */
static void
stateenum(FILE *f, const struct fsm *fsm, struct fsm_state *sl)
{
	struct fsm_state *s;
	int i;

	fprintf(f, "\tenum {\n");
	fprintf(f, "\t\t");

	for (s = sl, i = 1; s != NULL; s = s->next, i++) {
		fprintf(f, "S%u", indexof(fsm, s));
		if (s->next != NULL) {
			fprintf(f, ", ");
		}

		if (i % 10 == 0) {
			fprintf(f, "\n");
			fprintf(f, "\t\t");
		}
	}

	fprintf(f, "\n");
	fprintf(f, "\t} state;\n");
}

static void
out_proto(FILE *f, const struct ast *ast, const struct ast_zone *z)
{
	assert(f != NULL);
	assert(ast != NULL);
	assert(z != NULL);

	fprintf(f, "static enum lx_token z%u(struct lx *lx);\n", zindexof(ast, z));
}

static void
out_lgetc(FILE *f)
{
	fprintf(f, "int\n");
	fprintf(f, "%sfgetc(struct lx *lx)\n", prefix);
	fprintf(f, "{\n");
	fprintf(f, "\tassert(lx != NULL);\n");
	fprintf(f, "\tassert(lx->opaque != NULL);\n");
	fprintf(f, "\n");
	fprintf(f, "\treturn fgetc(lx->opaque);\n");
	fprintf(f, "}\n");
	fprintf(f, "\n");

	fprintf(f, "int\n");
	fprintf(f, "%ssgetc(struct lx *lx)\n", prefix);
	fprintf(f, "{\n");
	fprintf(f, "\tchar *s;\n");
	fprintf(f, "\n");
	fprintf(f, "\tassert(lx != NULL);\n");
	fprintf(f, "\tassert(lx->opaque != NULL);\n");
	fprintf(f, "\n");
	fprintf(f, "\ts = lx->opaque;\n");
	fprintf(f, "\tif (*s == '\\0') {\n");
	fprintf(f, "\t\treturn EOF;\n");
	fprintf(f, "\t}\n");
	fprintf(f, "\n");
	fprintf(f, "\treturn lx->opaque = s + 1, *s;\n");
	fprintf(f, "}\n");
	fprintf(f, "\n");

	fprintf(f, "int\n");
	fprintf(f, "%sagetc(struct lx *lx)\n", prefix);
	fprintf(f, "{\n");
	fprintf(f, "\tstruct lx_arr *a;\n");
	fprintf(f, "\n");
	fprintf(f, "\tassert(lx != NULL);\n");
	fprintf(f, "\tassert(lx->opaque != NULL);\n");
	fprintf(f, "\n");
	fprintf(f, "\ta = lx->opaque;\n");
	fprintf(f, "\n");
	fprintf(f, "\tassert(a != NULL);\n");
	fprintf(f, "\tassert(a->p != NULL);\n");
	fprintf(f, "\n");
	fprintf(f, "\tif (a->len == 0) {\n");
	fprintf(f, "\t\treturn EOF;\n");
	fprintf(f, "\t}\n");
	fprintf(f, "\n");
	fprintf(f, "\treturn a->len--, *a->p++;\n");
	fprintf(f, "}\n");
	fprintf(f, "\n");

	/* TODO: POSIX only */
	fprintf(f, "int\n");
	fprintf(f, "%sdgetc(struct lx *lx)\n", prefix);
	fprintf(f, "{\n");
	fprintf(f, "\tstruct lx_fd *d;\n");
	fprintf(f, "\n");
	fprintf(f, "\tassert(lx != NULL);\n");
	fprintf(f, "\tassert(lx->opaque != NULL);\n");
	fprintf(f, "\n");
	fprintf(f, "\td = lx->opaque;\n");
	fprintf(f, "\tassert(d->fd != -1);\n");
	fprintf(f, "\tassert(d->p != NULL);\n");
	fprintf(f, "\n");
	fprintf(f, "\tif (d->len == 0) {\n");
	fprintf(f, "\t\tssize_t r;\n");
	fprintf(f, "\n");
	fprintf(f, "\t\tassert((fcntl(d->fd, F_GETFL) & O_NONBLOCK) == 0);\n");
	fprintf(f, "\n");
	fprintf(f, "\t\td->p = (char *) d + sizeof *d;\n");
	fprintf(f, "\n");
	fprintf(f, "\t\tr = read(d->fd, d->p, d->bufsz);\n");
	fprintf(f, "\t\tif (r == -1) {\n");
	fprintf(f, "\t\t\tassert(errno != EAGAIN);\n");
	fprintf(f, "\t\t\treturn EOF;\n");
	fprintf(f, "\t\t}\n");
	fprintf(f, "\n");
	fprintf(f, "\t\tif (r == 0) {\n");
	fprintf(f, "\t\t\treturn EOF;\n");
	fprintf(f, "\t\t}\n");
	fprintf(f, "\n");
	fprintf(f, "\t\td->len = r;\n");
	fprintf(f, "\t}\n");
	fprintf(f, "\n");
	fprintf(f, "\treturn d->len--, *d->p++;\n");
	fprintf(f, "}\n");
	fprintf(f, "\n");
}

static void
out_io(FILE *f)
{
	/* TODO: consider passing char *c, and return int 0/-1 for error */
	fprintf(f, "static int\n");
	fprintf(f, "lx_getc(struct lx *lx)\n");
	fprintf(f, "{\n");
	fprintf(f, "\tint c;\n");
	fprintf(f, "\n");
	fprintf(f, "\tassert(lx != NULL);\n");
	fprintf(f, "\tassert(lx->lgetc != NULL);\n");
	fprintf(f, "\n");
	fprintf(f, "\tif (lx->c != EOF) {\n");
	fprintf(f, "\t\tc = lx->c, lx->c = EOF;\n");
	fprintf(f, "\t} else {\n");
	fprintf(f, "\t\tc = lx->lgetc(lx);\n");
	fprintf(f, "\t\tif (c == EOF) {\n");
	fprintf(f, "\t\t\treturn EOF;\n");
	fprintf(f, "\t\t}\n");
	fprintf(f, "\t}\n");
	fprintf(f, "\n");
	fprintf(f, "\tlx->end.byte++;\n");
	fprintf(f, "\tlx->end.col++;\n");
	fprintf(f, "\n");
	fprintf(f, "\tif (c == '\\n') {\n");
	fprintf(f, "\t\tlx->end.line++;\n");
	fprintf(f, "\t\tlx->end.col = 1;\n");
	fprintf(f, "\t}\n");
	fprintf(f, "\n");
	fprintf(f, "\treturn c;\n");
	fprintf(f, "}\n");
	fprintf(f, "\n");

	fprintf(f, "static void\n");
	fprintf(f, "%sungetc(struct lx *lx, int c)\n", prefix);
	fprintf(f, "{\n");
	fprintf(f, "\tassert(lx != NULL);\n");
	fprintf(f, "\tassert(lx->c == EOF);\n");
	fprintf(f, "\n");
	fprintf(f, "\tlx->c = c;\n");
	fprintf(f, "\n");
	fprintf(f, "\tif (lx->pop != NULL) {\n");
	fprintf(f, "\t\tlx->pop(lx);\n");
	fprintf(f, "\t}\n");
	fprintf(f, "\n");
	fprintf(f, "\tlx->end.byte--;\n");
	fprintf(f, "\tlx->end.col--;\n");
	fprintf(f, "\n");
	fprintf(f, "\tif (c == '\\n') {\n");
	fprintf(f, "\t\tlx->end.line--;\n");
	fprintf(f, "\t\tlx->end.col = 0; /* XXX: lost information */\n");
	fprintf(f, "\t}\n");
	fprintf(f, "}\n");
	fprintf(f, "\n");
}

static void
out_buf(FILE *f)
{
	fprintf(f, "int\n");
	fprintf(f, "%sdynpush(struct lx *lx, char c)\n", prefix);
	fprintf(f, "{\n");
	fprintf(f, "\tstruct lx_dynbuf *t;\n");
	fprintf(f, "\n");
	fprintf(f, "\tassert(lx != NULL);\n");
	fprintf(f, "\tassert(c != EOF);\n");
	fprintf(f, "\n");
	fprintf(f, "\tt = lx->buf;\n");
	fprintf(f, "\n");
	fprintf(f, "\tassert(t != NULL);\n");
	fprintf(f, "\n");
	fprintf(f, "\tif (t->p == t->a + t->len) {\n");
	fprintf(f, "\t\tsize_t len;\n");
	fprintf(f, "\t\tchar *tmp;\n");
	fprintf(f, "\n");
	fprintf(f, "\t\tif (t->len == 0) {\n");
	fprintf(f, "\t\t\tassert(LX_DYN_LOW > 0);\n");
	fprintf(f, "\t\t\tlen = LX_DYN_LOW;\n");
	fprintf(f, "\t\t} else {\n");
	fprintf(f, "\t\t\tlen = t->len * LX_DYN_FACTOR;\n");
	fprintf(f, "\t\t\tif (len < t->len) {\n");
	fprintf(f, "\t\t\t\terrno = ERANGE;\n");
	fprintf(f, "\t\t\t\treturn -1;\n");
	fprintf(f, "\t\t\t}\n");
	fprintf(f, "\t\t}\n");
	fprintf(f, "\n");
	fprintf(f, "\t\ttmp = realloc(t->a, len);\n");
	fprintf(f, "\t\tif (tmp == NULL) {\n");
	fprintf(f, "\t\t\treturn -1;\n");
	fprintf(f, "\t\t}\n");
	fprintf(f, "\n");
	fprintf(f, "\t\tt->p   = tmp + (t->p - t->a);\n");
	fprintf(f, "\t\tt->a   = tmp;\n");
	fprintf(f, "\t\tt->len = len;\n");
	fprintf(f, "\t}\n");
	fprintf(f, "\n");
	fprintf(f, "\tassert(t->p != NULL);\n");
	fprintf(f, "\tassert(t->a != NULL);\n");
	fprintf(f, "\n");
	fprintf(f, "\t*t->p++ = c;\n");
	fprintf(f, "\n");
	fprintf(f, "\treturn 0;\n");
	fprintf(f, "}\n");
	fprintf(f, "\n");

	fprintf(f, "void\n");
	fprintf(f, "%sdynpop(struct lx *lx)\n", prefix);
	fprintf(f, "{\n");
	fprintf(f, "\tstruct lx_dynbuf *t;\n");
	fprintf(f, "\n");
	fprintf(f, "\tassert(lx != NULL);\n");
	fprintf(f, "\n");
	fprintf(f, "\tt = lx->buf;\n");
	fprintf(f, "\n");
	fprintf(f, "\tassert(t != NULL);\n");
	fprintf(f, "\tassert(t->a != NULL);\n");
	fprintf(f, "\tassert(t->p >= t->a);\n");
	fprintf(f, "\n");
	fprintf(f, "\tif (t->p == t->a) {\n");
	fprintf(f, "\t\treturn;\n");
	fprintf(f, "\t}\n");
	fprintf(f, "\n");
	fprintf(f, "\tt->p--;\n");
	fprintf(f, "}\n");
	fprintf(f, "\n");

	fprintf(f, "int\n");
	fprintf(f, "%sdynclear(struct lx *lx)\n", prefix);
	fprintf(f, "{\n");
	fprintf(f, "\tstruct lx_dynbuf *t;\n");
	fprintf(f, "\n");
	fprintf(f, "\tassert(lx != NULL);\n");
	fprintf(f, "\n");
	fprintf(f, "\tt = lx->buf;\n");
	fprintf(f, "\n");
	fprintf(f, "\tassert(t != NULL);\n");
	fprintf(f, "\n");
	fprintf(f, "\tif (t->len > LX_DYN_HIGH) {\n");
	fprintf(f, "\t\tsize_t len;\n");
	fprintf(f, "\t\tchar *tmp;\n");
	fprintf(f, "\n");
	fprintf(f, "\t\tlen = t->len / LX_DYN_FACTOR;\n");
	fprintf(f, "\n");
	fprintf(f, "\t\ttmp = realloc(t->a, len);\n");
	fprintf(f, "\t\tif (tmp == NULL) {\n");
	fprintf(f, "\t\t\treturn -1;\n");
	fprintf(f, "\t\t}\n");
	fprintf(f, "\n");
	fprintf(f, "\t\tt->a   = tmp;\n");
	fprintf(f, "\t\tt->len = len;\n");
	fprintf(f, "\t}\n");
	fprintf(f, "\n");
	fprintf(f, "\tt->p = t->a;\n");
	fprintf(f, "\n");
	fprintf(f, "\treturn 0;\n");
	fprintf(f, "}\n");
	fprintf(f, "\n");

	fprintf(f, "void\n");
	fprintf(f, "%sdynfree(struct lx *lx)\n", prefix);
	fprintf(f, "{\n");
	fprintf(f, "\tstruct lx_dynbuf *t;\n");
	fprintf(f, "\n");
	fprintf(f, "\tassert(lx != NULL);\n");
	fprintf(f, "\n");
	fprintf(f, "\tt = lx->buf;\n");
	fprintf(f, "\n");
	fprintf(f, "\tassert(t != NULL);\n");
	fprintf(f, "\n");
	fprintf(f, "\tfree(t->a);\n");
	fprintf(f, "}\n");

	fprintf(f, "int\n");
	fprintf(f, "%sfixedpush(struct lx *lx, char c)\n", prefix);
	fprintf(f, "{\n");
	fprintf(f, "\tstruct lx_fixedbuf *t;\n");
	fprintf(f, "\n");
	fprintf(f, "\tassert(lx != NULL);\n");
	fprintf(f, "\tassert(c != EOF);\n");
	fprintf(f, "\n");
	fprintf(f, "\tt = lx->buf;\n");
	fprintf(f, "\n");
	fprintf(f, "\tassert(t != NULL);\n");
	fprintf(f, "\tassert(t->p != NULL);\n");
	fprintf(f, "\tassert(t->a != NULL);\n");
	fprintf(f, "\n");
	fprintf(f, "\tif (t->p == t->a + t->len) {\n");
	fprintf(f, "\t\terrno = ENOMEM;\n");
	fprintf(f, "\t\treturn -1;\n");
	fprintf(f, "\t}\n");
	fprintf(f, "\n");
	fprintf(f, "\t*t->p++ = c;\n");
	fprintf(f, "\n");
	fprintf(f, "\treturn 0;\n");
	fprintf(f, "}\n");
	fprintf(f, "\n");

	fprintf(f, "void\n");
	fprintf(f, "%sfixedpop(struct lx *lx)\n", prefix);
	fprintf(f, "{\n");
	fprintf(f, "\tstruct lx_fixedbuf *t;\n");
	fprintf(f, "\n");
	fprintf(f, "\tassert(lx != NULL);\n");
	fprintf(f, "\n");
	fprintf(f, "\tt = lx->buf;\n");
	fprintf(f, "\n");
	fprintf(f, "\tassert(t != NULL);\n");
	fprintf(f, "\tassert(t->a != NULL);\n");
	fprintf(f, "\tassert(t->p >= t->a);\n");
	fprintf(f, "\n");
	fprintf(f, "\tif (t->p == t->a) {\n");
	fprintf(f, "\t\treturn;\n");
	fprintf(f, "\t}\n");
	fprintf(f, "\n");
	fprintf(f, "\tt->p--;\n");
	fprintf(f, "}\n");
	fprintf(f, "\n");

	fprintf(f, "int\n");
	fprintf(f, "%sfixedclear(struct lx *lx)\n", prefix);
	fprintf(f, "{\n");
	fprintf(f, "\tstruct lx_fixedbuf *t;\n");
	fprintf(f, "\n");
	fprintf(f, "\tassert(lx != NULL);\n");
	fprintf(f, "\n");
	fprintf(f, "\tt = lx->buf;\n");
	fprintf(f, "\n");
	fprintf(f, "\tassert(t != NULL);\n");
	fprintf(f, "\tassert(t->p != NULL);\n");
	fprintf(f, "\tassert(t->a != NULL);\n");
	fprintf(f, "\n");
	fprintf(f, "\tt->p = t->a;\n");
	fprintf(f, "\n");
	fprintf(f, "\treturn 0;\n");
	fprintf(f, "}\n");
	fprintf(f, "\n");
}

static int
out_zone(FILE *f, const struct ast *ast, const struct ast_zone *z)
{
	assert(f != NULL);
	assert(z != NULL);
	assert(z->fsm != NULL);
	assert(fsm_all(z->fsm, fsm_isdfa));
	assert(ast != NULL);

	/* TODO: prerequisite that the FSM is a DFA */

	fprintf(f, "static enum lx_token\n");
	fprintf(f, "z%u(struct lx *lx)\n", zindexof(ast, z));
	fprintf(f, "{\n");
	fprintf(f, "\tint c;\n");
	fprintf(f, "\n");

	stateenum(f, z->fsm, z->fsm->sl);
	fprintf(f, "\n");

	fprintf(f, "\tassert(lx != NULL);\n");
	fprintf(f, "\n");

	fprintf(f, "\tif (lx->clear != NULL) {\n");
	fprintf(f, "\t\tlx->clear(lx);\n");
	fprintf(f, "\t}\n");
	fprintf(f, "\n");

	assert(z->fsm->start != NULL);
	fprintf(f, "\tstate = S%u;\n", indexof(z->fsm, z->fsm->start));
	fprintf(f, "\n");

	fprintf(f, "\tlx->start = lx->end;\n");
	fprintf(f, "\n");

	fprintf(f, "\twhile (c = lx_getc(lx), c != EOF) {\n");

	{
		struct fsm_state *s;

		fprintf(f, "\t\tswitch (state) {\n");

		for (s = z->fsm->sl; s != NULL; s = s->next) {
			int r;

			r = fsm_reachable(z->fsm, s, skip);
			if (r == -1) {
				return -1;
			}

			if (!r) {
				continue;
			}

			fprintf(f, "\t\tcase S%u:\n", indexof(z->fsm, s));
		}

		if (fsm_has(z->fsm, skip)) {
			fprintf(f, "\t\t\tbreak;\n");
			fprintf(f, "\n");
		}

		fprintf(f, "\t\tdefault:\n");
		fprintf(f, "\t\t\tif (lx->push != NULL) {\n");
		fprintf(f, "\t\t\t\tif (-1 == lx->push(lx, c)) {\n");
		fprintf(f, "\t\t\t\t\treturn TOK_ERROR;\n");
		fprintf(f, "\t\t\t\t}\n");
		fprintf(f, "\t\t\t}\n");
		fprintf(f, "\t\t\tbreak;\n");
		fprintf(f, "\n");

		fprintf(f, "\t\t}\n");
		fprintf(f, "\n");
	}

	{
		struct fsm_state *s;

		fprintf(f, "\t\tswitch (state) {\n");

		for (s = z->fsm->sl; s != NULL; s = s->next) {
			char buf[50];
			int n;

			n = fsm_example(z->fsm, s, buf, sizeof buf);
			if (-1 == n) {
				perror("fsm_example");
				return -1;
			}

			fprintf(f, "\t\tcase S%u: /* e.g. \"", indexof(z->fsm, s));
			out_escstr(f, buf);
			fprintf(f, "%s\" */\n",
				n >= (int) sizeof buf - 1 ? "..." : "");
			singlecase(f, ast, z, z->fsm, s);

			if (s->next != NULL) {
				fprintf(f, "\n");
			}
		}

		fprintf(f, "\t\t}\n");
	}

	fprintf(f, "\t}\n");

	fprintf(f, "\n");

	{
		struct fsm_state *s;

		fprintf(f, "\tlx->lgetc = NULL;\n");

		fprintf(f, "\n");

		fprintf(f, "\tswitch (state) {\n");

		for (s = z->fsm->sl; s != NULL; s = s->next) {
			const struct ast_mapping *m;

			if (!fsm_isend(z->fsm, s)) {
				continue;
			}

			m = s->opaque;

			assert(m != NULL);

			fprintf(f, "\tcase S%u: return ", indexof(z->fsm, s));

			/* note: no point in changing zone here, because getc is now NULL */

			if (m->token == NULL) {
				fprintf(f, "TOK_EOF;\n");
			} else {
				/* TODO: maybe make a printf-like little language to simplify this */
				fprintf(f, "TOK_");
				out_esctok(f, m->token->s);
				fprintf(f, ";\n");
			}
		}

		fprintf(f, "\tdefault: errno = EINVAL; return TOK_ERROR;\n");

		fprintf(f, "\t}\n");
	}

	fprintf(f, "}\n\n");

	return 0;
}

static void
out_name(FILE *f, const struct ast *ast)
{
	struct ast_token *t;

	assert(f != NULL);
	assert(ast != NULL);

	fprintf(f, "const char *\n");
	fprintf(f, "%sname(enum lx_token t)\n", prefix);
	fprintf(f, "{\n");

	fprintf(f, "\tswitch (t) {\n");

	for (t = ast->tl; t != NULL; t = t->next) {
		fprintf(f, "\tcase TOK_");
		out_esctok(f, t->s);
		fprintf(f, ": return \"");
		out_esctok(f, t->s);
		fprintf(f, "\";\n");
	}

	fprintf(f, "\tcase TOK_EOF:     return \"EOF\";\n");
	fprintf(f, "\tcase TOK_ERROR:   return \"ERROR\";\n");
	fprintf(f, "\tcase TOK_UNKNOWN: return \"UNKNOWN\";\n");

	fprintf(f, "\tdefault: return \"?\";\n");

	fprintf(f, "\t}\n");

	fprintf(f, "}\n");
	fprintf(f, "\n");
}

static int
out_example(FILE *f, const struct ast *ast)
{
	struct ast_token *t;
	struct ast_zone *z;

	assert(f != NULL);
	assert(ast != NULL);

	fprintf(f, "const char *\n");
	fprintf(f, "%sexample(enum lx_token (*z)(struct lx *), enum lx_token t)\n", prefix);
	fprintf(f, "{\n");

	fprintf(f, "\tassert(z != NULL);\n");

	fprintf(f, "\n");

	for (z = ast->zl; z != NULL; z = z->next) {
		fprintf(f, "\tif (z == z%u) {\n", zindexof(ast, z));
		fprintf(f, "\t\tswitch (t) {\n");

		for (t = ast->tl; t != NULL; t = t->next) {
			const struct fsm_state *s;
			char buf[50]; /* 50 looks reasonable for an on-screen limit */
			int n;

			s = shortest_example(z->fsm, t);
			if (s == NULL) {
				return -1;
			}

			if (s == z->fsm->start) {
				continue;
			}

			n = fsm_example(z->fsm, s, buf, sizeof buf);
			if (-1 == n) {
				perror("fsm_example");
				return -1;
			}

			fprintf(f, "\t\tcase TOK_");
			out_esctok(f, t->s);
			fprintf(f, ": return \"");
			out_escstr(f, buf);
			fprintf(f, "%s", n >= (int) sizeof buf - 1 ? "..." : "");
			fprintf(f, "\";\n");
		}

		fprintf(f, "\t\tdefault: goto error;\n");

		fprintf(f, "\t\t}\n");

		fprintf(f, "\t}%s\n", z->next ? " else" : "");
	}

	fprintf(f, "\n");
	fprintf(f, "error:\n");
	fprintf(f, "\n");
	fprintf(f, "\terrno = EINVAL;\n");
	fprintf(f, "\treturn NULL;\n");

	fprintf(f, "}\n");
	fprintf(f, "\n");

	return 0;
}

void
lx_out_c(const struct ast *ast, FILE *f)
{
	const struct ast_zone *z;

	assert(f != NULL);

	for (z = ast->zl; z != NULL; z = z->next) {
		if (!fsm_all(z->fsm, fsm_isdfa)) {
			errno = EINVAL;
			return;
		}
	}

	fprintf(f, "/* Generated by lx */\n");	/* TODO: date, input etc */
	fprintf(f, "\n");

	fprintf(f, "#include <assert.h>\n");
	fprintf(f, "#include <stdio.h>\n");
	fprintf(f, "#include <stdlib.h>\n"); /* TODO: for dynbuf only */
	fprintf(f, "#include <errno.h>\n");
	fprintf(f, "\n");

	/* TODO: POSIX only */
	fprintf(f, "#include <unistd.h>\n");
	fprintf(f, "#include <fcntl.h>\n");
	fprintf(f, "\n");

	fprintf(f, "#include LX_HEADER\n");
	fprintf(f, "\n");

	for (z = ast->zl; z != NULL; z = z->next) {
		out_proto(f, ast, z);
	}

	fprintf(f, "\n");

	out_io(f);
	out_lgetc(f);

	out_buf(f);

	for (z = ast->zl; z != NULL; z = z->next) {
		if (-1 == out_zone(f, ast, z)) {
			return; /* XXX: handle error */
		}
	}

	out_name(f, ast);

	if (-1 == out_example(f, ast)) {
		return;
	}

	{
		fprintf(f, "void\n");
		fprintf(f, "%sinit(struct lx *lx)\n", prefix);
		fprintf(f, "{\n");
		fprintf(f, "\tstatic const struct lx lx_default;\n");
		fprintf(f, "\n");
		fprintf(f, "\tassert(lx != NULL);\n");
		fprintf(f, "\n");
		fprintf(f, "\t*lx = lx_default;\n");
		fprintf(f, "\n");
		fprintf(f, "\tlx->c = EOF;\n");
		fprintf(f, "\tlx->z = NULL;\n");
		fprintf(f, "\n");
		fprintf(f, "\tlx->end.byte = 0;\n");
		fprintf(f, "\tlx->end.line = 1;\n");
		fprintf(f, "\tlx->end.col  = 1;\n");
		fprintf(f, "}\n");
		fprintf(f, "\n");
	}

	{
		fprintf(f, "enum lx_token\n");
		fprintf(f, "%snext(struct lx *lx)\n", prefix);
		fprintf(f, "{\n");

		fprintf(f, "\tenum lx_token t;\n");
		fprintf(f, "\n");
		fprintf(f, "\tassert(lx != NULL);\n");
		fprintf(f, "\n");

		fprintf(f, "\tif (lx->lgetc == NULL) {\n");
		fprintf(f, "\t\treturn TOK_EOF;\n");
		fprintf(f, "\t}\n");
		fprintf(f, "\n");

		fprintf(f, "\tif (lx->z == NULL) {\n");
		fprintf(f, "\t\tlx->z = z%u;\n", zindexof(ast, ast->global));
		fprintf(f, "\t}\n");
		fprintf(f, "\n");

		fprintf(f, "\tt = lx->z(lx);\n");
		fprintf(f, "\n");

		fprintf(f, "\tif (lx->push != NULL) {\n");
		fprintf(f, "\t\tif (-1 == lx->push(lx, '\\0')) {\n");
		fprintf(f, "\t\t\treturn TOK_ERROR;\n");
		fprintf(f, "\t\t}\n");
		fprintf(f, "\t}\n");
		fprintf(f, "\n");

		fprintf(f, "\tif (lx->lgetc == NULL && lx->free != NULL) {\n");
		fprintf(f, "\t\tlx->free(lx);\n");
		fprintf(f, "\t}\n");
		fprintf(f, "\n");

		fprintf(f, "\treturn t;\n");

		fprintf(f, "}\n");
	}

	fprintf(f, "\n");
}

