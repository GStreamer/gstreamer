CC = libtool gcc

all: createxml runxml

createxml: createxml.c 
	$(CC) -Wall -I../../ ../../gst/libgst.la `gnome-config --cflags --libs gnomeui` createxml.c -o createxml 

runxml: runxml.c 
	$(CC) -Wall -I../../ ../../gst/libgst.la `gnome-config --cflags --libs gnomeui` runxml.c -o runxml 

clean: 
	rm -f *.o createxml runxml
	
	
