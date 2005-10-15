# libgstparse.la is an optionally built helper library linked into core
noinst_LTLIBRARIES = libgstparse.la

CLEANFILES = grammar.tab.h grammar.output
EXTRA_DIST = grammar.y parse.l types.h

# uncomment these lines to dist the generated sources
#BUILT_SOURCES = grammar.tab.h grammar.tab.c lex._gst_parse_yy.c 
#libgstparse_la_SOURCES = lex._gst_parse_yy.c grammar.tab.c

# uncomment these lines to _NOT_ dist the generated sources
nodist_libgstparse_la_SOURCES = lex._gst_parse_yy.c grammar.tab.c
CLEANFILES += grammar.tab.c lex._gst_parse_yy.c 

libgstparse_la_CFLAGS = $(GST_ALL_CFLAGS)
libgstparse_la_LIBADD = $(GST_ALL_LIBS)

noinst_HEADERS = grammar.tab.h

grammar.tab.c grammar.tab.h: grammar.y 
	$(BISON_PATH) -d -v -p_gst_parse__yy $(srcdir)/grammar.y -o grammar.tab.c
 
lex._gst_parse_yy.c: parse.l grammar.tab.h
	$(FLEX_PATH) -P_gst_parse_yy $^
