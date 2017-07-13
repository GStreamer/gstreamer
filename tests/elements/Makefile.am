noinst_PROGRAMS = \
	test-vaapisink  \
	test-vaapipostproc  \
	test-roi  \
	$(NULL)

TEST_CFLAGS = \
	-DGST_USE_UNSTABLE_API		\
	$(GST_CFLAGS)			\
	$(GST_VIDEO_CFLAGS)		\
	$(NULL)

TEST_LIBS = \
	$(GST_LIBS) 			\
	$(GST_VIDEO_LIBS)		\
	$(NULL)

test_vaapisink_SOURCES	= test-vaapisink.c
test_vaapisink_CFLAGS	= $(TEST_CFLAGS)
test_vaapisink_LDADD	= $(TEST_LIBS)

test_vaapipostproc_SOURCES	= test-vaapipostproc.c
test_vaapipostproc_CFLAGS	= $(TEST_CFLAGS)
test_vaapipostproc_LDADD	= $(TEST_LIBS)

test_roi_SOURCES = test-roi.c
test_roi_CFLAGS  = $(TEST_CFLAGS)
test_roi_LDADD   = $(TEST_LIBS)

-include $(top_srcdir)/git.mk
