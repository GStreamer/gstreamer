# if HAVE_LIBGNOMEUI
# GNOME = gnome
# else
GNOME =
# endif

# gnome_LDADD = $(GST_OBJ_LIBS) $(LIBGNOMEUI_LIBS)
# gnome_CFLAGS = $(GST_OBJ_CFLAGS) $(LIBGNOMEUI_CFLAGS)

CHECK_REGISTRY = $(top_builddir)/tests/examples/manual/test-registry.reg

REGISTRY_ENVIRONMENT = \
        GST_REGISTRY=$(CHECK_REGISTRY)

TESTS_ENVIRONMENT = \
        $(REGISTRY_ENVIRONMENT)                                 \
        GST_PLUGIN_SCANNER=$(top_builddir)/libs/gst/helpers/gst-plugin-scanner \
        GST_PLUGIN_SYSTEM_PATH=                                 \
        GST_PLUGIN_PATH=$(top_builddir)/plugins

CLEANFILES = core core.* test-registry.* *.gcno *.gcda

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
	query \
	typefind \
	fakesrc \
	playbin \
	decodebin \
	$(GST_LOADSAVE_SRC)

AM_CFLAGS = $(GST_OBJ_CFLAGS)
LDADD = $(top_builddir)/libs/gst/base/libgstbase-@GST_MAJORMINOR@.la \
	$(GST_OBJ_LIBS)

elementmake.c elementcreate.c elementget.c elementlink.c elementfactory.c: $(top_srcdir)/docs/manual/basics-elements.xml
	$(PERL_PATH) $(srcdir)/extract.pl $@ \
		$(top_srcdir)/docs/manual/basics-elements.xml

bin.c : $(top_srcdir)/docs/manual/basics-bins.xml
	$(PERL_PATH) $(srcdir)/extract.pl $@ \
		$(top_srcdir)/docs/manual/basics-bins.xml

pad.c ghostpad.c: $(top_srcdir)/docs/manual/basics-pads.xml
	$(PERL_PATH) $(srcdir)/extract.pl $@ \
		$(top_srcdir)/docs/manual/basics-pads.xml

gnome.c: $(top_srcdir)/docs/manual/appendix-integration.xml
	$(PERL_PATH) $(srcdir)/extract.pl $@ \
		$(top_srcdir)/docs/manual/appendix-integration.xml

helloworld.c: $(top_srcdir)/docs/manual/basics-helloworld.xml
	$(PERL_PATH) $(srcdir)/extract.pl $@ \
		$(top_srcdir)/docs/manual/basics-helloworld.xml

init.c: $(top_srcdir)/docs/manual/basics-init.xml
	$(PERL_PATH) $(srcdir)/extract.pl $@ \
		$(top_srcdir)/docs/manual/basics-init.xml

query.c: $(top_srcdir)/docs/manual/advanced-position.xml
	$(PERL_PATH) $(srcdir)/extract.pl $@ \
		$(top_srcdir)/docs/manual/advanced-position.xml

typefind.c dynamic.c: $(top_srcdir)/docs/manual/advanced-autoplugging.xml
	$(PERL_PATH) $(srcdir)/extract.pl $@ \
		$(top_srcdir)/docs/manual/advanced-autoplugging.xml

fakesrc.c: $(top_srcdir)/docs/manual/advanced-dataaccess.xml
	$(PERL_PATH) $(srcdir)/extract.pl $@ \
		$(top_srcdir)/docs/manual/advanced-dataaccess.xml

playbin.c decodebin.c: $(top_srcdir)/docs/manual/highlevel-components.xml
	$(PERL_PATH) $(srcdir)/extract.pl $@ \
		$(top_srcdir)/docs/manual/highlevel-components.xml

xml-mp3.c: $(top_srcdir)/docs/manual/highlevel-xml.xml
	$(PERL_PATH) $(srcdir)/extract.pl $@ \
		$(top_srcdir)/docs/manual/highlevel-xml.xml 

TESTS = bin \
	elementcreate elementfactory elementget elementlink elementmake \
	ghostpad init

noinst_PROGRAMS = $(EXAMPLES)
