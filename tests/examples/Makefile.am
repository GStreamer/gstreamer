if GST_DISABLE_LOADSAVE
GST_LOADSAVE_DIRS = 
else
GST_LOADSAVE_DIRS = xml typefind
endif

dirs = \
	controller			\
	helloworld			\
	launch				\
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

SUBDIRS = $(dirs)			\
	$(GST_LOADSAVE_DIRS)

DIST_SUBDIRS = $(dirs) xml typefind
