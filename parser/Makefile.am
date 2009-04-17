TARGETS = gst-gapi-fixup.exe gst-generate-tags.exe
DEBUGS = $(addsuffix .mdb, $(TARGETS))

all: $(TARGETS)

gst-gapi-fixup.exe: $(srcdir)/gst-gapi-fixup.cs
	$(CSC) -out:$@ $(srcdir)/gst-gapi-fixup.cs

gst-generate-tags.exe: $(srcdir)/gst-generate-tags.cs
	$(CSC) -out:$@ $(srcdir)/gst-generate-tags.cs


noinst_SCRIPTS = $(TARGETS)

CLEANFILES = $(TARGETS) $(DEBUGS)
MAINTAINERCLEANFILES = Makefile.in

EXTRA_DIST = \
	gst-gapi-fixup.cs \
	gst-generate-tags.cs

