
CC = libtool gcc

all: runxml

runxml: runxml.c 
	$(CC) -Wall `gstreamer-config --cflags --libs` runxml.c -o runxml 

clean: 
	rm -f *.o runxml
	
	
