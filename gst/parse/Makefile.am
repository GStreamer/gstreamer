noinst_LTLIBRARIES = libgstparse.la

libgstparse_la_SOURCES = lex._gst_parse_yy.c grammar.tab.c

BISON = bison -d -v -p_gst_parse__yy

libgstparse_la_CFLAGS = $(LIBGST_CFLAGS)
libgstparse_la_LIBADD = $(LIBGST_LIBS)

noinst_HEADERS = grammar.tab.h

BUILT_SOURCES = grammar.tab.h grammar.tab.c lex._gst_parse_yy.c 

EXTRA_DIST = grammar.y parse.l types.h

grammar.tab.c grammar.tab.h: grammar.y ../gstmarshal.h
	$(BISON) $(srcdir)/grammar.y
 
## this is so make install without a previous make can work, because
## this dir needs gstmarshal.h generated
../gstmarshal.h:
	cd .. && make gstmarshal.h

lex._gst_parse_yy.c: parse.l
	flex -P_gst_parse_yy $^
