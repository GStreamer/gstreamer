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

always_dirs = \
	controller			\
	helloworld			\
	metadata                        \
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
	$(GST_PARSE_DIRS)		\
	$(GST_LOADSAVE_DIRS)

DIST_SUBDIRS = $(always_dirs) xml typefind launch
