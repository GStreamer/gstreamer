SUBDIRS = sched eos nego muxing

noinst_PROGRAMS = init loadall simplefake states caps queue registry \
paranoia rip mp3encode autoplug props case4 markup load tee autoplug2 autoplug3 \
capsconnect padfactory autoplug4 incsched reaping threadlock mp1vid reconnect \
faketest events timecache

# we have nothing but apps here, we can do this safely
LIBS += $(GST_LIBS)
CFLAGS += $(GST_CFLAGS)

EXTRA_DIST = README
