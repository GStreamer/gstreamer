dnl default elements used for tests and such

dnl AG_GST_DEFAULT_ELEMENTS

AC_DEFUN([AG_GST_DEFAULT_ELEMENTS],
[
  dnl decide on default elements
  dnl FIXME: provide configure-time options for this
  dnl FIXME: describe where exactly this gets used
  dnl FIXME: decide if it's a problem that this could point to sinks from
  dnl        depending plugin modules
  DEFAULT_AUDIOSINK="autoaudiosink"
  DEFAULT_VIDEOSINK="autovideosink"
  DEFAULT_AUDIOSRC="alsasrc"
  DEFAULT_VIDEOSRC="v4lsrc"
  DEFAULT_VISUALIZER="goom"
  case "$host" in
    *-sun-* | *pc-solaris* )
      DEFAULT_AUDIOSINK="sunaudiosink"
      DEFAULT_VIDEOSINK="ximagesink"
      DEFAULT_AUDIOSRC="sunaudiosrc"
      ;;
    *-darwin* )
      DEFAULT_AUDIOSINK="osxaudiosink"
      DEFAULT_AUDIOSRC="osxaudiosrc"
      DEFAULT_VIDEOSINK="osxvideosink"
      ;;
  esac
  
  AC_SUBST(DEFAULT_AUDIOSINK)
  AC_DEFINE_UNQUOTED(DEFAULT_AUDIOSINK, "$DEFAULT_AUDIOSINK",
    [Default audio sink])
  AC_SUBST(DEFAULT_AUDIOSRC)
  AC_DEFINE_UNQUOTED(DEFAULT_AUDIOSRC, "$DEFAULT_AUDIOSRC",
    [Default audio source])
  AC_SUBST(DEFAULT_VIDEOSINK)
  AC_DEFINE_UNQUOTED(DEFAULT_VIDEOSINK, "$DEFAULT_VIDEOSINK",
    [Default video sink])
  AC_SUBST(DEFAULT_VIDEOSRC)
  AC_DEFINE_UNQUOTED(DEFAULT_VIDEOSRC, "$DEFAULT_VIDEOSRC",
    [Default video source])
  AC_SUBST(DEFAULT_VISUALIZER)
  AC_DEFINE_UNQUOTED(DEFAULT_VISUALIZER, "$DEFAULT_VISUALIZER",
    [Default visualizer])
])
