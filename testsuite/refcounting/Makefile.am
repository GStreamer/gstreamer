include ../Rules

tests_pass = bin element mainloop pad element_pad
tests_fail =
tests_ignore = sched

element_SOURCES = element.c mem.c
pad_SOURCES = pad.c mem.c
element_pad_SOURCES = element_pad.c mem.c
bin_SOURCES = bin.c mem.c

EXTRA_DIST = thread.c object.c

noinst_HEADERS = mem.h
