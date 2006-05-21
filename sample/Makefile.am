TARGETS = playbin-player.exe decodebin-transcoder.exe
DEBUGS = $(addsuffix .mdb, $(TARGETS))

assemblies=$(top_builddir)/gstreamer-sharp/gstreamer-sharp.dll
references=$(addprefix /r:, $(assemblies))

playbin-player.exe: $(srcdir)/PlayBinPlayer.cs $(assemblies)
	$(CSC) $(GLIBSHARP_LIBS) $(references) $(srcdir)/PlayBinPlayer.cs \
		/out:playbin-player.exe

decodebin-transcoder.exe: $(srcdir)/DecodeBinTranscoder.cs $(assemblies)
	$(CSC) -out:$@ $(GLIBSHARP_LIBS) $(references) $(srcdir)/DecodeBinTranscoder.cs 

noinst_SCRIPTS = $(TARGETS)

CLEANFILES = $(TARGETS) $(DEBUGS)
MAINTAINERCLEANFILES = Makefile.in

EXTRA_DIST = \
	PlayBinPlayer.cs \
	DecodeBinTranscoder.cs
