
plugin_LTLIBRARIES = \
	libthreadscheduler.la
AS_LIBTOOL_LIBS = \
	libthreadscheduler

libthreadscheduler_la_SOURCES = threadscheduler.c 
libthreadscheduler_la_CFLAGS = $(GST_OBJ_CFLAGS) 
libthreadscheduler_la_LIBADD = $(GST_OBJ_LIBS)
libthreadscheduler_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)

noinst_HEADERS = 

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
