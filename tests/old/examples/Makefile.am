if GST_DISABLE_LOADSAVE
GST_LOADSAVE_DIRS = 
else
GST_LOADSAVE_DIRS = xml typefind
endif

dirs = \
	helloworld			\
	queue				\
	queue2				\
	queue3				\
	queue4				\
	launch				\
	thread				\
	plugins				\
	mixer				\
	cutter				\
	pingpong			\
	manual				\
	retag

SUBDIRS = $(dirs)			\
	$(GST_LOADSAVE_DIRS)

DIST_SUBDIRS = $(dirs) xml typefind
