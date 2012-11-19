plugin_LTLIBRARIES = libgstandroidcamera.la

libgstandroidcamera_la_SOURCES = \
	gst-androidcamera.c \
	gst-dvm.c \
	gst-android-hardware-camera.c \
	gst-android-graphics-surfacetexture.c \
	gst-android-graphics-imageformat.c \
	gstahcsrc.c \
	gstahccallback.c

JAVA_SOURCE = com/gstreamer/GstAhcCallback.java

noinst_HEADERS = \
	gst-dvm.h \
	gstahcsrc.h \
	gstahccallback.h \
	gst-android-hardware-camera.h \
	gst-android-graphics-surfacetexture.h \
	gst-android-graphics-imageformat.h

libgstandroidcamera_la_CFLAGS = \
	$(GST_PLUGINS_BASE_CFLAGS) \
	$(GST_BASE_CFLAGS) \
	$(GST_CFLAGS) \
	-I$(srcdir)
libgstandroidcamera_la_LIBADD = \
	$(GST_PLUGINS_BASE_LIBS) \
	$(GST_BASE_LIBS) \
	$(GST_LIBS)
libgstandroidcamera_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)
libgstandroidcamera_la_LIBTOOLFLAGS = --tag=disable-static

EXTRA_DIST = $(JAVA_SOURCE)
CLEANFILES = gstahccallback.c

if HAVE_ANDROID_SDK
gstahccallback.c: $(JAVA_SOURCE)
	@echo -e "  JAVAC\t $(JAVA_SOURCE)"
	@$(JAVAC) -target 1.6 -source 1.6 -nowarn \
	 -classpath $(ANDROID_SDK_DIR)/platforms/android-$(ANDROID_PLATFORM)/android.jar $(JAVA_SOURCE)
	@echo -e "  DEX\t $(JAVA_SOURCE:.java=.class)"
	@$(DX) --dex --output GstAhcCallback.jar $(JAVA_SOURCE:.java=.class)
	@echo -e "  GEN\t $@"
	@echo "#include \"gstahccallback.h\"" > $@
	@echo "" >> $@
	@echo "const static guint8 jar_file[] = {" >> $@
	@hexdump -v -e '"  "' -e '8/1 "0x%02x, "' -e '"\n"'  GstAhcCallback.jar | sed 's/0x  ,//g' >> $@
	@echo "};" >> $@
	@echo "" >> $@
	@echo "const guint8 *gst_ahc_callback_jar = jar_file;" >> $@
	@echo "const gsize gst_ahc_callback_jar_size = sizeof(jar_file);" >> $@
	@rm -f GstAhcCallback.jar
else
gstahccallback.c:
	@echo -e "  GEN\t $@"
	@echo "#include \"gstahccallback.h\"" > $@
	@echo "" >> $@
	@echo "const guint8 *gst_ahc_callback_jar = NULL;" >> $@
	@echo "const gsize gst_ahc_callback_jar_size = 0;" >> $@
endif

Android.mk: Makefile.am $(BUILT_SOURCES)
	androgenizer \
	-:PROJECT libgstandroidcamera -:SHARED libgstandroidcamera \
	 -:TAGS eng debug \
         -:REL_TOP $(top_srcdir) -:ABS_TOP $(abs_top_srcdir) \
	 -:SOURCES $(libgstandroidcamera_la_SOURCES) \
	           $(libgstandroidcamera_la_SOURCES) \
	 -:CFLAGS $(DEFS) $(DEFAULT_INCLUDES) $(libgstandroidcamera_la_CFLAGS) \
	 -:LDFLAGS $(libgstandroidcamera_la_LDFLAGS) \
	           $(libgstandroidcamera_la_LIBADD) \
	           -ldl \
	 -:PASSTHROUGH LOCAL_ARM_MODE:=arm \
		       LOCAL_MODULE_PATH:='$$(TARGET_OUT)/lib/gstreamer-@GST_MAJOR_MINOR@' \
	> $@
