#noinst_LTLIBRARIES = libgstparse.la

#libgstparse_la_SOURCES = parse.c grammar.c

noinst_PROGRAMS = grammar

grammar_SOURCES = lex.yy.c grammar.tab.c
grammar_CFLAGS = $(GLIB_CFLAGS) -DYYERROR_VERBOSE

noinst_HEADERS = grammar.tab.h

BUILT_SOURCES = grammar.tab.h grammar.tab.c lex.yy.c 

grammar.tab.h: grammar.y
	bison -v -d grammar.y

grammar.tab.c: grammar.y
	bison -v -d grammar.y

lex.yy.c: parse.l
	flex parse.l
