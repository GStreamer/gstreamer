testprogs = cleanup1

TESTS = $(testprogs)

check_PROGRAMS = $(testprogs)

# we have nothing but apps here, we can do this safely
cleanup1_LDADD = $(GST_LIBS) 
cleanup1_CFLAGS = $(GST_CFLAGS) $(XML_CFLAGS) $(GLIB_CFLAGS)
