
CC = gcc

helloworld: helloworld.c 
	$(CC) -Wall `gstreamer-config --cflags` `gtk-config --cflags` helloworld.c -o helloworld `gstreamer-config --libs` `gtk-config --libs`

clean: 
	rm -f *.o helloworld
	
	
