include ../Rules

tests_pass = cleanup1 cleanup2 cleanup4 cleanup5
tests_fail =

# cleanup3 fails depending on the machine
tests_ignore = cleanup3

# we have nothing but apps here, we can do this safely
cleanup1_LDADD = $(GST_OBJ_LIBS)
cleanup1_CFLAGS = $(GST_OBJ_CFLAGS) $(XML_CFLAGS) $(GLIB_CFLAGS)
cleanup2_LDADD = $(GST_OBJ_LIBS) 
cleanup2_CFLAGS = $(GST_OBJ_CFLAGS) $(XML_CFLAGS) $(GLIB_CFLAGS)
#cleanup3_LDADD = $(GST_OBJ_LIBS) 
#cleanup3_CFLAGS = $(GST_OBJ_CFLAGS) $(XML_CFLAGS) $(GLIB_CFLAGS)
cleanup4_LDADD = $(GST_OBJ_LIBS) 
cleanup4_CFLAGS = $(GST_OBJ_CFLAGS) $(XML_CFLAGS) $(GLIB_CFLAGS)
cleanup5_LDADD = $(GST_OBJ_LIBS) 
cleanup5_CFLAGS = $(GST_OBJ_CFLAGS) $(XML_CFLAGS) $(GLIB_CFLAGS)
