CC = libtool gcc

autoplug: autoplug.c 
	$(CC) -Wall -I../../ ../../gst/libgst.la `gnome-config --cflags --libs gnomeui` autoplug.c -o autoplug 

clean: 
	rm -f *.o autoplug
	
	
