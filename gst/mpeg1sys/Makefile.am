filterdir = $(libdir)/gst

filter_LTLIBRARIES = libgstmpeg1systemencode.la

libgstmpeg1systemencode_la_SOURCES = gstmpeg1systemencode.c \
			 buffer.c \
			 systems.c  
libgstmpeg1systemencode_la_CFLAGS = -O2 $(FOMIT_FRAME_POINTER) -funroll-all-loops -finline-functions -ffast-math $(GST_CFLAGS)

noinst_HEADERS = gstmpeg1systemencode.h \
		 main.h \
		 buffer.h

