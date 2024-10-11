Fuzzing GStreamer
=================

  This directory contains the various fuzzing targets and helper
  scripts.

* Fuzzing targets

  Fuzzing targets as small applications where we can test a specific
  element or API. The goal is to have them be as small/targeted as
  possible.

    ex: appsrc ! <some_element> ! fakesink num-buffers=<small>
    
  Not all components can be tested directly and therefore will be
  indirectly tested via other targets (ex: libgstaudio will be tested
  by targets/elements requiring it)

  Anything that can process externally-provided data should be
  covered, but there are cases where it might not make sense to use a
  fuzzer (such as most elements processing raw audio/video).

* build-oss-fuzz.sh

  This is the script executed by the oss-fuzz project.

  It builds glib, GStreamer, plugins and the fuzzing targets.

* *.c

  The fuzzing targets where the data to test will be provided to a
  function whose signature follows the LibFuzzer signature:
  https://llvm.org/docs/LibFuzzer.html

* *.corpus

  A file matching a test name that contains a list of files to use when
  starting a fuzzing run.  Providing an initial set files can speed up
  the fuzzing process significantly.

* TODO

  * Add a standalone build script

    We need to be able to build and test the fuzzing targets outside
    of the oss-fuzz infrastructure, and do that in our continuous
    integration system.

    We need:

    * A dummy fuzzing engine (given a directory, it opens all files and
      calls the fuzzing targets with the content of those files.
    * A script to be able to build those targets with that dummy engine
    * A corpus of files to test those targets with.

  * Build targets with dummy engine and run with existing tests.

  * Create pull-based variants

    Currently the existing targets are push-based only. Where
    applicable we should make pull-based variants to test the other
    code paths.

  * Add more targets

    core:
      gst_parse fuzzer ?
    base:
      ext/
        ogg
	opus
	pango
	theora
	vorbis
      gst/
        subparse
	typefind : already covered in typefind target
      gst-libs/gst/
        sdp
	other ones easily testable directly ?

        	
	 
      
      

