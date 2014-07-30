CFLAGS 		?= -Wall -O2 -g
PKGCONFIG 	?= pkg-config
LIBS		:= $(shell $(PKGCONFIG) --libs --cflags gstreamer-1.0 gstreamer-video-1.0 gtk+-3.0) -lm

all: gtk-play

clean:
	rm -f gtk-play

SOURCES = \
	  gtk-play.c \
	  ../lib/gstplayer.c

HEADERS = \
	  ../lib/gstplayer.h

gtk-play: $(SOURCES) $(HEADERS)
	$(CC) -o $@ $(SOURCES) $(CFLAGS) $(LIBS) -I../lib

