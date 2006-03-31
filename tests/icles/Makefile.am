if USE_X
X_TESTS = ximagesrc_test

ximagesrc_test_SOURCES = ximagesrc-test.c
ximagesrc_test_CFLAGS = $(GST_CFLAGS)
ximagesrc_test_LDFLAGS = $(GST_LIBS)
else
X_TESTS =
endif

noinst_PROGRAMS = $(X_TESTS)

