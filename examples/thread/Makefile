CC = libtool gcc

thread: thread.c 
	$(CC) -Wall -I../../ ../../gst/libgst.la `gnome-config --cflags --libs gnomeui` thread.c -o thread 

clean: 
	rm -f *.o thread
	
	
