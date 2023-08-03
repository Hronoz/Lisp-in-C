#ifndef LISP_H
#define LISP_H
#include "mpc.h"

typedef struct lenv lenv;
typedef struct lval lval;

enum { LVAL_NUM, LVAL_ERR, LVAL_SYM,
       LVAL_FUN, LVAL_SEXPR, LVAL_QEXPR };

enum { LERR_DIV_ZERO, LERR_BAD_OP, LERR_BAD_NUM };

typedef lval *(*lbuiltin)(lenv *, lval *);

char *ltype_name(size_t t);

void lval_expr_print(lval *v, char open, char close);
lval *lval_evaluate(lenv *e, lval *v);
lval *lval_num(long x);
lval *lval_err(char *fmt, ...);
lval *lval_sym(char *s);
lval *lval_sexpr(void);
lval *lval_qexpr(void);
lval *lval_fun(lbuiltin func);
void lval_delete(lval *v);
lval *lval_add(lval *v, lval *x);
lval *lval_read_num(mpc_ast_t *node);
lval *lval_read(mpc_ast_t *node);
void lval_print(lval *v);
lval *lval_pop(lval *v, size_t i);
lval *lval_take(lval *v, size_t i);
lval *lval_copy(lval *v);
lval *lval_evaluate_sexpr(lenv *e, lval *v);
lval *lval_join(lval *x, lval *y);
lval *lval_evaluate(lenv *e, lval *v);
void lval_expr_print(lval *v, char open, char close);
void lval_println(lval *v);
lval *lval_lambda(lval *formals, lval *body);

lval *builtin_op(lenv *e, lval *a, char *op);
lval *builtin_var(lenv *e, lval *a, char *func);
lval *builtin_head(lenv *e, lval *a);
lval *builtin_tail(lenv *e, lval *a);
lval *builtin_list(lenv *e, lval *a);
lval *builtin_eval(lenv *e, lval *a);
lval *builtin_join(lenv *e, lval *a);
lval *builtin_add(lenv *e, lval *a);
lval *builtin_sub(lenv *e, lval *a);
lval *builtin_mul(lenv *e, lval *a);
lval *builtin_div(lenv *e, lval *a);
lval *builtin_def(lenv *e, lval *a);
lval *builtin_put(lenv *e, lval *a);
lval *builtin_gt(lenv *e, lval *a);
lval *builtin_lt(lenv *e, lval *a);
lval *builtin_ge(lenv *e, lval *a);
lval *builtin_le(lenv *e, lval *a);
lval *builtin_ord(lenv *e, lval *a, char *op);
lval *builtin_cmp(lenv *e, lval *a, char *op);
lval *builtin_if(lenv *e, lval *a);
lval *builtin_lambda(lenv *e, lval *a);
lval *builtin(lenv *e, lval *a, char *func);

lenv *lenv_new(void);
void lenv_delete(lenv *e);
lval *lenv_get(lenv *e, lval *k);
lenv *lenv_copy(lenv *e);
void lenv_put(lenv *e, lval *k, lval *v);
void lenv_add_builtins(lenv *e);
void lenv_add_builtin(lenv *e, char *name, lbuiltin func);
void lenv_def(lenv *e, lval *k, lval *v);

#endif
