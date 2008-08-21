# plugindir is set in configure

##############################################################################
# change libgstplugin.la to something more suitable, e.g. libmysomething.la  #
##############################################################################
plugin_LTLIBRARIES = libgstdccp.la

##############################################################################
# for the next set of variables, rename the prefix if you renamed the .la,   #
#  e.g. libgstplugin_la_SOURCES => libmysomething_la_SOURCES                 #
#       libgstplugin_la_CFLAGS  => libmysomething_la_CFLAGS                  #
#       libgstplugin_la_LIBADD  => libmysomething_la_LIBADD                  #
#       libgstplugin_la_LDFLAGS => libmysomething_la_LDFLAGS                 #
##############################################################################

# sources used to compile this plug-in
libgstdccp_la_SOURCES = gstdccpplugin.c \
			gstdccp.c \
			gstdccpserversink.c \
			gstdccpserversrc.c \
			gstdccpclientsink.c \
			gstdccpclientsrc.c

# flags used to compile this plugin
# add other _CFLAGS and _LIBS as needed
libgstdccp_la_CFLAGS = $(GST_CFLAGS)
libgstdccp_la_LIBADD = $(GST_BASE_LIBS) $(GST_LIBS)
libgstdccp_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)

# headers we need but don't want installed
noinst_HEADERS = gstdccp.h \
                 gstdccpserversink.h \
                 gstdccpserversrc.h \
                 gstdccpclientsink.h \
                 gstdccpclientsrc.h

