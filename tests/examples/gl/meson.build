# FIXME: subdir('cocoa')
subdir('generic')
# FIXME: gtk is temporarily disabled. 
#        See. https://gitlab.freedesktop.org/gstreamer/gst-plugins-base/issues/518
if host_system != 'darwin'
subdir('gtk', if_found : gtk_dep)
endif
# FIXME: subdir('qt')
subdir('sdl')
