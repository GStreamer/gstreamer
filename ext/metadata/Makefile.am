plugin_LTLIBRARIES = libgstmetadata.la

libgstmetadata_la_SOURCES =	gstmetadata.c       \
				gstmetadataparse.c  \
				metadata.c          \
				metadataparsejpeg.c \
				metadataparsepng.c  \
				metadataexif.c      \
				metadataiptc.c      \
				metadataxmp.c       \
				metadataparseutil.c \
				metadatatypes.c     \
				gstmetadatamux.c    \
				metadatatags.c

libgstmetadata_la_CFLAGS = $(METADATA_CFLAGS) $(GST_PLUGINS_BASE_CFLAGS) $(GST_BASE_CFLAGS)
libgstmetadata_la_LIBADD = $(METADATA_LIBS) -lgsttag-@GST_MAJORMINOR@ $(GST_PLUGINS_BASE_LIBS) $(GST_BASE_LIBS)
libgstmetadata_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)

noinst_HEADERS = 	gstmetadataparse.h   \
			metadata.h           \
			metadataparsejpeg.h  \
			metadataparsepng.h   \
			metadataexif.h       \
			metadataiptc.h       \
			metadataxmp.h        \
			metadataparseutil.h  \
			metadatatypes.h      \
			gstmetadatamux.h     \
			metadatatags.h

