#!/bin/sh
# Run this to generate all the initial makefiles, etc.

DIE=0
package=gst-python
srcfile=gstreamer/gstreamermodule.c
                                                                                
# a quick cvs co if necessary to alleviate the pain - may remove this
# when developers get a clue ;)
if test ! -d common;
then
  echo "+ getting common/ from cvs"
  cvs co common
fi
                                                                                
# source helper functions
if test ! -e common/gst-autogen.sh;
then
  echo There is something wrong with your source tree.
  echo You are missing common/gst-autogen.sh
  exit 1
fi

. common/gst-autogen.sh
                                                                                
CONFIGURE_DEF_OPT='--enable-maintainer-mode'

autogen_options $@
                                                                                
echo -n "+ check for build tools"
if test ! -z $NOCHECK; then echo " skipped"; else  echo; fi
version_check "autoconf" "$AUTOCONF autoconf autoconf-2.53 autoconf-2.52" \
              "ftp://ftp.gnu.org/pub/gnu/autoconf/" 2 52 || DIE=1
version_check "automake" "$AUTOMAKE automake automake-1.6 automake-1.5" \
              "ftp://ftp.gnu.org/pub/gnu/automake/" 1 5 || DIE=1
version_check "libtool" "" \
              "ftp://ftp.gnu.org/pub/gnu/libtool/" 1 4 0 || DIE=1
version_check "pkg-config" "" \
              "http://www.freedesktop.org/software/pkgconfig" 0 8 0 || DIE=1
                                                                                
die_check $DIE
                                                                                
autoconf_2_52d_check || DIE=1

aclocal_check || DIE=1
autoheader_check || DIE=1
                                                                                
die_check $DIE
                                                                                
# if no arguments specified then this will be printed
if test -z "$*"; then
  echo "+ checking for autogen.sh options"
  echo "  This autogen script will automatically run ./configure as:"
  echo "  ./configure $CONFIGURE_DEF_OPT"
  echo "  To pass any additional options, please specify them on the $0"
  echo "  command line."
fi
                                                                                
toplevel_check $srcfile

if test -z "$ACLOCAL_FLAGS"; then
	acdir=`$aclocal --print-ac-dir`
        m4list="glib-2.0.m4 gtk-2.0.m4"

	for file in $m4list
	do
		if [ ! -f "$acdir/$file" ]; then
			echo "WARNING: aclocal's directory is $acdir, but..."
			echo "         no file $acdir/$file"
			echo "         You may see fatal macro warnings below."
			echo "         If these files are installed in /some/dir, set the ACLOCAL_FLAGS "
			echo "         environment variable to \"-I /some/dir\", or install"
			echo "         $acdir/$file."
			echo ""
		fi
	done
fi

tool_run "$aclocal" "-I common/m4 $ACLOCAL_FLAGS"
tool_run "libtoolize" "--copy --force"
tool_run "$autoheader"
                                                                                 # touch the stamp-h.in build stamp so we don't re-run autoheader in maintainer mode -- wingo
echo timestamp > stamp-h.in 2> /dev/null
                                                                                
tool_run "$autoconf"

case $CC in
*xlc | *xlc\ * | *lcc | *lcc\ *) am_opt=--include-deps;;
esac
tool_run "$automake" "-a -c $am_opt"

test -n "$NOCONFIGURE" && {
  echo "+ skipping configure stage for package $package, as requested."
  echo "+ autogen.sh done."
  exit 0
}

echo "+ running configure ... "
test ! -z "$CONFIGURE_DEF_OPT" && echo "  ./configure default flags: $CONFIGURE_DEF_OPT"
test ! -z "$CONFIGURE_EXT_OPT" && echo "  ./configure external flags: $CONFIGURE_EXT_OPT"
echo
                                                                                
./configure $CONFIGURE_DEF_OPT $CONFIGURE_EXT_OPT || {
        echo "  configure failed"
        exit 1
}
                                                                                
echo "Now type 'make' to compile $package."
