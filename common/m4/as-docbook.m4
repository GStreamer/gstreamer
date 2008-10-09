dnl AS_DOCBOOK([, ACTION-IF-FOUND [, ACTION-IF-NOT-FOUND]])
dnl checks if xsltproc can build docbook documentation
dnl (which is possible if the catalog is set up properly
dnl I also tried checking for a specific version and type of docbook
dnl but xsltproc seemed to happily run anyway, so we can't check for that
dnl and version
dnl this macro takes inspiration from
dnl http://www.movement.uklinux.net/docs/docbook-autotools/configure.html
AC_DEFUN([AS_DOCBOOK],
[
  XSLTPROC_FLAGS=--nonet
  DOCBOOK_ROOT=
  TYPE_LC=xml
  TYPE_UC=XML
  DOCBOOK_VERSION=4.1.2

  if test ! -f /etc/xml/catalog; then
    for i in /usr/share/sgml/docbook/stylesheet/xsl/nwalsh /usr/share/sgml/docbook/xsl-stylesheets/;
    do
      if test -d "$i"; then
        DOCBOOK_ROOT=$i
      fi
    done
  else
    XML_CATALOG=/etc/xml/catalog
    CAT_ENTRY_START='<!--'
    CAT_ENTRY_END='-->'
  fi

  dnl We need xsltproc to process the test
  AC_CHECK_PROG(XSLTPROC,xsltproc,xsltproc,)
  XSLTPROC_WORKS=no
  if test -n "$XSLTPROC"; then
    AC_MSG_CHECKING([whether xsltproc docbook processing works])

    if test -n "$XML_CATALOG"; then
      DB_FILE="http://docbook.sourceforge.net/release/xsl/current/xhtml/docbook.xsl"
    else
      DB_FILE="$DOCBOOK_ROOT/docbook.xsl"
    fi
    $XSLTPROC $XSLTPROC_FLAGS $DB_FILE >/dev/null 2>&1 << END
<?xml version="1.0" encoding='ISO-8859-1'?>
<!DOCTYPE book PUBLIC "-//OASIS//DTD DocBook $TYPE_UC V$DOCBOOK_VERSION//EN" "http://www.oasis-open.org/docbook/$TYPE_LC/$DOCBOOK_VERSION/docbookx.dtd">
<book id="test">
</book>
END
    if test "$?" = 0; then
      XSLTPROC_WORKS=yes
    fi
    AC_MSG_RESULT($XSLTPROC_WORKS)
  fi

  if test "x$XSLTPROC_WORKS" = "xyes"; then
    dnl execute ACTION-IF-FOUND
    ifelse([$1], , :, [$1])
  else
    dnl execute ACTION-IF-NOT-FOUND
    ifelse([$2], , :, [$2])
  fi

  AC_SUBST(XML_CATALOG)
  AC_SUBST(XSLTPROC_FLAGS)
  AC_SUBST(DOCBOOK_ROOT)
  AC_SUBST(CAT_ENTRY_START)
  AC_SUBST(CAT_ENTRY_END)
])
