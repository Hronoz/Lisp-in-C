#include <errno.h>
#include <math.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include "lisp.h"

#ifdef _WIN32

static char buffer[2048];

char *readline(const char *prompt)
{
    char *copy;
    fputs(prompt, stdout);
    fgets(buffer, 2048, stdin);
    copy = malloc(strlen(buffer + 1));
    strcpy(copy, buffer);
    copy[strlen(copy) + 1] = '\0';
    return copy;
}

void add_history(char *unused) {}

#else

#include <editline/readline.h>

#endif

int main(int argc, char **argv)
{
    mpc_parser_t *Number = mpc_new("number");
    mpc_parser_t *Symbol = mpc_new("symbol");
    mpc_parser_t *Sexpr = mpc_new("sexpr");
    mpc_parser_t *Qexpr = mpc_new("qexpr");
    mpc_parser_t *Expr = mpc_new("expr");
    mpc_parser_t *Lispy = mpc_new("lispy");
    lenv *e;

    mpca_lang(MPCA_LANG_DEFAULT, 
            "                                                       \
                number: /-?[0-9]+/;                                 \
                symbol: /[a-zA-Z0-9_+\\-*\\/\\\\=<>!&]+/;           \
                sexpr: '(' <expr>* ')';                             \
                qexpr: '{' <expr>* '}';                             \
                expr: <number> | <symbol> | <sexpr> | <qexpr>;      \
                lispy: /^/ <expr>* /$/;                             \
            ",
              Number, Symbol, Sexpr, Qexpr, Expr, Lispy);

    (void)argc;
    (void)argv;

    puts("Lispy version 0.0.1");
    puts("CTRL+C to exit\n");

    e = lenv_new();
    lenv_add_builtins(e);

    while (1) {
        mpc_result_t result;
        char *input = readline("lispy> ");

        add_history(input);

        if (mpc_parse("<stdin>", input, Lispy, &result)) {
            lval *x = lval_evaluate(e, lval_read(result.output));
            lval_println(x);
            lval_delete(x);
            mpc_ast_delete(result.output);
        } else {
            mpc_err_print(result.error);
            mpc_err_delete(result.error);
        }

        free(input);
    }

    mpc_cleanup(6, Number, Symbol, Sexpr, Qexpr, Expr, Lispy);

    return 0;
}
