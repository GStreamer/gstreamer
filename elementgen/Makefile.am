TARGETS = $(noinst_SCRIPTS)
DEBUGS = $(addsuffix .mdb, $(TARGETS))

element-gen.exe: $(srcdir)/elementgen.cs
	$(CSC) -out:$@ -debug $(srcdir)/elementgen.cs

gsts_xmlinspect_SOURCES = gst-xmlinspect.c
gsts_xmlinspect_LDFLAGS = $(GST_LIBS)
gsts_xmlinspect_CFLAGS = $(GST_CFLAGS)

noinst_SCRIPTS = element-gen.exe
noinst_PROGRAMS = gsts-xmlinspect

CLEANFILES = $(TARGETS) $(DEBUGS)
MAINTAINERCLEANFILES = Makefile.in

EXTRA_DIST = \
	$(srcdir)/elementgen.cs \
	$(srcdir)/interfaces/*.cs

