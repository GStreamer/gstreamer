plugin_LTLIBRARIES = libgstgoom.la

PPC_FILES=ppc_zoom_ultimate.s ppc_drawings.s ppc_drawings.h ppc_zoom_ultimate.h
MMX_FILES=mmx.c xmmx.c mmx.h xmmx.h

if HAVE_CPU_PPC
ARCH_FILES = $(PPC_FILES)
endif
if HAVE_CPU_PPC64
ARCH_FILES = $(PPC_FILES)
endif
if HAVE_CPU_I386
ARCH_FILES = $(MMX_FILES)
ARCH_CFLAGS = -DBUILD_MMX
endif

libgstgoom_la_SOURCES =						\
	gstgoom.c gstgoom.h					\
	drawmethods.c drawmethods.h				\
	sound_tester.c sound_tester.h				\
	mathtools.c mathtools.h					\
	goomsl_heap.c goomsl_heap.h				\
	goomsl_hash.c goomsl_hash.h				\
	goomsl.c goomsl_private.h				\
	lines.c lines.h ifs.c ifs.h surf3d.c surf3d.h		\
	tentacle3d.c tentacle3d.h v3d.c v3d.h			\
	gfontrle.c gfontrle.h gfontlib.c gfontlib.h		\
	convolve_fx.c flying_stars_fx.c				\
	goom_fx.h goom_visual_fx.h				\
	motif_goom1.h motif_goom2.h				\
	plugin_info.c goom_plugin_info.h			\
	default_scripts.h goom_tools.c				\
	config_param.c filters.c goom_core.c graphic.c		\
	goom.h goom_typedefs.h goom_graphic.h			\
	goom_config_param.h goom_visual_fx.h goom_filters.h	\
	goom_tools.h goom_tools.h goom_config.h			\
	$(ARCH_FILES)

libgstgoom_la_CFLAGS = $(GST_PLUGINS_BASE_CFLAGS) $(GST_BASE_CFLAGS) $(GST_CFLAGS) $(GOOM_FILTER_CFLAGS) $(LIBOIL_CFLAGS) $(ARCH_CFLAGS)
libgstgoom_la_LIBADD = $(GST_BASE_LIBS) $(GST_LIBS) $(LIBM) $(LIBOIL_LIBS) libgstgoomconfigparse.la
libgstgoom_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)

EXTRA_DIST = filters.c $(PPC_FILES) $(MMX_FILES)

goomsl_yacc.c goomsl_yacc.h: goomsl_yacc.y
	$(BISON_PATH) -d $(srcdir)/goomsl_yacc.y -o goomsl_yacc.c && \
	mv goomsl_yacc.c goomsl_yacc_tmp.c && \
	echo '#ifdef HAVE_CONFIG_H' > goomsl_yacc_tmp2.c && \
	echo '#include <config.h>' >> goomsl_yacc_tmp2.c && \
	echo '#endif' >> goomsl_yacc_tmp2.c && \
	cat goomsl_yacc_tmp.c >> goomsl_yacc_tmp2.c && \
	rm goomsl_yacc_tmp.c && \
	mv goomsl_yacc_tmp2.c goomsl_yacc.c

goomsl_lex.c: goomsl_lex.l goomsl_yacc.h
	$(FLEX_PATH) -ogoomsl_lex.c $(srcdir)/goomsl_lex.l && \
	mv goomsl_lex.c goomsl_lex_tmp.c && \
	echo '#ifdef HAVE_CONFIG_H' > goomsl_lex_tmp2.c && \
	echo '#include <config.h>' >> goomsl_lex_tmp2.c && \
	echo '#endif' >> goomsl_lex_tmp2.c && \
	cat goomsl_lex_tmp.c >> goomsl_lex_tmp2.c && \
	rm goomsl_lex_tmp.c && \
	mv goomsl_lex_tmp2.c goomsl_lex.c

# libgstgoomconfigparse.la is library linked into the goom plugin
noinst_LTLIBRARIES = libgstgoomconfigparse.la

# uncomment these lines to dist the generated sources
#BUILT_SOURCES = goomsl_yacc.h goomsl_yacc.c goomsl_lex.c
#libgstgoomconfigparse_la_SOURCES = goomsl_lex.c goomsl_yacc.c goomsl_yacc.h
#libgstgoomconfigparse_la_CFLAGS = $(GST_PLUGINS_BASE_CFLAGS) $(GST_BASE_CFLAGS) $(GST_CFLAGS) $(GOOM_FILTER_CFLAGS) $(LIBOIL_CFLAGS) $(ARCH_CFLAGS)

# uncomment these lines to _NOT_ dist the generated sources
nodist_libgstgoomconfigparse_la_SOURCES = goomsl_lex.c goomsl_yacc.c
libgstgoomconfigparse_la_CFLAGS = $(GST_PLUGINS_BASE_CFLAGS) $(GST_BASE_CFLAGS) $(GOOM_FILTER_CFLAGS) $(LIBOIL_CFLAGS) $(ARCH_CFLAGS)
CLEANFILES=goomsl_yacc.c goomsl_lex.c goomsl_yacc.h

EXTRA_DIST += goomsl_yacc.y goomsl_lex.l

.NOTPARALLEL:

