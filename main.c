#include "mpc.h"
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
    Number = mpc_new("number");
    Symbol = mpc_new("symbol");
    Comment = mpc_new("comment");
    Sexpr = mpc_new("sexpr");
    Qexpr = mpc_new("qexpr");
    String = mpc_new("string");
    Expr = mpc_new("expr");
    Lispy = mpc_new("lispy");
    lenv *e;

    mpca_lang(MPCA_LANG_DEFAULT, 
            "                                                               \
                number: /-?[0-9]+/;                                         \
                symbol: /[a-zA-Z0-9_+\\-*\\/\\\\=<>!&]+/;                   \
                string: /\"(\\\\.|[^\"])*\"/;                               \
                comment: /;[^\\r\\n]*/;                                     \
                sexpr: '(' <expr>* ')';                                     \
                qexpr: '{' <expr>* '}';                                     \
                expr: <number> | <symbol> | <sexpr>                         \
                    | <qexpr>  | <string> | <comment>;                      \
                lispy: /^/ <expr>* /$/;                                     \
            ",
              Number, Symbol, String, Comment, Sexpr, Qexpr, Expr, Lispy);

    (void)argc;
    (void)argv;

    e = lenv_new();
    lenv_add_builtins(e);

    if (argc == 1) {
        puts("Lispy version 0.0.1");
        puts("CTRL+C to exit\n");

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
    }

    if (argc >= 2) {
        for (int i = 1; i < argc; i++) {
            lval *args = lval_add(lval_sexpr(), lval_str(argv[i]));
            lval *x = builtin_load(e, args);
            if (x->type == LVAL_ERR) {
                lval_println(x);
            }
            lval_delete(x);
        }
    }

    lenv_delete(e);

    mpc_cleanup(8, Number, Symbol, Sexpr, Qexpr,
                String, Comment, Expr, Lispy);

    return 0;
}
