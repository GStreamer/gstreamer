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

AM_TESTS_ENVIRONMENT = \
        $(REGISTRY_ENVIRONMENT)                                 \
        GST_PLUGIN_SCANNER_1_0=$(top_builddir)/libs/gst/helpers/gst-plugin-scanner \
        GST_PLUGIN_SYSTEM_PATH_1_0=                                 \
        GST_PLUGIN_PATH_1_0=$(top_builddir)/plugins

EXTRA_DIST = extract.pl

EXAMPLES = \
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
	blockprobe \
	probe \
	appsrc \
	appsink \
	dynformat \
	effectswitch \
	norebuffer \
	playbin \
	decodebin \
	playsink 

BUILT_SOURCES = \
	elementmake.c elementcreate.c elementget.c elementlink.c elementfactory.c \
	bin.c \
	pad.c ghostpad.c \
	gnome.c \
	helloworld.c \
	init.c \
	query.c \
	typefind.c \
	blockprobe.c \
	probe.c \
	appsrc.c \
	appsink.c \
	dynformat.c \
	effectswitch.c \
	norebuffer.c \
	playbin.c decodebin.c \
	playsink.c

if HAVE_PTHREAD
BUILT_SOURCES += testrtpool.c
EXAMPLES += testrtpool
endif

CLEANFILES = core core.* test-registry.* *.gcno *.gcda $(BUILT_SOURCES)

AM_CFLAGS = $(GST_OBJ_CFLAGS)
LDADD = $(top_builddir)/libs/gst/base/libgstbase-@GST_API_VERSION@.la \
	$(GST_OBJ_LIBS)

elementmake.c elementcreate.c elementget.c elementlink.c elementfactory.c: $(top_srcdir)/docs/manual/basics-elements.xml
	$(PERL_PATH) $(srcdir)/extract.pl $@ $<

bin.c : $(top_srcdir)/docs/manual/basics-bins.xml
	$(PERL_PATH) $(srcdir)/extract.pl $@ $<

pad.c ghostpad.c: $(top_srcdir)/docs/manual/basics-pads.xml
	$(PERL_PATH) $(srcdir)/extract.pl $@ $<

gnome.c: $(top_srcdir)/docs/manual/appendix-integration.xml
	$(PERL_PATH) $(srcdir)/extract.pl $@ $<

helloworld.c: $(top_srcdir)/docs/manual/basics-helloworld.xml
	$(PERL_PATH) $(srcdir)/extract.pl $@ $<

init.c: $(top_srcdir)/docs/manual/basics-init.xml
	$(PERL_PATH) $(srcdir)/extract.pl $@ $<

query.c: $(top_srcdir)/docs/manual/advanced-position.xml
	$(PERL_PATH) $(srcdir)/extract.pl $@ $<

typefind.c: $(top_srcdir)/docs/manual/advanced-autoplugging.xml
	$(PERL_PATH) $(srcdir)/extract.pl $@ $<

blockprobe.c: $(top_srcdir)/docs/manual/advanced-dataaccess.xml
	$(PERL_PATH) $(srcdir)/extract.pl $@ $<

probe.c: $(top_srcdir)/docs/manual/advanced-dataaccess.xml
	$(PERL_PATH) $(srcdir)/extract.pl $@ $<

appsrc.c: $(top_srcdir)/docs/manual/advanced-dataaccess.xml
	$(PERL_PATH) $(srcdir)/extract.pl $@ $<

appsink.c: $(top_srcdir)/docs/manual/advanced-dataaccess.xml
	$(PERL_PATH) $(srcdir)/extract.pl $@ $<

dynformat.c: $(top_srcdir)/docs/manual/advanced-dataaccess.xml
	$(PERL_PATH) $(srcdir)/extract.pl $@ $<

effectswitch.c: $(top_srcdir)/docs/manual/advanced-dataaccess.xml
	$(PERL_PATH) $(srcdir)/extract.pl $@ $<

norebuffer.c: $(top_srcdir)/docs/manual/advanced-buffering.xml
	$(PERL_PATH) $(srcdir)/extract.pl $@ $<

playbin.c decodebin.c playsink.c: $(top_srcdir)/docs/manual/highlevel-playback.xml
	$(PERL_PATH) $(srcdir)/extract.pl $@ $<

testrtpool.c: $(top_srcdir)/docs/manual/advanced-threads.xml
	$(PERL_PATH) $(srcdir)/extract.pl $@ $<

TESTS = bin \
	elementcreate elementfactory elementget elementlink elementmake \
	ghostpad init

noinst_PROGRAMS = $(EXAMPLES)

testrtpool_LDADD = $(GST_OBJ_LIBS) $(PTHREAD_LIBS)
testrtpool_CFLAGS = $(GST_OBJ_CFLAGS) $(PTHREAD_CFLAGS)
