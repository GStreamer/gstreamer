CC = libtool gcc

xml: xml.c 
	$(CC) -Wall -I../../ ../../gst/libgst.la `gnome-config --cflags --libs gnomeui` xml.c -o xml 

clean: 
	rm -f *.o xml
	
	
