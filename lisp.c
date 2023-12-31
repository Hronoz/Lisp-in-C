#include "lisp.h"
#include "mpc.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define LASSERT(args, cond, fmt, ...)                                  \
    do {                                                               \
        if (!(cond)) {                                                 \
            lval *err = lval_err(fmt, ##__VA_ARGS__);                  \
            lval_delete(args);                                         \
            return err;                                                \
        }                                                              \
    } while (0)

#define LASSERT_TYPE(func, args, index, expect)                        \
    LASSERT(args, args->cell[index]->type == expect,                   \
            "Function '%s' passed incorrect type "                     \
            "for argument %i. Got %s, Expected %s.",                   \
            func, index, ltype_name(args->cell[index]->type),          \
            ltype_name(expect))

#define LASSERT_NUM(func, args, num)                                   \
    LASSERT(args, args->count == num,                                  \
            "Function '%s' passed incorrect "                          \
            "number of arguments. Got %i, Expected %i.",               \
            func, args->count, num)

#define LASSERT_NOT_EMPTY(func, args, index)                           \
    LASSERT(args, args->cell[index]->count != 0,                       \
            "Function '%s' passed {} for argument %i.", func, index);

mpc_parser_t *Number;
mpc_parser_t *Symbol;
mpc_parser_t *Comment;
mpc_parser_t *Sexpr;
mpc_parser_t *Qexpr;
mpc_parser_t *String;
mpc_parser_t *Expr;
mpc_parser_t *Lispy;

char *ltype_name(size_t t)
{
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
    case LVAL_STR:
        return "String";
    default:
        return "Unknown";
    }
}

lval *lval_num(long x)
{
    lval *v = malloc(sizeof(lval));
    v->type = LVAL_NUM;
    v->num = x;
    return v;
}

lval *lval_err(char *fmt, ...)
{
    lval *v = malloc(sizeof(lval));
    va_list va;
    v->type = LVAL_ERR;

    va_start(va, fmt);

    v->err = malloc(512);
    vsnprintf(v->err, 511, fmt, va);
    v->err = realloc(v->err, strlen(v->err) + 1);

    va_end(va);

    return v;
}

lval *lval_sym(char *s)
{
    lval *v = malloc(sizeof(lval));
    v->type = LVAL_SYM;
    v->sym = malloc(strlen(s) + 1);
    strcpy(v->sym, s);
    return v;
}

lval *lval_sexpr(void)
{
    lval *v = malloc(sizeof(lval));

    v->type = LVAL_SEXPR;
    v->count = 0;
    v->cell = NULL;

    return v;
}

lval *lval_qexpr(void)
{
    lval *v = malloc(sizeof(lval));

    v->type = LVAL_QEXPR;
    v->count = 0;
    v->cell = NULL;

    return v;
}

lval *lval_str(char *s)
{
    lval *v = malloc(sizeof(lval));
    v->type = LVAL_STR;
    v->str = malloc(strlen(s) + 1);
    strcpy(v->str, s);

    return v;
}

lval *lval_builtin(lbuiltin func)
{
    lval *v = malloc(sizeof(lval));
    v->type = LVAL_FUN;
    v->builtin = func;
    return v;
}

lval *lval_lambda(lval *formals, lval *body)
{
    lval *v = malloc(sizeof(lval));

    v->type = LVAL_FUN;

    v->builtin = NULL;

    /* TODO: spent 2 hours debbuging this line */
    /* it was: v->env = NULL; */
    v->env = lenv_new();

    v->formals = formals;
    v->body = body;

    return v;
}

void lval_delete(lval *v)
{
    switch (v->type) {
    case LVAL_NUM:
        break;
    case LVAL_FUN:
        if (!v->builtin) {
            lenv_delete(v->env);
            lval_delete(v->formals);
            lval_delete(v->body);
        }
        break;
    case LVAL_ERR:
        free(v->err);
        break;
    case LVAL_SYM:
        free(v->sym);
        break;
    case LVAL_STR:
        free(v->str);
        break;
    case LVAL_QEXPR:
    case LVAL_SEXPR:
        for (size_t i = 0; i < v->count; i++) {
            lval_delete(v->cell[i]);
        }
        free(v->cell);
        break;
    }
    free(v);
}

lval *lval_call(lenv *e, lval *f, lval *a)
{
    int given;
    int total;

    if (f->builtin) {
        return f->builtin(e, a);
    }

    given = a->count;
    total = f->formals->count;

    while (a->count) {
        lval *sym;

        if (f->formals->count == 0) {
            lval_delete(a);
            return lval_err("Function passed too many arguments: "
                            "got %i, expected %i.",
                            given, total);
        }

        sym = lval_pop(f->formals, 0);

        if (strcmp(sym->sym, "&") == 0) {

            if (f->formals->count != 1) {
                lval_delete(a);
                return lval_err("Function format invalid. "
                                "Symbol '&' not followed by single symbol.");
            }

            lval *nsym = lval_pop(f->formals, 0);
            lenv_put(f->env, nsym, builtin_list(e, a));
            lval_delete(sym);
            lval_delete(nsym);
            break;
        }

        /* Pop the next argument from the list */
        lval *val = lval_pop(a, 0);

        /* Bind a copy into the function's environment */
        lenv_put(f->env, sym, val);

        /* Delete symbol and value */
        lval_delete(sym);
        lval_delete(val);
    }

    /* Argument list is now bound so can be cleaned up */
    lval_delete(a);

    /* If '&' remains in formal list bind to empty list */
    if (f->formals->count > 0 && strcmp(f->formals->cell[0]->sym, "&") == 0) {

        /* Check to ensure that & is not passed invalidly. */
        if (f->formals->count != 2) {
            return lval_err("Function format invalid. "
                            "Symbol '&' not followed by single symbol.");
        }

        /* Pop and delete '&' symbol */
        lval_delete(lval_pop(f->formals, 0));

        /* Pop next symbol and create empty list */
        lval *sym = lval_pop(f->formals, 0);
        lval *val = lval_qexpr();

        /* Bind to environment and delete */
        lenv_put(f->env, sym, val);
        lval_delete(sym);
        lval_delete(val);
    }

    /* If all formals have been bound evaluate */
    if (f->formals->count == 0) {

        /* Set environment parent to evaluation environment */
        f->env->par = e;

        /* Evaluate and return */
        return builtin_eval(f->env, lval_add(lval_sexpr(), lval_copy(f->body)));
    } else {
        /* Otherwise return partially evaluated function */
        return lval_copy(f);
    }
}

lval *lval_add(lval *v, lval *x)
{
    v->count++;
    v->cell = realloc(v->cell, sizeof(lval *) * v->count);
    v->cell[v->count - 1] = x;
    return v;
}

lval *lval_read_num(mpc_ast_t *node)
{
    long x = strtol(node->contents, NULL, 10);

    errno = 0;
    return errno != ERANGE ? lval_num(x) : lval_err("Invalid number");
}

lval *lval_read_str(mpc_ast_t *node)
{
    char *unescaped;
    lval *str;

    node->contents[strlen(node->contents) - 1] = '\0';
    unescaped = malloc(strlen(node->contents + 1) + 1);
    strcpy(unescaped, node->contents + 1);
    unescaped = mpcf_unescape(unescaped);
    str = lval_str(unescaped);
    free(unescaped);

    return str;
}

lval *lval_read(mpc_ast_t *node)
{
    lval *x = NULL;

    if (strstr(node->tag, "number")) {
        return lval_read_num(node);
    }
    if (strstr(node->tag, "symbol")) {
        return lval_sym(node->contents);
    }
    if (strstr(node->tag, "string")) {
        return lval_read_str(node);
    }

    if (strcmp(node->tag, ">") == 0) {
        x = lval_sexpr();
    }
    if (strstr(node->tag, "sexpr")) {
        x = lval_sexpr();
    }
    if (strstr(node->tag, "qexpr")) {
        x = lval_qexpr();
    }

    for (int i = 0; i < node->children_num; i++) {
        if (strcmp(node->children[i]->contents, "(") == 0) {
            continue;
        }
        if (strcmp(node->children[i]->contents, ")") == 0) {
            continue;
        }
        if (strcmp(node->children[i]->contents, "{") == 0) {
            continue;
        }
        if (strcmp(node->children[i]->contents, "}") == 0) {
            continue;
        }
        if (strcmp(node->children[i]->tag, "regex") == 0) {
            continue;
        }
        if (strstr(node->tag, "comment")) {
            continue;
        }

        x = lval_add(x, lval_read(node->children[i]));
    }

    return x;
}

void lval_print_str(lval *v)
{
    char *escaped = malloc(strlen(v->str) + 1);
    strcpy(escaped, v->str);
    escaped = mpcf_escape(escaped);
    printf("\"%s\"", escaped);
    free(escaped);
}

void lval_print(lval *v)
{
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
    case LVAL_STR:
        lval_print_str(v);
        break;
    case LVAL_FUN:
        if (v->builtin) {
            printf("<builtin>");
        } else {
            printf("(\\ ");
            lval_print(v->formals);
            putchar(' '); /* TODO: use format string */
            lval_print(v->body);
            putchar(')');
        }
        break;
    }
}

lval *lval_pop(lval *v, size_t i)
{
    lval *x = v->cell[i];

    memmove(&v->cell[i], &v->cell[i + 1], sizeof(lval *) * (v->count - i - 1));

    v->count--;
    v->cell = realloc(v->cell, sizeof(lval *) * v->count);

    return x;
}

lval *lval_take(lval *v, size_t i)
{
    lval *x = lval_pop(v, i);
    lval_delete(v);

    return x;
}

lval *lval_copy(lval *v)
{
    lval *x = malloc(sizeof(lval));
    x->type = v->type;

    switch (v->type) {
    case LVAL_FUN:
        if (v->builtin) {
            x->builtin = v->builtin;
        } else {
            x->builtin = NULL;
            x->env = lenv_copy(v->env);
            x->formals = lval_copy(v->formals);
            x->body = lval_copy(v->body);
        }
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
    case LVAL_STR:
        x->str = malloc(strlen(v->str) + 1);
        strcpy(x->str, v->str);
        break;
    case LVAL_SEXPR:
    case LVAL_QEXPR:
        x->count = v->count;
        x->cell = malloc(sizeof(lval *) * x->count);
        for (size_t i = 0; i < x->count; i++) {
            x->cell[i] = lval_copy(v->cell[i]);
        }
        break;
    }

    return x;
}

lval *lval_evaluate_sexpr(lenv *e, lval *v)
{
    lval *f;
    lval *result;

    for (size_t i = 0; i < v->count; i++) {
        v->cell[i] = lval_evaluate(e, v->cell[i]);
    }

    for (size_t i = 0; i < v->count; i++) {
        if (v->cell[i]->type == LVAL_ERR) {
            return lval_take(v, i);
        }
    }

    if (v->count == 0) {
        return v;
    }
    if (v->count == 1) {
        return lval_evaluate(e, lval_take(v, 0));
    }

    f = lval_pop(v, 0);
    if (f->type != LVAL_FUN) {
        lval *err = lval_err("S-Expression starts with incorrect type: "
                             "got %s, expected %s.",
                             ltype_name(f->type), ltype_name(LVAL_FUN));

        lval_delete(f);
        lval_delete(v);

        return err;
    }

    result = lval_call(e, f, v);

    lval_delete(f);

    return result;
}

lval *lval_join(lval *x, lval *y)
{
    while (y->count) {
        x = lval_add(x, lval_pop(y, 0));
    }

    lval_delete(y);

    return x;
}

int lval_eq(lval *x, lval *y)
{
    if (x->type != y->type) {
        return 0;
    }

    switch (x->type) {
    case LVAL_NUM:
        return x->num == y->num;
        break;
    case LVAL_ERR:
        return strcmp(x->err, y->err) == 0;
        break;
    case LVAL_SYM:
        return strcmp(x->sym, y->sym) == 0;
        break;
    case LVAL_STR:
        return strcmp(x->str, y->str) == 0;
    case LVAL_FUN:
        if (x->builtin || y->builtin) {
            return x->builtin == y->builtin;
        }
        return lval_eq(x->formals, y->formals) && lval_eq(x->body, y->body);
        break;
    case LVAL_QEXPR:
    case LVAL_SEXPR:
        if (x->count != y->count) {
            return 0;
        }
        for (size_t i = 0; i < x->count; i++) {
            if (!lval_eq(x->cell[i], y->cell[i])) {
                return 0;
            }
        }
        return 1;
    }

    return 0;
}

lval *lval_evaluate(lenv *e, lval *v)
{
    if (v->type == LVAL_SYM) {
        lval *x = lenv_get(e, v);
        lval_delete(v);
        return x;
    }

    if (v->type == LVAL_SEXPR) {
        return lval_evaluate_sexpr(e, v);
    }

    return v;
}

void lval_expr_print(lval *v, char open, char close)
{
    putchar(open);
    for (size_t i = 0; i < v->count; i++) {
        lval_print(v->cell[i]);

        if (i != (v->count - 1)) {
            putchar(' ');
        }
    }
    putchar(close);
}

void lval_println(lval *v)
{
    lval_print(v);
    putchar('\n');
}

lval *builtin_op(lenv *e, lval *a, char *op)
{
    lval *x;

    for (size_t i = 0; i < a->count; i++) {
        if (a->cell[i]->type != LVAL_NUM) {
            lval_delete(a);

            return lval_err("Can't operate on non-number!");
        }
    }

    x = lval_pop(a, 0);

    if ((strcmp(op, "-") == 0) && a->count == 0) {
        x->num = -(x->num);
    }

    while (a->count > 0) {
        lval *y = lval_pop(a, 0);

        if (strcmp(op, "+") == 0) {
            x->num += y->num;
        }
        if (strcmp(op, "-") == 0) {
            x->num -= y->num;
        }
        if (strcmp(op, "*") == 0) {
            x->num *= y->num;
        }
        if (strcmp(op, "/") == 0) {
            if (y->num == 0) {
                lval_delete(x);
                lval_delete(y);
                x = lval_err("Division by zero.");
                break;
            }
            x->num /= y->num;
        }

        lval_delete(y);
    }

    lval_delete(a);

    return x;
}

lval *builtin_load(lenv *e, lval *a)
{
    mpc_result_t r;

    LASSERT_NUM("load", a, 1);
    LASSERT_TYPE("load", a, 0, LVAL_STR);

    if (mpc_parse_contents(a->cell[0]->str, Lispy, &r)) {
        lval *expr = lval_read(r.output);
        mpc_ast_delete(r.output);

        while (expr->count) {
            lval *x = lval_evaluate(e, lval_pop(expr, 0));
            if (x->type == LVAL_ERR) {
                lval_println(x);
            }
            lval_delete(x);
        }

        lval_delete(expr);
        lval_delete(a);
        
        return lval_sexpr();
    } else {
        lval *err;
        char *err_msg = mpc_err_string(r.error);
        mpc_err_delete(r.error);
        err = lval_err("Could not load library %s", err_msg);
        free(err_msg);
        lval_delete(a);

        return err;
    }
}

lval *builtin_print(lenv *e, lval *a)
{
    for (size_t i = 0; i < a->count; i++) {
        lval_print(a->cell[i]);
        putchar(' ');
    }
    putchar('\n');
    lval_delete(a);

    return lval_sexpr();
}

lval *builtin_error(lenv *e, lval *a)
{
    lval *err;

    LASSERT_NUM("error", a, 1);
    LASSERT_TYPE("error", a, 0, LVAL_STR);

    err = lval_err(a->cell[0]->str);
    lval_delete(a);

    return err;
}


lval *builtin_add(lenv *e, lval *a) { return builtin_op(e, a, "+"); }

lval *builtin_sub(lenv *e, lval *a) { return builtin_op(e, a, "-"); }

lval *builtin_mul(lenv *e, lval *a) { return builtin_op(e, a, "*"); }

lval *builtin_div(lenv *e, lval *a) { return builtin_op(e, a, "/"); }

lval *builtin_gt(lenv *e, lval *a) { return builtin_ord(e, a, ">"); }

lval *builtin_lt(lenv *e, lval *a) { return builtin_ord(e, a, "<"); }

lval *builtin_ge(lenv *e, lval *a) { return builtin_ord(e, a, ">="); }

lval *builtin_le(lenv *e, lval *a) { return builtin_ord(e, a, "<="); }

lval *builtin_eq(lenv *e, lval *a) { return builtin_cmp(e, a, "=="); }

lval *builtin_ne(lenv *e, lval *a) { return builtin_cmp(e, a, "!="); }

lval *builtin_ord(lenv *e, lval *a, char *op)
{
    int r;

    LASSERT_NUM(op, a, 2);
    LASSERT_TYPE(op, a, 0, LVAL_NUM);
    LASSERT_TYPE(op, a, 1, LVAL_NUM);

    if (strcmp(op, ">") == 0) {
        r = a->cell[0]->num > a->cell[1]->num;
    }
    if (strcmp(op, "<") == 0) {
        r = a->cell[0]->num < a->cell[1]->num;
    }
    if (strcmp(op, ">=") == 0) {
        r = a->cell[0]->num >= a->cell[1]->num;
    }
    if (strcmp(op, "<=") == 0) {
        r = a->cell[0]->num <= a->cell[1]->num;
    }

    lval_delete(a);

    return lval_num(r);
}

lval *builtin_cmp(lenv *e, lval *a, char *op)
{
    int r;
    LASSERT_NUM(op, a, 2);

    if (strcmp(op, "==") == 0) {
        r = lval_eq(a->cell[0], a->cell[1]);
    }

    if (strcmp(op, "!=") == 0) {
        r = !lval_eq(a->cell[0], a->cell[1]);
    }

    lval_delete(a);

    return lval_num(r);
}

lval *builtin_if(lenv *e, lval *a)
{
    lval *x;

    LASSERT_NUM("if", a, 3);
    LASSERT_TYPE("if", a, 0, LVAL_NUM);
    LASSERT_TYPE("if", a, 1, LVAL_QEXPR);
    LASSERT_TYPE("if", a, 2, LVAL_QEXPR);

    a->cell[1]->type = LVAL_SEXPR;
    a->cell[2]->type = LVAL_SEXPR;

    if (a->cell[0]->num) {
        x = lval_evaluate(e, lval_pop(a, 1));
    } else {
        x = lval_evaluate(e, lval_pop(a, 2));
    }

    lval_delete(a);

    return x;
}

lval *builtin_head(lenv *e, lval *a)
{
    lval *v;

    LASSERT(a, a->count == 1,
            "Function 'head' passed too many arguments: "
            "got %i, expected %i.",
            a->count, 1);
    LASSERT(a, a->cell[0]->type == LVAL_QEXPR,
            "Function 'head' passed incorrect type"
            "Got %s, expected %s.",
            ltype_name(a->cell[0]->type), ltype_name(LVAL_SEXPR));
    LASSERT(a, a->count == 1, "Function 'head' passed {}");

    v = lval_take(a, 0);

    while (v->count > 1) {
        lval_delete(lval_pop(v, 1));
    }

    return v;
}

lval *builtin_tail(lenv *e, lval *a)
{
    lval *v;

    LASSERT(a, a->count == 1,
            "Function 'tail' passed too many arguments: "
            "got %i, expected %i.",
            a->count, 1);
    LASSERT(a, a->cell[0]->type == LVAL_QEXPR,
            "Function 'tail' passed incorrect type"
            "Got %s, expected %s.",
            ltype_name(a->cell[0]->type), ltype_name(LVAL_SEXPR));
    LASSERT(a, a->count == 1, "Function 'tail' passed {}");

    v = lval_take(a, 0);

    lval_delete(lval_pop(v, 0));

    return v;
}

lval *builtin_list(lenv *e, lval *a)
{
    a->type = LVAL_QEXPR;

    return a;
}

lval *builtin_eval(lenv *e, lval *a)
{
    lval *x;

    LASSERT(a, a->count == 1,
            "Function 'eval' passed too many arguments: "
            "got %i, expected %i.",
            a->count, 1);
    LASSERT(a, a->cell[0]->type == LVAL_QEXPR,
            "Function 'eval' passed incorrect type"
            "Got %s, expected %s.",
            ltype_name(a->cell[0]->type), ltype_name(LVAL_SEXPR));

    x = lval_take(a, 0);
    x->type = LVAL_SEXPR;

    return lval_evaluate(e, x);
}

lval *builtin_join(lenv *e, lval *a)
{
    lval *x;
    for (size_t i = 0; i < a->count; i++) {
        LASSERT(a, a->cell[i]->type == LVAL_QEXPR,
                "Function 'join' passed incorrect type"
                "Got %s, expected %s.",
                ltype_name(a->cell[0]->type), ltype_name(LVAL_SEXPR));
    }

    x = lval_pop(a, 0);

    while (a->count) {
        x = lval_join(x, lval_pop(a, 0));
    }

    lval_delete(a);

    return x;
}

lval *builtin_var(lenv *e, lval *a, char *func)
{
    lval *syms = a->cell[0];

    LASSERT_TYPE(func, a, 0, LVAL_QEXPR);

    for (size_t i = 0; i < syms->count; i++) {
        LASSERT(a, (syms->cell[i]->type == LVAL_SYM),
                "Function '%s' can't define non-symbol: "
                "got %s, expected %s.",
                func, ltype_name(syms->cell[i]->type), ltype_name(LVAL_SYM));
    }

    LASSERT(a, (syms->count == a->count - 1),
            "Function '%s' passed too many arguments for symbols: "
            "got %s, expected %s",
            func, syms->count, a->count - 1);

    for (size_t i = 0; i < syms->count; i++) {
        if (strcmp(func, "def") == 0) {
            lenv_def(e, syms->cell[i], a->cell[i + 1]);
        }

        if (strcmp(func, "def") == 0) {
            lenv_put(e, syms->cell[i], a->cell[i + 1]);
        }
    }

    lval_delete(a);

    return lval_sexpr();
}

lval *builtin_def(lenv *e, lval *a) { return builtin_var(e, a, "def"); }

lval *builtin_put(lenv *e, lval *a) { return builtin_var(e, a, "="); }

lval *builtin_lambda(lenv *e, lval *a)
{
    lval *formals;
    lval *body;

    LASSERT_NUM("\\", a, 2);
    LASSERT_TYPE("\\", a, 0, LVAL_QEXPR);
    LASSERT_TYPE("\\", a, 1, LVAL_QEXPR);

    for (size_t i = 0; i < a->cell[0]->count; i++) {
        LASSERT(a, (a->cell[0]->cell[i]->type == LVAL_SYM),
                "Can't define non-symbol: got %s, expected %s.",
                ltype_name(a->cell[0]->cell[i]->type), ltype_name(LVAL_SYM));
    }

    formals = lval_pop(a, 0);
    body = lval_pop(a, 0);
    lval_delete(a);

    return lval_lambda(formals, body);
}

lval *builtin(lenv *e, lval *a, char *func)
{
    if (strcmp("list", func) == 0) {
        return builtin_list(e, a);
    }
    if (strcmp("head", func) == 0) {
        return builtin_head(e, a);
    }
    if (strcmp("tail", func) == 0) {
        return builtin_tail(e, a);
    }
    if (strcmp("join", func) == 0) {
        return builtin_join(e, a);
    }
    if (strcmp("eval", func) == 0) {
        return builtin_eval(e, a);
    }

    if (strstr("+-*/", func)) {
        return builtin_op(e, a, func);
    }

    lval_delete(a);

    return lval_err("Unknown function");
}

lenv *lenv_new(void)
{
    lenv *e = malloc(sizeof(lenv));

    e->par = NULL;
    e->count = 0;
    e->syms = NULL;
    e->vals = NULL;

    return e;
}

void lenv_delete(lenv *e)
{
    for (size_t i = 0; i < e->count; i++) {
        free(e->syms[i]);
        lval_delete(e->vals[i]);
    }
    free(e->syms);
    free(e->vals);
    free(e);
}

lval *lenv_get(lenv *e, lval *k)
{
    for (size_t i = 0; i < e->count; i++) {
        if (strcmp(e->syms[i], k->sym) == 0) {
            return lval_copy(e->vals[i]);
        }
    }

    if (e->par) {
        return lenv_get(e->par, k);
    } else {
        return lval_err("Unbound symbol '%s'", k->sym);
    }
}

lenv *lenv_copy(lenv *e)
{
    lenv *n = malloc(sizeof(lenv));
    n->par = e->par;
    n->count = e->count;
    n->syms = malloc(sizeof(char *) * n->count);
    n->vals = malloc(sizeof(lval *) * n->count);

    for (size_t i = 0; i < e->count; i++) {
        n->syms[i] = malloc(strlen(e->syms[i]) + 1);
        strcpy(n->syms[i], e->syms[i]);
        n->vals[i] = lval_copy(e->vals[i]);
    }

    return n;
}

void lenv_put(lenv *e, lval *k, lval *v)
{
    for (size_t i = 0; i < e->count; i++) {
        if (strcmp(e->syms[i], k->sym) == 0) {
            lval_delete(e->vals[i]);
            e->vals[i] = lval_copy(v);
            return;
        }
    }

    /* if no existing entry found allocate space for new entry */
    e->count++;
    e->vals = realloc(e->vals, sizeof(lval *) * e->count);
    e->syms = realloc(e->syms, sizeof(char *) * e->count);

    e->vals[e->count - 1] = lval_copy(v);
    e->syms[e->count - 1] = malloc(strlen(k->sym) + 1);
    strcpy(e->syms[e->count - 1], k->sym);
}

void lenv_def(lenv *e, lval *k, lval *v)
{
    while (e->par) {
        e = e->par;
    } /* ???? */

    lenv_put(e, k, v);
}

void lenv_add_builtin(lenv *e, char *name, lbuiltin func)
{
    lval *k = lval_sym(name);
    lval *v = lval_builtin(func);
    lenv_put(e, k, v);
    lval_delete(k);
    lval_delete(v);
}

void lenv_add_builtins(lenv *e)
{
    lenv_add_builtin(e, "list", builtin_list);
    lenv_add_builtin(e, "head", builtin_head);
    lenv_add_builtin(e, "tail", builtin_tail);
    lenv_add_builtin(e, "eval", builtin_eval);
    lenv_add_builtin(e, "join", builtin_join);
    lenv_add_builtin(e, "def", builtin_def);
    lenv_add_builtin(e, "load", builtin_load);
    lenv_add_builtin(e, "error", builtin_error);
    lenv_add_builtin(e, "print", builtin_print);

    lenv_add_builtin(e, "+", builtin_add);
    lenv_add_builtin(e, "-", builtin_sub);
    lenv_add_builtin(e, "*", builtin_mul);
    lenv_add_builtin(e, "/", builtin_div);
    lenv_add_builtin(e, "=", builtin_put);

    lenv_add_builtin(e, "if", builtin_if);
    lenv_add_builtin(e, "==", builtin_eq);
    lenv_add_builtin(e, "!=", builtin_ne);
    lenv_add_builtin(e, ">", builtin_gt);
    lenv_add_builtin(e, "<", builtin_lt);
    lenv_add_builtin(e, ">=", builtin_ge);
    lenv_add_builtin(e, "<=", builtin_le);

    lenv_add_builtin(e, "\\", builtin_lambda);
}
