CC = libtool gcc

queue2: queue2.c 
	$(CC) -Wall -I../../ ../../gst/libgst.la `gnome-config --cflags --libs gnomeui` queue2.c -o queue2 

clean: 
	rm -f *.o queue2
	
	
