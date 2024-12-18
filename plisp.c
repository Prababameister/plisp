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

#define LASSERT(args, cond, fmt, ...)                                          \
  if (!(cond)) {                                                               \
    lval *err = lval_err(fmt, ##__VA_ARGS__);                                  \
    lval_del(args);                                                            \
    return err;                                                                \
  }

struct lval;
struct lenv;
typedef struct lval lval;
typedef struct lenv lenv;

typedef lval *(*lbuiltin)(lenv *, lval *);

/* Lisp Value type */
enum { LVAL_ERR, LVAL_NUM, LVAL_SYM, LVAL_FUN, LVAL_SEXPR, LVAL_QEXPR };

struct lval {
  int type;

  long num;

  char *err;
  char *sym;
  lbuiltin fun;

  int count;
  struct lval **cell;
};

lval *lval_copy(lval *v) {
  lval *x = malloc(sizeof(lval));
  x->type = v->type;

  switch (v->type) {
  case LVAL_FUN:
    x->fun = v->fun;
    break;
  case LVAL_NUM:
    x->num = v->num;
    break;

  case LVAL_ERR:
    x->err = malloc(strlen(v->err) + 1);
    strcpy(x->err, v->err);
    break;

  case LVAL_SYM:
    x->sym = malloc(strlen(v->sym) + 1);
    strcpy(x->sym, v->sym);
    break;

  case LVAL_SEXPR:
  case LVAL_QEXPR:
    x->count = v->count;
    x->cell = malloc(sizeof(lval *) * x->count);
    for (int i = 0; i < x->count; ++i)
      x->cell[i] = lval_copy(v->cell[i]);
    break;
  }

  return x;
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
  case LVAL_FUN:
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

// Constructors
lval *lval_num(long x) {
  lval *v = malloc(sizeof(lval));
  v->type = LVAL_NUM;
  v->num = x;
  return v;
}
lval *lval_err(char *fmt, ...) {
  lval *v = malloc(sizeof(lval));
  v->type = LVAL_ERR;

  // va list
  va_list va;
  va_start(va, fmt);

  v->err = malloc(512);
  vsnprintf(v->err, 511, fmt, va);

  v->err = realloc(v->err, strlen(v->err) + 1);
  va_end(va);
  return v;
}
lval *lval_sym(char *s) {
  lval *v = malloc(sizeof(lval));
  v->type = LVAL_SYM;
  v->sym = malloc(strlen(s) + 1);
  strcpy(v->sym, s);
  return v;
}
lval *lval_fun(lbuiltin func) {
  lval *v = malloc(sizeof(lval));
  v->type = LVAL_FUN;
  v->fun = func;
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

lval *lval_add(lval *v, lval *x) {
  v->count++;
  v->cell = realloc(v->cell, sizeof(lval *) * v->count);
  v->cell[v->count - 1] = x;
  return v;
}

// lenv stuff
struct lenv {
  int count;
  char **syms;
  lval **vals;
};

lenv *lenv_new(void) {
  lenv *e = malloc(sizeof(lenv));
  e->count = 0;
  e->syms = NULL;
  e->vals = NULL;
  return e;
}
void lenv_del(lenv *e) {
  for (int i = 0; i < e->count; ++i) {
    free(e->syms);
    lval_del(e->vals[i]);
  }
  free(e->syms);
  free(e->vals);
  free(e);
}

lval *lenv_get(lenv *e, lval *k) {
  // Iterate over all symbols in env and return if it matches
  for (int i = 0; i < e->count; ++i)
    if (strcmp(e->syms[i], k->sym) == 0)
      return lval_copy(e->vals[i]);
  return lval_err("Unbound symbol!");
}
void lenv_put(lenv *e, lval *k, lval *v) {
  // Check if this symbol exists
  for (int i = 0; i < e->count; ++i) {
    if (strcmp(e->syms[i], k->sym) == 0) {
      // If found, delete the item at that position and replace with that
      // supplied by the user
      lval_del(e->vals[i]);
      e->vals[i] = lval_copy(v);
      return;
    }
  }

  // For no existing entry
  ++e->count;
  e->syms = realloc(e->syms, sizeof(char *) * e->count);
  e->vals = realloc(e->vals, sizeof(lval *) * e->count);

  e->syms[e->count - 1] = malloc(strlen(k->sym) + 1);
  strcpy(e->syms[e->count - 1], k->sym);
  e->vals[e->count - 1] = lval_copy(v);
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

char *ltype_name(int t) {
  switch (t) {
  case LVAL_FUN:
    return "Function";
  case LVAL_NUM:
    return "Number";
  case LVAL_ERR:
    return "Error";
  case LVAL_SYM:
    return "Symbol";
  case LVAL_SEXPR:
    return "S-Expression";
  case LVAL_QEXPR:
    return "Q-Expression";
  default:
    return "Unknown";
  }
}

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
  case LVAL_FUN:
    printf("<function>");
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

lval *lval_eval(lenv *e, lval *v);

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

lval *builtin_head(lenv *e, lval *a) {
  LASSERT(a, a->count == 1,
          "Function 'head' passed too many arguments! "
          "Got %s. Expected 1",
          a->count);
  LASSERT(a, a->cell[0]->type == LVAL_QEXPR,
          "Function 'head' passed the incorrect types! "
          "Recieved %s instead of %s",
          ltype_name(a->cell[0]->type), ltype_name(LVAL_QEXPR));
  LASSERT(a, a->cell[0]->count != 0, "Function 'head' passed {}!");

  lval *v = lval_take(a, 0);

  while (v->count > 1)
    lval_del(lval_pop(v, 1));

  return v;
}
lval *builtin_tail(lenv *e, lval *a) {
  LASSERT(a, a->count == 1,
          "Function 'tail' passed too many arguments! "
          "Got %s. Expected 1",
          a->count);
  LASSERT(a, a->cell[0]->type == LVAL_QEXPR,
          "Function 'tail' passed the incorrect types! "
          "Recieved %s instead of %s",
          ltype_name(a->cell[0]->type), ltype_name(LVAL_QEXPR));
  LASSERT(a, a->cell[0]->count != 0, "Function 'tail' passed {}!");

  lval *v = lval_take(a, 0);
  lval_del(lval_pop(v, 0));

  return v;
}
lval *builtin_list(lenv *e, lval *a) {
  a->type = LVAL_QEXPR;
  return a;
}
lval *builtin_eval(lenv *e, lval *a) {
  LASSERT(a, a->count == 1,
          "Function 'eval' passed too many arguments! "
          "Got %s. Expected 1",
          a->count);
  LASSERT(a, a->cell[0]->type == LVAL_QEXPR,
          "Function 'eval' passed the incorrect types! "
          "Recieved %s instead of %s",
          ltype_name(a->cell[0]->type), ltype_name(LVAL_QEXPR));

  lval *x = lval_take(a, 0);
  x->type = LVAL_SEXPR;
  return lval_eval(e, x);
}

lval *lval_join(lval *x, lval *y) {
  while (y->count)
    x = lval_add(x, lval_pop(y, 0));

  lval_del(y);
  return x;
}
lval *builtin_join(lenv *e, lval *a) {
  for (int i = 0; i < a->count; ++i)
    LASSERT(a, a->cell[i]->type == LVAL_QEXPR,
            "Function 'join' passed the incorrect types! "
            "Recieved %s instead of %s at index %i",
            ltype_name(a->cell[i]->type), ltype_name(LVAL_QEXPR), i);

  lval *x = lval_pop(a, 0);
  while (a->count)
    x = lval_join(x, lval_pop(a, 0));

  lval_del(a);
  return x;
}

lval *builtin_len(lenv *e, lval *a) {
  LASSERT(a, a->count == 1,
          "Function 'len' passed too many arguments! "
          "Got %s. Expected 1",
          a->count);
  LASSERT(a, a->cell[0]->type == LVAL_QEXPR,
          "Function 'len' passed the incorrect types! "
          "Recieved %s instead of %s",
          ltype_name(a->cell[0]->type), ltype_name(LVAL_QEXPR));

  lval *l = lval_num(a->cell[0]->count);
  lval_del(a);
  return l;
}
lval *builtin_init(lenv *e, lval *a) {
  LASSERT(a, a->count == 1,
          "Function 'init' passed too many arguments! "
          "Got %s. Expected 1",
          a->count);
  LASSERT(a, a->cell[0]->type == LVAL_QEXPR,
          "Function 'init' passed the incorrect types! "
          "Recieved %s instead of %s",
          ltype_name(a->cell[0]->type), ltype_name(LVAL_QEXPR));
  LASSERT(a, a->cell[0]->count != 0, "Function 'head' passed {}!");

  lval *v = lval_take(a, 0);

  lval_del(lval_pop(v, v->count - 1));
  return v;
}

lval *builtin_op(lenv *e, lval *a, char *s) {
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
lval *builtin_add(lenv *e, lval *a) { return builtin_op(e, a, "+"); }
lval *builtin_sub(lenv *e, lval *a) { return builtin_op(e, a, "-"); }
lval *builtin_mul(lenv *e, lval *a) { return builtin_op(e, a, "*"); }
lval *builtin_div(lenv *e, lval *a) { return builtin_op(e, a, "/"); }

lval *builtin_def(lenv *e, lval *a) {
  LASSERT(a, a->cell[0]->type == LVAL_QEXPR,
          "Function 'init' passed the incorrect types! "
          "Recieved %s instead of %s",
          ltype_name(a->cell[0]->type), ltype_name(LVAL_QEXPR));

  lval *syms = a->cell[0];
  for (int i = 0; i < syms->count; ++i)
    LASSERT(a, syms->cell[i]->type == LVAL_SYM,
            "Function 'join' passed the incorrect types! "
            "Recieved %s instead of %s at index %i",
            ltype_name(syms->cell[i]->type), ltype_name(LVAL_SYM), i);

  LASSERT(a, syms->count == a->count - 1,
          "Number of symbols and values do not match!"
          "There are %i symbols vs %i values",
          syms->count, a->count - 1);

  for (int i = 0; i < syms->count; ++i)
    lenv_put(e, syms->cell[i], a->cell[i + 1]);

  lval_del(a);
  return lval_sexpr();
}

lval *lval_eval_sexpr(lenv *e, lval *v) {
  /* Evaluate children */
  for (int i = 0; i < v->count; ++i)
    v->cell[i] = lval_eval(e, v->cell[i]);

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
  if (f->type != LVAL_FUN) {
    lval_del(f);
    lval_del(v);
    return lval_err("First element is not a function!");
  }

  lval *result = f->fun(e, v);
  lval_del(f);
  return result;
}

lval *lval_eval(lenv *e, lval *v) {
  if (v->type == LVAL_SYM) {
    lval *x = lenv_get(e, v);
    lval_del(v);
    return x;
  }
  // Evaluate Sexpressions
  if (v->type == LVAL_SEXPR)
    return lval_eval_sexpr(e, v);
  return v;
}

void lenv_add_builtin(lenv *e, char *name, lbuiltin func) {
  lval *k = lval_sym(name);
  lval *v = lval_fun(func);
  lenv_put(e, k, v);
  lval_del(k);
  lval_del(v);
}

void lenv_add_builtins(lenv *e) {
  // List stuff
  lenv_add_builtin(e, "list", builtin_list);
  lenv_add_builtin(e, "head", builtin_head);
  lenv_add_builtin(e, "tail", builtin_tail);
  lenv_add_builtin(e, "eval", builtin_eval);
  lenv_add_builtin(e, "join", builtin_join);
  lenv_add_builtin(e, "len", builtin_len);
  lenv_add_builtin(e, "init", builtin_init);
  lenv_add_builtin(e, "def", builtin_def);

  // Basic math
  lenv_add_builtin(e, "+", builtin_add);
  lenv_add_builtin(e, "-", builtin_sub);
  lenv_add_builtin(e, "*", builtin_mul);
  lenv_add_builtin(e, "/", builtin_div);
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
              symbol : /[a-zA-Z0-9_+\\-*\\/\\\\=<>!&]+/ ;        \
              sexpr: '(' <expr>* ')';                            \
              qexpr: '{' <expr>* '}';                            \
              expr: <number> | <symbol> | <sexpr> | <qexpr>;     \
              plisp: /^/ <expr>* /$/ ;                           \
            ",
            Number, Symbol, Sexpr, Qexpr, Expr, Plisp);

  // Print version and exit Information
  puts("Welcome to Plisp Version 0.0.0.3");
  puts("Press Ctrl+c to Exit\n");

  lenv *e = lenv_new();
  lenv_add_builtins(e);

  // Keep reading input
  while (1) {
    // Output prompt and get our input
    char *input = readline("plisp> ");

    // Add history to input
    add_history(input);

    // Parse the input text
    mpc_result_t r;
    if (mpc_parse("<stdin>", input, Plisp, &r)) {
      lval *x = lval_eval(e, lval_read(r.output));
      lval_println(x);
      lval_del(x);

      mpc_ast_delete(r.output);
    } else {
      mpc_err_print(r.error);
      mpc_err_delete(r.error);
    }

    // Free input
    free(input);
  }

  lenv_del(e);
  mpc_cleanup(6, Number, Symbol, Sexpr, Qexpr, Expr, Plisp);
  return 0;
}
