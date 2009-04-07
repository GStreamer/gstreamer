MCS_FLAGS = -debug
NUNIT_FLAGS = $(MONO_NUNIT_LIBS)

ASSEMBLY_NAME = gstreamer-tests
ASSEMBLY = $(ASSEMBLY_NAME).dll
ASSEMBLY_CSFILES = $(srcdir)/ApplicationTest.cs $(srcdir)/BinTest.cs $(srcdir)/BufferTest.cs $(srcdir)/CapsTest.cs $(srcdir)/PadTest.cs $(srcdir)/ElementTest.cs $(srcdir)/MessageTest.cs $(srcdir)/PipelineTest.cs

NUNIT_TESTER_NAME = ConsoleUi
NUNIT_TESTER = $(NUNIT_TESTER_NAME).exe
NUNIT_TESTER_CSFILES = $(srcdir)/$(NUNIT_TESTER_NAME).cs

$(ASSEMBLY): $(ASSEMBLY_CSFILES)
	$(CSC) $(MCS_FLAGS) $(NUNIT_FLAGS) -out:$@ -target:library -r:$(top_builddir)/gstreamer-sharp/gstreamer-sharp.dll $(ASSEMBLY_CSFILES) $(GLIBSHARP_LIBS)

$(NUNIT_TESTER): $(NUNIT_TESTER_CSFILES)
	$(CSC) $(MCS_FLAGS) -out:$@ $(NUNIT_FLAGS) $(NUNIT_TESTER_CSFILES)

run-test: $(NUNIT_TESTER) $(ASSEMBLY)
	MONO_PATH="../gstreamer-sharp/" mono --debug $(NUNIT_TESTER) $(ASSEMBLY)

CLEANFILES = $(ASSEMBLY) $(NUNIT_TESTER) TestResult.xml
DISTCLEANFILES = *.mdb Makefile.in *.dll *.exe

