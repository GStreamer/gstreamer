testprogs = thread

TESTS = $(testprogs)

check_PROGRAMS = $(testprogs)

# we have nothing but apps here, we can do this safely
thread_LDADD = $(GST_LIBS) 
thread_CFLAGS = $(GST_CFLAGS) $(GNOME_CFLAGS) $(XML_CFLAGS)
