testprogs = cleanup1 cleanup2 cleanup3

TESTS = $(testprogs)

check_PROGRAMS = $(testprogs)

# we have nothing but apps here, we can do this safely
cleanup1_LDADD = $(GST_LIBS) #-ldmallocth
cleanup1_CFLAGS = $(GST_CFLAGS) $(XML_CFLAGS) $(GLIB_CFLAGS)
cleanup2_LDADD = $(GST_LIBS) 
cleanup2_CFLAGS = $(GST_CFLAGS) $(XML_CFLAGS) $(GLIB_CFLAGS)
cleanup3_LDADD = $(GST_LIBS) 
cleanup3_CFLAGS = $(GST_CFLAGS) $(XML_CFLAGS) $(GLIB_CFLAGS)
