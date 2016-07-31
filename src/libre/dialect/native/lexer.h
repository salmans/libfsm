/* Generated by lx */

#ifndef LX_H
#define LX_H

enum lx_native_token {
	TOK_CLOSECOUNT,
	TOK_OPENCOUNT,
	TOK_COUNT,
	TOK_SEP,
	TOK_CLOSEGROUP,
	TOK_OPENGROUP,
	TOK_RANGE,
	TOK_INVERT,
	TOK_CHAR,
	TOK_HEX,
	TOK_OCT,
	TOK_ESC,
	TOK_ALT,
	TOK_DOT,
	TOK_PLUS,
	TOK_STAR,
	TOK_QMARK,
	TOK_END,
	TOK_START,
	TOK_CLOSESUB,
	TOK_OPENSUB,
	TOK_EOF,
	TOK_ERROR,
	TOK_UNKNOWN
};

struct lx_pos {
	unsigned byte;
	unsigned line;
	unsigned col;
};

struct lx_native_lx {
	int (*lgetc)(struct lx_native_lx *lx);
	void *opaque;

	int c; /* lx_native_ungetc buffer */

	struct lx_pos start;
	struct lx_pos end;

	void *buf;
	int  (*push) (struct lx_native_lx *lx, char c);
	void (*pop)  (struct lx_native_lx *lx);
	int  (*clear)(struct lx_native_lx *lx);
	void (*free) (struct lx_native_lx *lx);

	enum lx_native_token (*z)(struct lx_native_lx *lx);
};

/*
 * The initial buffer size; this ought to be over the typical token length,
 * so as to avoid a run-up of lots of resizing.
 */
#ifndef LX_DYN_LOW
#define LX_DYN_LOW 1 << 10
#endif

/*
 * High watermark; if the buffer grows over this, it will resize back down
 * by LX_DYN_FACTOR when no longer in use.
 */
#ifndef LX_DYN_HIGH
#define LX_DYN_HIGH 1 << 13
#endif

/*
 * Andrew Koenig said the growth factor should be less than phi, (1 + sqrt(5)) / 2
 * P.J. Plauger said 1.5 works well in practice. (Perhaps because of internal
 * bookkeeping data stored by the allocator.)
 *
 * Non-integer factors here add the constraint that LX_DYN_LOW > 1 because
 * because conversion to size_t truncates, and e.g. 1 * 1.5 == 1 is no good
 * as the requirement is to *increase* a buffer.
 */
#ifndef LX_DYN_FACTOR
#define LX_DYN_FACTOR 2
#endif

/* dynamic token buffer */
struct lx_dynbuf {
	char *p;
	size_t len;
	char *a;
};

const char *lx_native_name(enum lx_native_token t);
const char *lx_native_example(enum lx_native_token (*z)(struct lx_native_lx *), enum lx_native_token t);

void lx_native_init(struct lx_native_lx *lx);
enum lx_native_token lx_native_next(struct lx_native_lx *lx);

int lx_native_fgetc(struct lx_native_lx *lx);

int  lx_native_dynpush(struct lx_native_lx *lx, char c);
void lx_native_dynpop(struct lx_native_lx *lx);
int  lx_native_dynclear(struct lx_native_lx *lx);
void lx_native_dynfree(struct lx_native_lx *lx);

#endif
