
if GST_DISABLE_OMEGA_COTHREADS
omegaschedulers =
omegaschedulers_nola =
else
omegaschedulers = \
	libgstbasicomegascheduler.la \
	libgstentryomegascheduler.la \
	libgstoptomegascheduler.la
omegaschedulers_nola = \
	libgstbasicomegascheduler \
	libgstentryomegascheduler \
	libgstoptomegascheduler
endif

plugin_LTLIBRARIES = \
	$(omegaschedulers) \
	libgstbasicgthreadscheduler.la \
	libgstentrygthreadscheduler.la \
	libgstoptscheduler.la \
	libgstoptgthreadscheduler.la
AS_LIBTOOL_LIBS = \
	$(omegaschedulers_nola) \
	libgstbasicgthreadscheduler \
	libgstentrygthreadscheduler \
	libgstoptscheduler \
	libgstoptgthreadscheduler

if GST_DISABLE_OMEGA_COTHREADS
else
libgstbasicomegascheduler_la_SOURCES = gstbasicscheduler.c 
libgstbasicomegascheduler_la_CFLAGS = $(GST_OBJ_CFLAGS) -D_COTHREADS_OMEGA
libgstbasicomegascheduler_la_LIBADD = $(GST_OBJ_LIBS) ../libcothreads.la
libgstbasicomegascheduler_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS) $(as_libtool_LDFLAGS)
endif

libgstbasicgthreadscheduler_la_SOURCES = gstbasicscheduler.c 
libgstbasicgthreadscheduler_la_CFLAGS = $(GST_OBJ_CFLAGS) -D_COTHREADS_GTHREAD
libgstbasicgthreadscheduler_la_LIBADD = $(GST_OBJ_LIBS)
libgstbasicgthreadscheduler_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS) $(as_libtool_LDFLAGS)

libgstentrygthreadscheduler_la_SOURCES = entryscheduler.c 
libgstentrygthreadscheduler_la_CFLAGS = $(GST_OBJ_CFLAGS) -D_COTHREADS_GTHREAD
libgstentrygthreadscheduler_la_LIBADD = $(GST_OBJ_LIBS)
libgstentrygthreadscheduler_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS) $(as_libtool_LDFLAGS)

if GST_DISABLE_OMEGA_COTHREADS
else
libgstentryomegascheduler_la_SOURCES = entryscheduler.c 
libgstentryomegascheduler_la_CFLAGS = $(GST_OBJ_CFLAGS) -D_COTHREADS_OMEGA
libgstentryomegascheduler_la_LIBADD = $(GST_OBJ_LIBS) ../libcothreads.la
libgstentryomegascheduler_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS) $(as_libtool_LDFLAGS)
endif

libgstoptscheduler_la_SOURCES = gstoptimalscheduler.c 
libgstoptscheduler_la_CFLAGS = $(GST_OBJ_CFLAGS) 
libgstoptscheduler_la_LIBADD = $(GST_OBJ_LIBS)
libgstoptscheduler_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS) $(as_libtool_LDFLAGS)

if GST_DISABLE_OMEGA_COTHREADS
else
libgstoptomegascheduler_la_SOURCES = gstoptimalscheduler.c 
libgstoptomegascheduler_la_CFLAGS = $(GST_OBJ_CFLAGS) -D_COTHREADS_OMEGA -DUSE_COTHREADS
libgstoptomegascheduler_la_LIBADD = $(GST_OBJ_LIBS) ../libcothreads.la
libgstoptomegascheduler_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS) $(as_libtool_LDFLAGS)
endif

libgstoptgthreadscheduler_la_SOURCES = gstoptimalscheduler.c 
libgstoptgthreadscheduler_la_CFLAGS = $(GST_OBJ_CFLAGS) -D_COTHREADS_GTHREAD -DUSE_COTHREADS
libgstoptgthreadscheduler_la_LIBADD = $(GST_OBJ_LIBS)
libgstoptgthreadscheduler_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS) $(as_libtool_LDFLAGS)

noinst_HEADERS = cothreads_compat.h gthread-cothreads.h

if AS_LIBTOOL_WIN32

as_libtool_noinst_DATA_files = $(AS_LIBTOOL_LIB).lib

as_libtool_LDFLAGS = -no-undefined

# depend on this in install-data-local
as-libtool-install-data-local:
	for i in $(AS_LIBTOOL_LIBS); do \
	  $(INSTALL) $$i.lib $(DESTDIR)$(libdir) ; \
	  $(INSTALL) $$i.def $(DESTDIR)$(libdir) ; \
	done

# depend on this in uninstall-local
as-libtool-uninstall-local:
	for i in $(AS_LIBTOOL_LIBS); do \
	  -rm $(DESTDIR)$(libdir)/$$i.lib ; \
	  -rm $(DESTDIR)$(libdir)/$$i.def ; \
	done
else

as_libtool_LDFLAGS =

as-libtool-install-data-local:
as-libtool-uninstall-local:

endif



%.lib: %.def
	dlltool -S $(CC) -f "-c" --export-all-symbols --input-def \
		%.def --output-lib %.lib

%.def: %.la
	echo EXPORTS >%.def.tmp
	nm --defined-only -g .libs/%.a | \
		grep ^0 | \
		awk '{ print $$3 }' | \
		sed 's/^/       /' >>%.def.tmp
	mv %.def.tmp %.def

