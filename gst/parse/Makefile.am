noinst_LTLIBRARIES = libgstparse.la

libgstparse_la_SOURCES = lex.yy.c grammar.tab.c

BISON = bison -d -v

libgstparse_la_CFLAGS = $(LIBGST_CFLAGS)
libgstparse_la_LIBADD = $(LIBGST_LIBS)

noinst_HEADERS = grammar.tab.h

BUILT_SOURCES = grammar.tab.h grammar.tab.c lex.yy.c 

grammar.tab.h: grammar.y
	$(BISON) grammar.y

grammar.tab.c: grammar.y
	$(BISON) grammar.y

lex.yy.c: parse.l
	flex parse.l
