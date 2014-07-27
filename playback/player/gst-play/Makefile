CFLAGS 		?= -Wall -O2 -g
PKGCONFIG 	?= pkg-config
LIBS		:= $(shell $(PKGCONFIG) --libs --cflags gstreamer-1.0 gstreamer-video-1.0) -lm

all: gst-play

clean:
	rm -f gst-play

SOURCES = \
	  gst-play.c \
	  gst-play-kb.c \
	  ../lib/gstplayer.c

HEADERS = \
	  gst-play-kb.h \
	  ../lib/gstplayer.h

gst-play: $(SOURCES) $(HEADERS)
	$(CC) -o $@ $(SOURCES) $(CFLAGS) $(LIBS) -I../lib

