#!/bin/sh
# Run this to generate all the initial makefiles, etc.

DIE=0
package=GStreamer
srcfile=gst/gstobject.h

(autoconf --version) < /dev/null > /dev/null 2>&1 || {
	echo
	echo "You must have autoconf installed to compile $package."
	echo "Download the appropriate package for your distribution,"
	echo "or get the source tarball at ftp://ftp.gnu.org/pub/gnu/autoconf/"
	DIE=1
}

(automake --version) < /dev/null > /dev/null 2>&1 || {
	echo
	echo "You must have automake installed to compile $package."
	echo "Download the appropriate package for your distribution,"
	echo "or get the source tarball at ftp://ftp.gnu.org/pub/gnu/automake/"
	DIE=1
}
automakevermin=`(automake --version|head -n 1|sed 's/^.* //;s/\./ /g;';echo "1 4")|sort -n|head -n 1`
automakevergood=`(automake --version|head -n 1|sed 's/^.* //;s/\./ /g;';echo "1 4f")|sort -n|head -n 1`
if test "x$automakevermin" != "x1 4"; then
# version is less than 1.4, the minimum suitable version
	echo
	echo "You must have automake version 1.4 or greater installed."
	echo "Download the appropriate package for your distribution,"
	echo "or get the source tarball at ftp://ftp.gnu.org/pub/gnu/automake/"
	DIE=1
else
if test "x$automakevergood" != "x1 4f"; then
echo -n "Checking for patched automake..."
# version is less than 1.4f, the version with the patch applied
# check that patch is applied
cat > autogen.patch.tmp <<EOF
--- 1
+++ 2
@@ -2383,8 +2383,8 @@
 	# to all possible directories, and use it.  If DIST_SUBDIRS is
 	# defined, just use it.
 	local (\$dist_subdir_name);
-	if (&variable_defined ('DIST_SUBDIRS')
-	    || &variable_conditions ('SUBDIRS'))
+	if (&variable_conditions ('SUBDIRS')
+	    || &variable_defined ('DIST_SUBDIRS'))
 	{
 	    \$dist_subdir_name = 'DIST_SUBDIRS';
 	    if (! &variable_defined ('DIST_SUBDIRS'))
EOF
  if patch -s -f --dry-run `which automake` <autogen.patch.tmp >/dev/null 2>&1;
  then
    # Patch succeeded: appropriately patched.
    echo " found."
  else
    # Patch failed: either unpatched or incompatibly patched.
    if patch -R -s -f --dry-run `which automake` <autogen.patch.tmp >/dev/null 2>&1;
    then
      # Reversed patch succeeded: not patched.
      echo " not found."
      echo
      echo "Detected automake version 1.4 (or near) without patch."
      echo "Your version of automake needs a patch applied in order to operate correctly."
      echo
      patchedfile="`pwd`/automake"
      if test -e $patchedfile; then 
	PATCHED=0
      else
        echo "making a patched version..."
        patch -R -s -f `which automake` <autogen.patch.tmp -o $patchedfile;
	chmod +x $patchedfile;
	PATCHED=1
      fi
      echo
      echo "***************************************************************************"
      if test -e $patchedfile; then 
	if test "x$PATCHED" == "x1"; then
	  echo "A patched version of automake is available at:"
	  echo "$patchedfile"
	  echo "You should put this in an appropriate place, or modify \$PATH, so that it is"
	  echo "used in preference to this installed version of automake."
	fi
      fi
      echo "It is not safe to perform the build without a patched automake."
      echo "Read the README file for an explanation."
      echo "***************************************************************************"
      echo
      DIE=1
    else
      # Reversed patch failed: incompatibly patched.
      echo
      echo
      echo "Unable to check whether automake is appropriately patched."
      echo "Your version of automake may need to have a patch applied."
      echo "Read the README file for more explanation."
      echo
    fi
  fi
rm autogen.patch.tmp
fi
fi



(libtool --version) < /dev/null > /dev/null 2>&1 || {
	echo
	echo "You must have libtool installed to compile $package."
	echo "Get the latest version from ftp://alpha.gnu.org/gnu/libtool/"
	DIE=1
}

libtool_version=`libtool --version | sed 's/^.* \([0-9\.]*\) .*$/\1/'`
libtool_major=`echo $libtool_version | cut -d. -f1`
libtool_minor=`echo $libtool_version | cut -d. -f2`
libtool_micro=`echo $libtool_version | cut -d. -f3`
if [ x$libtool_micro = x ]; then
	libtool_micro=0
fi
if [ $libtool_major -le 1 ]; then
	if [ $libtool_major -lt 1 ]; then
		echo
		echo "You must have libtool 1.3.5 or greater to compile $package."
		echo "Get the latest version from ftp://alpha.gnu.org/gnu/libtool/"
		DIE=1
	elif [ $libtool_minor -le 3 ]; then
		if [ $libtool_minor -lt 3 ]; then
			echo
			echo "You must have libtool 1.3.5 or greater to compile $package."
			echo "Get the latest version from ftp://alpha.gnu.org/gnu/libtool/"
			DIE=1
		elif [ $libtool_micro -lt 5 ]; then
			echo
			echo "You must have libtool 1.3.5 or greater to compile $package."
			echo "Get the latest version from ftp://alpha.gnu.org/gnu/libtool/"
			DIE=1
		fi
	fi
fi

if test "$DIE" -eq 1; then
	exit 1
fi

test -f $srcfile || {
	echo "You must run this script in the top-level $package directory"
	exit 1
}

if test -z "$*"; then
	echo "I am going to run ./configure with no arguments - if you wish "
        echo "to pass any to it, please specify them on the $0 command line."
fi


# Generate configure.in and configure.ac
sed <configure.base >configure.in '/^SUBSTFOR configure.ac:.*/d;s/^SUBSTFOR configure.in://g'
sed <configure.base >configure.ac '/^SUBSTFOR configure.in:.*/d;s/^SUBSTFOR configure.ac://g'

libtoolize --copy --force
aclocal $ACLOCAL_FLAGS || {
	echo
	echo "aclocal failed - check that all needed development files are present on system"
	exit 1
}
autoheader || {
	echo
	echo "autoheader failed"
	exit 1
}
autoconf || {
	echo
	echo "autoconf failed"
	#exit 1
}
automake --add-missing || {
	echo
	echo "automake failed"
	#exit 1
}

# now remove the cache, because it can be considered dangerous in this case
rm -f config.cache

# The new configure options for busy application developers (Hadess)
#./configure --enable-maintainer-mode --enable-debug --enable-debug-verbose 

./configure --enable-maintainer-mode --enable-plugin-builddir --enable-debug --enable-DEBUG "$@" || {
	echo
	echo "configure failed"
	exit 1
}

echo 
echo "Now type 'make' to compile $package."
