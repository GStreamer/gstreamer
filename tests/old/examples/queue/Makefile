CC = libtool gcc

queue: queue.c 
	$(CC) -Wall -I../../ ../../gst/libgst.la `gnome-config --cflags --libs gnomeui` queue.c -o queue 

clean: 
	rm -f *.o queue
	
	
