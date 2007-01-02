if GST_DISABLE_LOADSAVE
GST_LOADSAVE_DIRS = 
else
GST_LOADSAVE_DIRS = xml typefind
endif

if GST_DISABLE_PARSE
GST_PARSE_DIRS = 
else
GST_PARSE_DIRS = launch
endif

# adapter test needs sys/times.h and unistd.h
if HAVE_SYS_TIMES_H_AND_UNISTD_H
  ADAPTER_TEST_DIR = adapter
else
  ADAPTER_TEST_DIR =
endif

always_dirs = \
	controller \
	helloworld \
	manual     \
	metadata   \
	queue

#appreader
#cutter
#events
#helloworld2
#launch
#manual
#mixer
#pingpong
#plugins
#pwg
#queue2
#queue3
#queue4
#retag
#thread

SUBDIRS = 				\
	$(always_dirs)			\
	$(ADAPTER_TEST_DIR)		\
	$(GST_PARSE_DIRS)		\
	$(GST_LOADSAVE_DIRS)

DIST_SUBDIRS = $(always_dirs) adapter xml typefind launch
