noinst_PROGRAMS = \
	test-vaapisink  \
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

-include $(top_srcdir)/git.mk
