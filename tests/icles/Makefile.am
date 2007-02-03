if USE_SOUNDTOUCH

GST_SOUNDTOUCH_TESTS = pitch-test

pitch_test_SOURCES = pitch-test.c
pitch_test_CFLAGS  = $(GST_CONTROLLER_CFLAGS)
pitch_test_LDADD   = $(GST_CONTROLLER_LIBS)
pitch_test_LDFLAGS = $(GST_PLUGIN_LDFLAGS)

else
GST_SOUNDTOUCH_TESTS = 
endif

videocrop_test_SOURCES = videocrop-test.c
videocrop_test_CFLAGS  = $(GST_CFLAGS)
videocrop_test_LDADD   = $(GST_LIBS)
videocrop_test_LDFLAGS = $(GST_PLUGIN_LDFLAGS)

equalizer_test_SOURCES = equalizer-test.c
equalizer_test_CFLAGS  = $(GST_CFLAGS)
equalizer_test_LDADD   = $(GST_LIBS)
equalizer_test_LDFLAGS = $(GST_PLUGIN_LDFLAGS)

noinst_PROGRAMS = $(GST_SOUNDTOUCH_TESTS) equalizer-test videocrop-test
