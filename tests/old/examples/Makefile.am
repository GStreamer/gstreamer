if GST_DISABLE_LOADSAVE
GST_LOADSAVE_DIRS = 
else
GST_LOADSAVE_DIRS = xml typefind
endif

dirs = \
	helloworld			\
	queue				\
	launch				\
	thread				\
	plugins				\
	mixer				\
	metadata                        \
	controller			\
	cutter				\
	pingpong			\
	manual				\
	pwg				\
	retag

#queue2				\
#queue3				\
#queue4				

SUBDIRS = $(dirs)			\
	$(GST_LOADSAVE_DIRS)

DIST_SUBDIRS = $(dirs) xml typefind
