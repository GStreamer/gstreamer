#noinst_LTLIBRARIES = libgstparse.la

#libgstparse_la_SOURCES = parse.c grammar.c

BISON = bison -d

noinst_PROGRAMS = grammar

grammar_SOURCES = lex.yy.c grammar.tab.c
grammar_CFLAGS = $(GLIB_CFLAGS)

noinst_HEADERS = grammar.tab.h

BUILT_SOURCES = grammar.tab.h grammar.tab.c lex.yy.c 

grammar.tab.h: grammar.y
	$(BISON) grammar.y

grammar.tab.c: grammar.y
	$(BISON) grammar.y

lex.yy.c: parse.l
	flex parse.l
