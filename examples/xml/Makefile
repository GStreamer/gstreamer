
CC = gcc

all: createxml runxml

createxml: createxml.c 
	$(CC) -Wall `gstreamer-config --cflags --libs` createxml.c -o createxml 

runxml: runxml.c 
	$(CC) -Wall `gstreamer-config --cflags --libs` runxml.c -o runxml 

clean: 
	rm -f *.o createxml runxml
	
	
