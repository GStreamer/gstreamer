CC = libtool gcc

helloworld: helloworld.c 
	$(CC) -Wall -I../../ ../../gst/libgst.la `gnome-config --cflags --libs gnomeui` helloworld.c -o helloworld 

clean: 
	rm -f *.o helloworld
	
	
