TARGETS = gst-gapi-fixup.exe gst-generate-tags.exe gst-gapi-parser.exe
DEBUGS = $(addsuffix .mdb, $(TARGETS))

all: $(TARGETS)

gst-gapi-fixup.exe: $(srcdir)/gst-gapi-fixup.cs
	$(CSC) -out:$@ $(srcdir)/gst-gapi-fixup.cs

gst-generate-tags.exe: $(srcdir)/gst-generate-tags.cs
	$(CSC) -out:$@ $(srcdir)/gst-generate-tags.cs

gst-gapi-parser.exe: $(srcdir)/gapi-parser.cs
	$(CSC) -out:$@ $(srcdir)/gapi-parser.cs


noinst_SCRIPTS = $(TARGETS) gapi_pp.pl gapi2xml.pl

CLEANFILES = $(TARGETS) $(DEBUGS)
MAINTAINERCLEANFILES = Makefile.in

EXTRA_DIST = \
	gst-gapi-fixup.cs \
	gst-generate-tags.cs \
	gapi-parser.cs \
	gapi_pp.pl \
	gapi2xml.pl

