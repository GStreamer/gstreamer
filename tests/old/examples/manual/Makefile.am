if HAVE_LIBGNOMEUI
GNOME=gnome
else
GNOME=
endif

EXAMPLES = dynamic $(GNOME) helloworld helloworld2 queue threads xml-mp3
noinst_PROGRAMS = $(EXAMPLES)

LDADD = $(GST_LIBS)
INCLUDES = $(GST_CFLAGS)

#dynamic_LDADD = $(GST_LIBS) $(LIBGNOMEUI_LIBS)
#dynamic_CFLAGS = $(GST_CFLAGS) $(LIBGNOMEUI_CFLAGS)
gnome_LDADD = $(GST_LIBS) $(LIBGNOMEUI_LIBS)
gnome_CFLAGS = $(GST_CFLAGS) $(LIBGNOMEUI_CFLAGS)

EXTRA_DIST = extract.pl

# for some reason specifying %.c runs us into trouble when running make
# clean, it starts looking for things like mostlyclean-am.c, please
# help me fix that so we don't need to specify all sources here
# also, it's a bit irritating that right now a change in any xml file
# triggers a rebuild of all examples
#%.c:
dynamic.c gnome.c helloworld.c helloworld2.c queue.c threads.c xml-mp3.c: $(top_srcdir)/docs/manual/*.xml
	$(srcdir)/extract.pl $@ $(top_srcdir)/docs/manual/*.xml
