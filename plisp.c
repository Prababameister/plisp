#include <stdio.h>
#include <stdlib.h>

#include "mpc.h"

#ifdef _WIN32
#include <string.h>

static char buffer[2048];

// Fake readline function
char *readline(char *prompt) {
  fputs(prompt, stdout);
  fgets(buffer, 2048, stdin);
  char *cpy = malloc(strlen(buffer) + 1);
  strcpy(cpy, buffer);
  cpy[strlen(cpy) - 1] = '\0';
  return cpy;
}

// Imposter add_history function
void add_history(char *unused) {}

#else
#include <editline/readline.h>
#endif

#define LASSERT(args, cond, err)                                               \
  if (!(cond)) {                                                               \
    lval_del(args);                                                            \
    return lval_err(err);                                                      \
  }

/* Lisp Value type */
enum { LVAL_ERR, LVAL_NUM, LVAL_SYM, LVAL_SEXPR, LVAL_QEXPR };

typedef struct lval {
  int type;
  long num;

  char *err;
  char *sym;

  int count;
  struct lval **cell;
} lval;

// Constructors
lval *lval_num(long x) {
  lval *v = malloc(sizeof(lval));
  v->type = LVAL_NUM;
  v->num = x;
  return v;
}
lval *lval_err(char *m) {
  lval *v = malloc(sizeof(lval));
  v->type = LVAL_ERR;
  v->err = malloc(strlen(m) + 1);
  strcpy(v->err, m);
  return v;
}
lval *lval_sym(char *s) {
  lval *v = malloc(sizeof(lval));
  v->type = LVAL_SYM;
  v->sym = malloc(strlen(s) + 1);
  strcpy(v->sym, s);
  return v;
}
lval *lval_sexpr(void) {
  lval *v = malloc(sizeof(lval));
  v->type = LVAL_SEXPR;
  v->count = 0;
  v->cell = NULL;
  // return null sexpr
  return v;
}
lval *lval_qexpr(void) {
  lval *v = malloc(sizeof(lval));
  v->type = LVAL_QEXPR;
  v->count = 0;
  v->cell = NULL;
  // return null sexpr
  return v;
}

// Destructor
void lval_del(lval *v) {
  switch (v->type) {
  case LVAL_NUM:
    break;
  case LVAL_ERR:
    free(v->err);
    break;
  case LVAL_SYM:
    free(v->sym);
    break;
  case LVAL_SEXPR:
  case LVAL_QEXPR:
    for (int i = 0; i < v->count; ++i)
      lval_del(v->cell[i]);

    free(v->cell);
    break;
  }

  free(v);
}

lval *lval_add(lval *v, lval *x) {
  v->count++;
  v->cell = realloc(v->cell, sizeof(lval *) * v->count);
  v->cell[v->count - 1] = x;
  return v;
}

lval *lval_read_num(mpc_ast_t *t) {
  errno = 0;
  long x = strtol(t->contents, NULL, 10);
  return errno != ERANGE ? lval_num(x) : lval_err("Invalid number");
}

lval *lval_read(mpc_ast_t *t) {
  // If symbol or number is read, just return that
  if (strstr(t->tag, "number"))
    return lval_read_num(t);
  if (strstr(t->tag, "symbol"))
    return lval_sym(t->contents);

  lval *x = NULL;
  // If empty (just '>') or a sexpr, then create an initial empty list
  if (strstr(t->tag, "sexpr") || strcmp(t->tag, ">") == 0)
    x = lval_sexpr();
  if (strstr(t->tag, "qexpr"))
    x = lval_qexpr();

  for (int i = 1; i < t->children_num - 1; ++i) {
    if (strcmp(t->children[i]->contents, "(") == 0)
      continue;
    if (strcmp(t->children[i]->contents, ")") == 0)
      continue;
    if (strcmp(t->children[i]->contents, "{") == 0)
      continue;
    if (strcmp(t->children[i]->contents, "}") == 0)
      continue;
    if (strcmp(t->children[i]->contents, "regex") == 0)
      continue;
    x = lval_add(x, lval_read(t->children[i]));
  }

  return x;
}

// Print lval
void lval_print(lval *v);

void lval_expr_print(lval *v, char open, char close) {
  putchar(open);
  for (int i = 0; i < v->count; ++i) {
    /* Print value contained */
    lval_print(v->cell[i]);

    /* Don't print space if last element */
    if (i != (v->count - 1))
      putchar(' ');
  }
  putchar(close);
}

void lval_print(lval *v) {
  switch (v->type) {
  case LVAL_NUM:
    printf("%li", v->num);
    break;
  case LVAL_ERR:
    printf("Error: %s", v->err);
    break;
  case LVAL_SYM:
    printf("%s", v->sym);
    break;
  case LVAL_SEXPR:
    lval_expr_print(v, '(', ')');
    break;
  case LVAL_QEXPR:
    lval_expr_print(v, '{', '}');
    break;
  }
}
// Print lval with a newline appended
void lval_println(lval *v) {
  lval_print(v);
  putchar('\n');
}
/* Lisp Value defintion end */

lval *lval_eval(lval *v);

// Remove element
lval *lval_pop(lval *v, int i) {
  lval *x = v->cell[i];

  memmove(&v->cell[i], &v->cell[i + 1], sizeof(lval *) * (v->count - i - 1));

  --v->count;
  v->cell = realloc(v->cell, sizeof(lval *) * v->count);
  return x;
}

lval *lval_take(lval *v, int i) {
  lval *x = lval_pop(v, i);
  lval_del(v);
  return x;
}
// ------

lval *builtin_head(lval *a) {
  LASSERT(a, a->count == 1, "Function 'head' passed too many arguments!");
  LASSERT(a, a->cell[0]->type == LVAL_QEXPR,
          "Function 'head' passed the incorrect types!");
  LASSERT(a, a->cell[0]->count != 0, "Function 'head' passed {}!");

  lval *v = lval_take(a, 0);

  while (v->count > 1)
    lval_del(lval_pop(v, 1));

  return v;
}
lval *builtin_tail(lval *a) {
  LASSERT(a, a->count == 1, "Function 'tail' passed too many arguments!");
  LASSERT(a, a->cell[0]->type == LVAL_QEXPR,
          "Function 'tail' passed the incorrect types!");
  LASSERT(a, a->cell[0]->count != 0, "Function 'tail' passed {}!");

  lval *v = lval_take(a, 0);
  lval_del(lval_pop(v, 0));

  return v;
}
lval *builtin_list(lval *a) {
  a->type = LVAL_QEXPR;
  return a;
}
lval *builtin_eval(lval *a) {
  LASSERT(a, a->count == 1, "Function 'eval' passed too many arguments!");
  LASSERT(a, a->cell[0]->type == LVAL_QEXPR,
          "Function 'eval' passed the incorrect types!");

  lval *x = lval_take(a, 0);
  x->type = LVAL_SEXPR;
  return lval_eval(x);
}

lval *lval_join(lval *x, lval *y) {
  while (y->count)
    x = lval_add(x, lval_pop(y, 0));

  lval_del(y);
  return x;
}
lval *builtin_join(lval *a) {
  for (int i = 0; i < a->count; ++i)
    LASSERT(a, a->cell[i]->type == LVAL_QEXPR,
            "Function 'join' passed incorrect type.");

  lval *x = lval_pop(a, 0);
  while (a->count)
    x = lval_join(x, lval_pop(a, 0));

  lval_del(a);
  return x;
}

lval *builtin_len(lval *a) {
  LASSERT(a, a->count == 1, "Function 'len' passed too many arguments!");
  LASSERT(a, a->cell[0]->type == LVAL_QEXPR,
          "Function 'len' passed the incorrect types!");

  lval *l = lval_num(a->cell[0]->count);
  lval_del(a);
  return l;
}
lval *builtin_init(lval *a) {
  LASSERT(a, a->count == 1, "Function 'init' passed too many arguments!");
  LASSERT(a, a->cell[0]->type == LVAL_QEXPR,
          "Function 'init' passed the incorrect types!");
  LASSERT(a, a->cell[0]->count != 0, "Function 'head' passed {}!");

  lval *v = lval_take(a, 0);

  lval_del(lval_pop(v, v->count - 1));
  return v;
}

lval *builtin_op(lval *a, char *s) {
  // Make sure all numbers
  for (int i = 0; i < a->count; ++i) {
    if (a->cell[i]->type != LVAL_NUM) {
      lval_del(a);
      return lval_err("Cannot operate on non-number!");
    }
  }

  // Remove first element
  lval *x = lval_pop(a, 0);

  if ((strcmp(s, "-") == 0) && a->count == 0)
    x->num = -x->num;

  while (a->count > 0) {
    lval *y = lval_pop(a, 0);

    if (strcmp(s, "+") == 0)
      x->num += y->num;
    if (strcmp(s, "-") == 0)
      x->num -= y->num;
    if (strcmp(s, "*") == 0)
      x->num *= y->num;
    if (strcmp(s, "/") == 0) {
      if (y->num == 0) {
        lval_del(x);
        lval_del(y);
        x = lval_err("Division by zero!");
        break;
      }
      x->num /= y->num;
    }

    lval_del(y);
  }

  lval_del(a);
  return x;
}

lval *builtin(lval *a, char *func) {
  if (strcmp("list", func) == 0)
    return builtin_list(a);
  if (strcmp("head", func) == 0)
    return builtin_head(a);
  if (strcmp("tail", func) == 0)
    return builtin_tail(a);
  if (strcmp("join", func) == 0)
    return builtin_join(a);
  if (strcmp("eval", func) == 0)
    return builtin_eval(a);
  if (strcmp("len", func) == 0)
    return builtin_len(a);
  if (strcmp("init", func) == 0)
    return builtin_init(a);

  if (strstr("+-*/", func))
    return builtin_op(a, func);

  lval_del(a);
  return lval_err("Unknown function!");
}

lval *lval_eval_sexpr(lval *v) {
  /* Evaluate children */
  for (int i = 0; i < v->count; ++i)
    v->cell[i] = lval_eval(v->cell[i]);

  // Error checking
  for (int i = 0; i < v->count; ++i)
    if (v->cell[i]->type == LVAL_ERR)
      return lval_take(v, i);

  // Empty Expression
  if (v->count == 0)
    return v;

  // Single Expression
  if (v->count == 1)
    return lval_take(v, 0);

  // Make sure that first element is symbol
  lval *f = lval_pop(v, 0);
  if (f->type != LVAL_SYM) {
    lval_del(f);
    lval_del(v);
    return lval_err("S-expression does not start with symbol!");
  }

  lval *result = builtin(v, f->sym);
  lval_del(f);
  return result;
}

lval *lval_eval(lval *v) {
  // Evaluate Sexpressions
  if (v->type == LVAL_SEXPR)
    return lval_eval_sexpr(v);
  return v;
}

int main() {
  // Create parsers
  mpc_parser_t *Number = mpc_new("number");
  mpc_parser_t *Symbol = mpc_new("symbol");
  mpc_parser_t *Sexpr = mpc_new("sexpr");
  mpc_parser_t *Qexpr = mpc_new("qexpr");
  mpc_parser_t *Expr = mpc_new("expr");
  mpc_parser_t *Plisp = mpc_new("plisp");

  // Define the grammer
  mpca_lang(MPCA_LANG_DEFAULT, "                                 \
              number : /-?[0-9]+/ ;                              \
              symbol : \"list\" | \"head\" | \"tail\"            \
                     | \"join\" | \"eval\" | \"len\"             \
                     | \"init\"                                  \
                     | '+' | '-' | '*' | '/' ;                   \
              sexpr: '(' <expr>* ')';                            \
              qexpr: '{' <expr>* '}';                            \
              expr: <number> | <symbol> | <sexpr> | <qexpr>;     \
              plisp: /^/ <expr>* /$/ ;                           \
            ",
            Number, Symbol, Sexpr, Qexpr, Expr, Plisp);

  // Print version and exit Information
  puts("Welcome to Plisp Version 0.0.0.3");
  puts("Press Ctrl+c to Exit\n");

  // Keep reading input
  while (1) {
    // Output prompt and get our input
    char *input = readline("plisp> ");

    // Add history to input
    add_history(input);

    // Parse the input text
    mpc_result_t r;
    if (mpc_parse("<stdin>", input, Plisp, &r)) {
      lval *x = lval_eval(lval_read(r.output));
      lval_println(x);
      lval_del(x);
    } else {
      mpc_err_print(r.error);
      mpc_err_delete(r.error);
    }

    // Free input
    free(input);
  }

  mpc_cleanup(6, Number, Symbol, Sexpr, Qexpr, Expr, Plisp);
  return 0;
}