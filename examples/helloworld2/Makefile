
CC = gcc

helloworld2: helloworld2.c 
	$(CC) -Wall `gstreamer-config --cflags` `gtk-config --cflags` helloworld2.c -o helloworld2 `gstreamer-config --libs` `gtk-config --libs`

clean: 
	rm -f *.o helloworld2
	
	
