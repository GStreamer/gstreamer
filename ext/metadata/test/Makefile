
CC=gcc
CFLAGS=-c -Wall -g3 -O0 `pkg-config --cflags gstreamer-0.10 libglade-2.0 gtk+-2.0`
CLIBS=-lgstinterfaces-0.10 -Wl -export-dynamic `pkg-config --libs gstreamer-0.10 libglade-2.0 gtk+-2.0`

all: metadata_editor

metadata_editor: metadata_editor.o
	$(CC) $(CLIBS) metadata_editor.o -o metadata_editor

metadata_editor.o: metadata_editor.c
	$(CC) $(CFLAGS) metadata_editor.c

clean:
	rm -rf *.o metadata_editor

