
CC = libtool gcc

autoplug: autoplug.c 
	$(CC) -Wall `gstreamer-config --cflags --libs` `gnome-config --cflags --libs gnomeui` autoplug.c -o autoplug 

clean: 
	rm -f *.o autoplug
	
	
