# libgstparse.la is an optionally built helper library linked into core
noinst_LTLIBRARIES = libgstparse.la

CLEANFILES = grammar.tab.h grammar.output
EXTRA_DIST = \
	grammar.y \
	parse.l \
	grammar.tab.pre.c \
	grammar.tab.pre.h \
	lex._gst_parse_yy.pre.c

# uncomment these lines to dist the generated sources
#BUILT_SOURCES = grammar.tab.h grammar.tab.c lex._gst_parse_yy.c
#libgstparse_la_SOURCES = lex._gst_parse_yy.c grammar.tab.c

# uncomment these lines to _NOT_ dist the generated sources
nodist_libgstparse_la_SOURCES = lex._gst_parse_yy.c grammar.tab.c
CLEANFILES += grammar.tab.c lex._gst_parse_yy.c

libgstparse_la_CFLAGS = $(GST_ALL_CFLAGS)
libgstparse_la_LIBADD = $(GST_ALL_LIBS)

noinst_HEADERS = grammar.tab.h types.h

all: grammar.tab.pre.c grammar.tab.pre.h lex._gst_parse_yy.pre.c

grammar.tab.pre.c grammar.tab.pre.h: grammar.y
if GENERATE_PARSER
	$(BISON_PATH) -d -v -p_gst_parse_yy $(srcdir)/grammar.y -o grammar.tab.c && \
	mv grammar.tab.c grammar.tab_tmp.c && \
	echo '#ifdef HAVE_CONFIG_H' > grammar.tab_tmp2.c && \
	echo '#include <config.h>' >> grammar.tab_tmp2.c && \
	echo '#endif' >> grammar.tab_tmp2.c && \
	cat grammar.tab_tmp.c >> grammar.tab_tmp2.c && \
	rm grammar.tab_tmp.c && \
	mv grammar.tab_tmp2.c grammar.tab.c
	mv grammar.tab.c grammar.tab.pre.c
	mv grammar.tab.h grammar.tab.pre.h
else
	echo "Timestamp issue on pregenerated bison output."
	echo "Using probably outdated pregenerated sources..."
	touch grammar.tab.pre.c grammar.tab.pre.h
endif

lex._gst_parse_yy.pre.c: parse.l grammar.tab.pre.h
if GENERATE_PARSER
	$(FLEX_PATH) -P_gst_parse_yy $^ && \
	mv lex._gst_parse_yy.c lex._gst_parse_yy_tmp.c && \
	echo '#ifdef HAVE_CONFIG_H' > lex._gst_parse_yy_tmp2.c && \
	echo '#include <config.h>' >> lex._gst_parse_yy_tmp2.c && \
	echo '#endif' >> lex._gst_parse_yy_tmp2.c && \
	cat lex._gst_parse_yy_tmp.c >> lex._gst_parse_yy_tmp2.c && \
	rm lex._gst_parse_yy_tmp.c && \
	mv lex._gst_parse_yy_tmp2.c lex._gst_parse_yy.c && \
	mv lex._gst_parse_yy.c lex._gst_parse_yy.pre.c
else
	echo "Timestamp issue on pregenerated flex output."
	echo "Using probably outdated pregenerated sources..."
	touch lex._gst_parse_yy.pre.c
endif

if GENERATE_PARSER
grammar.tab.c grammar.tab.h: grammar.y
	$(BISON_PATH) -d -v -p_gst_parse_yy $(srcdir)/grammar.y -o grammar.tab.c && \
	mv grammar.tab.c grammar.tab_tmp.c && \
	echo '#ifdef HAVE_CONFIG_H' > grammar.tab_tmp2.c && \
	echo '#include <config.h>' >> grammar.tab_tmp2.c && \
	echo '#endif' >> grammar.tab_tmp2.c && \
	cat grammar.tab_tmp.c >> grammar.tab_tmp2.c && \
	rm grammar.tab_tmp.c && \
	mv grammar.tab_tmp2.c grammar.tab.c
else
grammar.tab.c grammar.tab.h: grammar.tab.pre.c grammar.tab.pre.h
	cp $(srcdir)/grammar.tab.pre.h grammar.tab.h
	cp $(srcdir)/grammar.tab.pre.c grammar.tab.c
endif

if GENERATE_PARSER
lex._gst_parse_yy.c: parse.l grammar.tab.h
	$(FLEX_PATH) -P_gst_parse_yy $^ && \
	mv lex._gst_parse_yy.c lex._gst_parse_yy_tmp.c && \
	echo '#ifdef HAVE_CONFIG_H' > lex._gst_parse_yy_tmp2.c && \
	echo '#include <config.h>' >> lex._gst_parse_yy_tmp2.c && \
	echo '#endif' >> lex._gst_parse_yy_tmp2.c && \
	cat lex._gst_parse_yy_tmp.c >> lex._gst_parse_yy_tmp2.c && \
	rm lex._gst_parse_yy_tmp.c && \
	mv lex._gst_parse_yy_tmp2.c lex._gst_parse_yy.c
else
lex._gst_parse_yy.c: lex._gst_parse_yy.pre.c grammar.tab.h grammar.tab.c
	cp $(srcdir)/lex._gst_parse_yy.pre.c lex._gst_parse_yy.c
endif


.NOTPARALLEL:

