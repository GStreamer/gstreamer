# libgstparse.la is an optionally built helper library linked into core
noinst_LTLIBRARIES = libgstparse.la

CLEANFILES = grammar.tab.h parse_lex.h grammar.output
EXTRA_DIST = \
	grammar.y \
	parse.l

# uncomment these lines to dist the generated sources
#BUILT_SOURCES = grammar.tab.h grammar.tab.c parse_lex.h lex.priv_gst_parse_yy.c
#libgstparse_la_SOURCES = lex.priv_gst_parse_yy.c grammar.tab.c

# uncomment these lines to _NOT_ dist the generated sources
nodist_libgstparse_la_SOURCES = lex.priv_gst_parse_yy.c grammar.tab.c grammar.tag.h parse_lex.h
CLEANFILES += grammar.tab.c lex.priv_gst_parse_yy.c

libgstparse_la_CFLAGS = $(GST_ALL_CFLAGS) -DGST_EXPORTS -DYYMALLOC=g_malloc -DYYFREE=g_free
libgstparse_la_LIBADD = $(GST_ALL_LIBS)

noinst_HEADERS = types.h

grammar.tab.c grammar.tab.h: grammar.y
	$(AM_V_GEN)$(BISON_PATH) -d -v -ppriv_gst_parse_yy $(srcdir)/grammar.y -o grammar.tab.c && \
	mv grammar.tab.c grammar.tab_tmp.c && \
	echo '#ifdef HAVE_CONFIG_H' > grammar.tab_tmp2.c && \
	echo '#include <config.h>' >> grammar.tab_tmp2.c && \
	echo '#endif' >> grammar.tab_tmp2.c && \
	cat grammar.tab_tmp.c >> grammar.tab_tmp2.c && \
	rm grammar.tab_tmp.c && \
	mv grammar.tab_tmp2.c grammar.tab.c

lex.priv_gst_parse_yy.c parse_lex.h: parse.l grammar.tab.h
	$(AM_V_GEN)$(FLEX_PATH) --header-file=parse_lex.h -Ppriv_gst_parse_yy $^ && \
	mv lex.priv_gst_parse_yy.c lex.priv_gst_parse_yy_tmp.c && \
	echo '#ifdef HAVE_CONFIG_H' > lex.priv_gst_parse_yy_tmp2.c && \
	echo '#include <config.h>' >> lex.priv_gst_parse_yy_tmp2.c && \
	echo '#endif' >> lex.priv_gst_parse_yy_tmp2.c && \
	{ grep -q priv_gst_parse_yyget_column parse_lex.h || \
	{ echo 'int priv_gst_parse_yyget_column  (void * yyscanner);' >> lex.priv_gst_parse_yy_tmp2.c && \
	echo 'void priv_gst_parse_yyset_column (int column_no , void * yyscanner);' >> lex.priv_gst_parse_yy_tmp2.c; }; } && \
	cat lex.priv_gst_parse_yy_tmp.c >> lex.priv_gst_parse_yy_tmp2.c && \
	rm lex.priv_gst_parse_yy_tmp.c && \
	mv lex.priv_gst_parse_yy_tmp2.c lex.priv_gst_parse_yy.c

.NOTPARALLEL:

