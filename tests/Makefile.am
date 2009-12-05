MCS_FLAGS = -debug $(MONO_NUNIT_LIBS)

ASSEMBLY_NAME = gstreamer-tests
ASSEMBLY = $(ASSEMBLY_NAME).dll
ASSEMBLY_CSFILES = $(srcdir)/ApplicationTest.cs $(srcdir)/BinTest.cs $(srcdir)/BufferTest.cs $(srcdir)/CapsTest.cs $(srcdir)/PadTest.cs $(srcdir)/ElementTest.cs $(srcdir)/MessageTest.cs $(srcdir)/PipelineTest.cs $(srcdir)/BaseTransformTest.cs

$(ASSEMBLY): $(ASSEMBLY_CSFILES)
	$(CSC) $(MCS_FLAGS) -out:$@ -target:library -r:$(top_builddir)/gstreamer-sharp/gstreamer-sharp.dll $(ASSEMBLY_CSFILES) $(GLIBSHARP_LIBS)

run-test: $(ASSEMBLY)
	LD_LIBRARY_PATH=$(top_builddir)/gstreamer-sharp/glue/.libs:$$LD_LIBRARY_PATH MONO_PATH=$(top_builddir)/gstreamer-sharp $(NUNIT_TESTER) -labels $(ASSEMBLY)

EXTRA_DIST = $(ASSEMBLY_CSFILES)
CLEANFILES = $(ASSEMBLY) *.mdb *.pdb TestResult.xml
MAINTAINERCLEANFILES = Makefile.in

clean-local:
	rm -rf %temp%
