INCLUDES = $(GST_OBJ_CFLAGS) \
	-DVERSION="\"0.0.1\""

libboilerplate_la_SOURCES = boilerplate.c
libpads_la_SOURCES = pads.c
libchain_la_SOURCES = chain.c
libchain2_la_SOURCES = chain2.c
libstate_la_SOURCES = state.c
libproperties_la_SOURCES = properties.c

EXTRA_DIST = extract.pl

EXAMPLES = \
	libboilerplate.la \
	libpads.la \
	libchain.la \
	libchain2.la \
	libstate.la \
	libproperties.la

EXAMPLE_APPS = \
	test

filter.h register.func: $(top_srcdir)/docs/pwg/building-boiler.xml
	$(PERL_PATH) $(srcdir)/extract.pl $@ \
		$(top_srcdir)/docs/pwg/building-boiler.xml

boilerplate.c: $(top_srcdir)/docs/pwg/building-boiler.xml filter.h register.func
	$(PERL_PATH) $(srcdir)/extract.pl $@ \
		$(top_srcdir)/docs/pwg/building-boiler.xml

caps.func init.func: $(top_srcdir)/docs/pwg/building-pads.xml filter.h
	$(PERL_PATH) $(srcdir)/extract.pl $@ \
		$(top_srcdir)/docs/pwg/building-pads.xml

pads.c: $(top_srcdir)/docs/pwg/building-pads.xml register.func caps.func init.func
	$(PERL_PATH) $(srcdir)/extract.pl $@ \
		$(top_srcdir)/docs/pwg/building-pads.xml

chain.func: $(top_srcdir)/docs/pwg/building-chainfn.xml
	$(PERL_PATH) $(srcdir)/extract.pl $@ \
		$(top_srcdir)/docs/pwg/building-chainfn.xml

chain.c chain2.c: $(top_srcdir)/docs/pwg/building-chainfn.xml register.func caps.func init.func chain.func
	$(PERL_PATH) $(srcdir)/extract.pl $@ \
		$(top_srcdir)/docs/pwg/building-chainfn.xml

state.func: $(top_srcdir)/docs/pwg/building-state.xml
	$(PERL_PATH) $(srcdir)/extract.pl $@ \
		$(top_srcdir)/docs/pwg/building-state.xml

state.c: $(top_srcdir)/docs/pwg/building-state.xml register.func caps.func init.func chain.func state.func
	$(PERL_PATH) $(srcdir)/extract.pl $@ \
		$(top_srcdir)/docs/pwg/building-state.xml

properties.c: $(top_srcdir)/docs/pwg/building-props.xml filter.h register.func
	$(PERL_PATH) $(srcdir)/extract.pl $@ \
		$(top_srcdir)/docs/pwg/building-props.xml

test.c: $(top_srcdir)/docs/pwg/building-testapp.xml
	$(PERL_PATH) $(srcdir)/extract.pl $@ \
		$(top_srcdir)/docs/pwg/building-testapp.xml

noinst_PROGRAMS = $(EXAMPLE_APPS)
noinst_LTLIBRARIES = $(EXAMPLES)
LDADD = $(GST_OBJ_LIBS)
