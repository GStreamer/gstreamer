if HAVE_LIBGNOMEUI
GNOME=gnome
else
GNOME=
endif

if GST_DISABLE_LOADSAVE
GST_LOADSAVE_SRC =
else
GST_LOADSAVE_SRC = xml-mp3
endif

INCLUDES = $(GST_OBJ_CFLAGS)

#dynamic_LDADD = $(GST_OBJ_LIBS) $(LIBGNOMEUI_LIBS)
#dynamic_CFLAGS = $(GST_OBJ_CFLAGS) $(LIBGNOMEUI_CFLAGS)
gnome_LDADD = $(GST_OBJ_LIBS) $(LIBGNOMEUI_LIBS)
gnome_CFLAGS = $(GST_OBJ_CFLAGS) $(LIBGNOMEUI_CFLAGS)

EXTRA_DIST = extract.pl

EXAMPLES = \
	dynamic \
	$(GNOME) \
	elementcreate \
	elementmake \
	elementfactory \
	elementget \
	elementlink \
	bin \
	pad \
	ghostpad \
	helloworld \
	init \
	popt \
	query \
	threads \
	typefind \
	playbin \
	decodebin \
	$(GST_LOADSAVE_SRC)

elementmake.c elementcreate.c elementget.c elementlink.c elementfactory.c: $(top_srcdir)/docs/manual/basics-elements.xml
	$(PERL_PATH) $(srcdir)/extract.pl $@ \
		$(top_srcdir)/docs/manual/basics-elements.xml

bin.c : $(top_srcdir)/docs/manual/basics-bins.xml
	$(PERL_PATH) $(srcdir)/extract.pl $@ \
		$(top_srcdir)/docs/manual/basics-bins.xml

pad.c ghostpad.c: $(top_srcdir)/docs/manual/basics-pads.xml
	$(PERL_PATH) $(srcdir)/extract.pl $@ \
		$(top_srcdir)/docs/manual/basics-pads.xml

gnome.c: $(top_srcdir)/docs/manual/appendix-gnome.xml
	$(PERL_PATH) $(srcdir)/extract.pl $@ \
		$(top_srcdir)/docs/manual/appendix-gnome.xml

helloworld.c: $(top_srcdir)/docs/manual/basics-helloworld.xml
	$(PERL_PATH) $(srcdir)/extract.pl $@ \
		$(top_srcdir)/docs/manual/basics-helloworld.xml

init.c popt.c: $(top_srcdir)/docs/manual/basics-init.xml
	$(PERL_PATH) $(srcdir)/extract.pl $@ \
		$(top_srcdir)/docs/manual/basics-init.xml

query.c: $(top_srcdir)/docs/manual/advanced-position.xml
	$(PERL_PATH) $(srcdir)/extract.pl $@ \
		$(top_srcdir)/docs/manual/advanced-position.xml

threads.c: $(top_srcdir)/docs/manual/advanced-threads.xml
	$(PERL_PATH) $(srcdir)/extract.pl $@ \
		$(top_srcdir)/docs/manual/advanced-threads.xml

typefind.c dynamic.c: $(top_srcdir)/docs/manual/advanced-autoplugging.xml
	$(PERL_PATH) $(srcdir)/extract.pl $@ \
		$(top_srcdir)/docs/manual/advanced-autoplugging.xml

playbin.c decodebin.c: $(top_srcdir)/docs/manual/highlevel-components.xml
	$(PERL_PATH) $(srcdir)/extract.pl $@ \
		$(top_srcdir)/docs/manual/highlevel-components.xml

xml-mp3.c: $(top_srcdir)/docs/manual/highlevel-xml.xml
	$(PERL_PATH) $(srcdir)/extract.pl $@ \
		$(top_srcdir)/docs/manual/highlevel-xml.xml 

# we use some of the examples as testsuite apps, to verify that
# they actually run
include $(top_srcdir)/testsuite/Rules
                                                                                
tests_pass = elementmake elementget init popt
tests_fail =
tests_ignore =

noinst_PROGRAMS = $(EXAMPLES)
LDADD = $(GST_OBJ_LIBS)
