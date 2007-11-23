plugin_LTLIBRARIES = libgstmetadata.la

libgstmetadata_la_SOURCES =	gstmetadata.c       \
				gstmetadataparse.c  \
				metadataparse.c     \
				metadataparsejpeg.c \
				metadataparsepng.c  \
				metadataparseexif.c \
				metadataparseiptc.c \
				metadataparsexmp.c  \
				metadataparseutil.c \
				metadatatypes.c     \
				gstmetadatamux.c

libgstmetadata_la_CFLAGS = $(METADATA_CFLAGS) $(GST_PLUGINS_BASE_CFLAGS) $(GST_BASE_CFLAGS)
libgstmetadata_la_LIBADD = $(METADATA_LIBS) -lgsttag-@GST_MAJORMINOR@ $(GST_PLUGINS_BASE_LIBS) $(GST_BASE_LIBS)
libgstmetadata_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)

noinst_HEADERS = 	gstmetadataparse.h   \
			metadataparse.h      \
			metadataparsejpeg.h  \
			metadataparsepng.h   \
			metadataparseexif.h  \
			metadataparseiptc.h  \
			metadataparsexmp.h   \
			metadataparseutil.h  \
			metadatatypes.h      \
			gstmetadatamux.h

