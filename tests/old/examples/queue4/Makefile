CC = libtool gcc

queue4: queue4.c 
	$(CC) -Wall -I../../ ../../gst/libgst.la `gnome-config --cflags --libs gnomeui` queue4.c -o queue4 

clean: 
	rm -f *.o queue4
	
	
