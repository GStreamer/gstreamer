
noinst_LTLIBRARIES = libgstparse.la


CLEANFILES = grammar.tab.h grammar.output
EXTRA_DIST = grammar.y parse.l types.h

# uncomment these lines to dist the generated sources
#BUILT_SOURCES = grammar.tab.h grammar.tab.c lex._gst_parse_yy.c 
#libgstparse_la_SOURCES = lex._gst_parse_yy.c grammar.tab.c

# uncomment these lines to _NOT_ dist the generated sources
nodist_libgstparse_la_SOURCES = lex._gst_parse_yy.c grammar.tab.c
CLEANFILES += grammar.tab.c lex._gst_parse_yy.c 

libgstparse_la_CFLAGS = $(GST_CFLAGS) $(DISABLE_FLEX_WARNING)
libgstparse_la_LIBADD = $(LIBGST_LIBS)

noinst_HEADERS = grammar.tab.h

grammar.tab.c grammar.tab.h: grammar.y ../gstmarshal.h
	$(BISON_PATH) -d -v -p_gst_parse__yy $(srcdir)/grammar.y -o grammar.tab.c
 
lex._gst_parse_yy.c: parse.l grammar.tab.h
	$(FLEX_PATH) -P_gst_parse_yy parse.l 

## this is so make install without a previous make can work, because
## this dir needs gstmarshal.h generated
../gstmarshal.h:
	cd .. && $(MAKE) gstmarshal.h
