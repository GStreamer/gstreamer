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
	dynamic $(GNOME) elementmake elementget helloworld \
	init popt queue threads $(GST_LOADSAVE_SRC)

dynamic.c: $(top_srcdir)/docs/manual/dynamic.xml
	$(PERL_PATH) $(srcdir)/extract.pl $@ $(top_srcdir)/docs/manual/dynamic.xml

elementmake.c elementget.c: $(top_srcdir)/docs/manual/elements-api.xml
	$(PERL_PATH) $(srcdir)/extract.pl $@ $(top_srcdir)/docs/manual/elements-api.xml

gnome.c: $(top_srcdir)/docs/manual/gnome.xml
	$(PERL_PATH) $(srcdir)/extract.pl $@ $(top_srcdir)/docs/manual/gnome.xml

helloworld.c: $(top_srcdir)/docs/manual/helloworld.xml
	$(PERL_PATH) $(srcdir)/extract.pl $@ $(top_srcdir)/docs/manual/helloworld.xml

init.c popt.c: $(top_srcdir)/docs/manual/init-api.xml
	$(PERL_PATH) $(srcdir)/extract.pl $@ $(top_srcdir)/docs/manual/init-api.xml

queue.c: $(top_srcdir)/docs/manual/queues.xml
	$(PERL_PATH) $(srcdir)/extract.pl $@ $(top_srcdir)/docs/manual/queues.xml

threads.c: $(top_srcdir)/docs/manual/threads.xml
	$(PERL_PATH) $(srcdir)/extract.pl $@ $(top_srcdir)/docs/manual/threads.xml

xml-mp3.c: $(top_srcdir)/docs/manual/xml.xml
	$(PERL_PATH) $(srcdir)/extract.pl $@ $(top_srcdir)/docs/manual/xml.xml 

# we use some of the examples as testsuite apps, to verify that
# they actually run
include $(top_srcdir)/testsuite/Rules
                                                                                
tests_pass = elementmake elementget init popt
tests_fail =
tests_ignore =

noinst_PROGRAMS = $(EXAMPLES)
LDADD = $(GST_OBJ_LIBS)
