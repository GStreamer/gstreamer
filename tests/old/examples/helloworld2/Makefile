CC = libtool gcc

helloworld2: helloworld2.c 
	$(CC) -Wall -I../../ ../../gst/libgst.la `gnome-config --cflags --libs gnomeui` helloworld2.c -o helloworld2 

clean: 
	rm -f *.o helloworld2
	
	
