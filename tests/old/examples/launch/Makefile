CC = libtool gcc

launch: launch.c 
	$(CC) -Wall -I../../ ../../gst/libgst.la `gnome-config --cflags --libs gnomeui` launch.c -o launch 

clean: 
	rm -f *.o launch
	
	
