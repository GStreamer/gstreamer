# adapter test needs sys/times.h and unistd.h
if HAVE_SYS_TIMES_H_AND_UNISTD_H
  ADAPTER_TEST_DIR = adapter
else
  ADAPTER_TEST_DIR =
endif

always_dirs = \
	controller \
	helloworld \
	memory   \
	netclock \
	ptp \
	stepping \
	streamiddemux \
	streams

SUBDIRS = 				\
	$(always_dirs)			\
	$(ADAPTER_TEST_DIR)

DIST_SUBDIRS = $(always_dirs) adapter

include $(top_srcdir)/common/parallel-subdirs.mak
