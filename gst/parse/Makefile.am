
noinst_LTLIBRARIES = libgstparse.la


EXTRA_DIST = grammar.y parse.l types.h

BUILT_SOURCES = grammar.tab.h grammar.tab.c lex._gst_parse_yy.c 

libgstparse_la_SOURCES = lex._gst_parse_yy.c grammar.tab.c
libgstparse_la_CFLAGS = $(GST_CFLAGS) $(DISABLE_FLEX_WARNING)
libgstparse_la_LIBADD = $(LIBGST_LIBS)

noinst_HEADERS = grammar.tab.h

grammar.tab.c grammar.tab.h: grammar.y ../gstmarshal.h
	$(BISON_PATH) -d -v -p_gst_parse__yy $(srcdir)/grammar.y -o grammar.tab.c
 
lex._gst_parse_yy.c: parse.l
	$(FLEX_PATH) -P_gst_parse_yy $^

## this is so make install without a previous make can work, because
## this dir needs gstmarshal.h generated
../gstmarshal.h:
	cd .. && $(MAKE) gstmarshal.h

distclean-local:
	rm -f grammar.output grammar.tab.h

