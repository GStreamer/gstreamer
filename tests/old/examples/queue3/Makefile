CC = libtool gcc

queue3: queue3.c 
	$(CC) -Wall -I../../ ../../gst/libgst.la `gnome-config --cflags --libs gnomeui` queue3.c -o queue3 

clean: 
	rm -f *.o queue3
	
	
