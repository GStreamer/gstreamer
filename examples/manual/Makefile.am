if HAVE_LIBGNOMEUI
GNOME=gnome
else
GNOME=
endif

EXAMPLES = $(GNOME) xml
noinst_PROGRAMS = $(EXAMPLES)

LDADD = $(GST_LIBS)
INCLUDES = $(GST_CFLAGS)

gnome_LDADD = $(GST_LIBS) $(LIBGNOMEUI_LIBS)
gnome_CFLAGS = $(GST_CFLAGS) $(LIBGNOMEUI_CFLAGS)

# for some reason specifying %.c runs us into trouble when running make
# clean, it starts looking for things like mostlyclean-am.c, please
# help me fix that so we don't need to specify all sources here
# also, it's a bit irritating that right now a change in any xml file
# triggers a rebuild of all examples
#%.c:
gnome.c xml.c: $(top_srcdir)/docs/manual/*.xml
	$(srcdir)/extract.pl $@ $(top_srcdir)/docs/manual/*.xml
