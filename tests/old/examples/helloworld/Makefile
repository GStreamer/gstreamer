
CC = gcc

helloworld: helloworld.c 
	$(CC) -Wall `gstreamer-config --cflags --libs` helloworld.c -o helloworld 

clean: 
	rm -f *.o helloworld
	
	
