SUBDIRS = sched eos nego muxing

if GST_DISABLE_LOADSAVE
GST_LOADSAVE_PROG =
else
GST_LOADSAVE_PROG = caps registry autoplug props tee autoplug2 capsconnect \
		    padfactory autoplug4
endif

noinst_PROGRAMS = $(GST_LOADSAVE_PROG) init loadall simplefake states queue \
paranoia rip mp3encode case4 markup load autoplug3 \
incsched reaping threadlock mp1vid reconnect \
faketest events timecache

# we have nothing but apps here, we can do this safely
LIBS += $(GST_LIBS)
CFLAGS += $(GST_CFLAGS)

EXTRA_DIST = README
