filterdir = $(libdir)/gst

filter_LTLIBRARIES = libwinloader.la

# compiler manages to screw something up here... must reduce optimizations
CFLAGS += -O0 -funroll-loops

libwinloader_la_SOURCES = win32.c driver.c elfdll.c ext.c module.c \
pe_image.c pe_resource.c registry.c resource.c stubs.s vfl.c afl.c

noinst_HEADERS = wineacm.h

DEFINES=-rdynamic -fPIC -D__WINE__ -Ddbg_printf=__vprintf -DTRACE=__vprintf 
CFLAGS += $(DEFINES)
