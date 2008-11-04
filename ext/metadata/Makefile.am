plugin_LTLIBRARIES = libgstmetadata.la

libgstmetadata_la_SOURCES =	gstmetadata.c       \
				gstmetadatademux.c  \
				metadata.c          \
				metadataparsejpeg.c \
				metadatamuxjpeg.c   \
				metadataparsepng.c  \
				metadatamuxpng.c    \
				metadataexif.c      \
				metadataiptc.c      \
				metadataxmp.c       \
				metadataparseutil.c \
				metadatatypes.c     \
				gstmetadatamux.c    \
				metadatatags.c      \
				gstbasemetadata.c

libgstmetadata_la_CFLAGS = $(METADATA_CFLAGS) $(GST_PLUGINS_BASE_CFLAGS) $(GST_BASE_CFLAGS) $(GST_CFLAGS)
libgstmetadata_la_LIBADD = $(METADATA_LIBS) -lgsttag-@GST_MAJORMINOR@ $(GST_PLUGINS_BASE_LIBS) $(GST_BASE_LIBS) $(GST_LIBS)
libgstmetadata_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)
libgstmetadata_la_LIBTOOLFLAGS = --tag=disable-static

noinst_HEADERS = 	gstmetadatademux.h   \
			metadata.h           \
			metadataparsejpeg.h  \
			metadatamuxjpeg.h    \
			metadataparsepng.h   \
			metadatamuxpng.h     \
			metadataexif.h       \
			metadataiptc.h       \
			metadataxmp.h        \
			metadataparseutil.h  \
			metadatatags.h	     \
			metadatatypes.h      \
			gstmetadatamux.h     \
			gstbasemetadata.h

EXTRA_DIST = metadata_mapping.htm
