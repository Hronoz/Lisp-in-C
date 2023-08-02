COMP_FLAGS=-Wall -Wextra -g -std=c99 # -Weverything -pedantic 

all:
	clang $(COMP_FLAGS) -o a.out main.c lisp.c mpc.c -ledit -lm -Iinclude
