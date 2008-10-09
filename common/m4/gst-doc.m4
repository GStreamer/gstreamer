AC_DEFUN([AG_GST_DOCBOOK_CHECK],
[
  dnl choose a location to install docbook docs in
  if test "x$PACKAGE_TARNAME" = "x"
  then
    AC_MSG_ERROR([Internal error - PACKAGE_TARNAME not set])
  fi
  docdir="\$(datadir)/doc/$PACKAGE_TARNAME-$GST_MAJORMINOR"

  dnl enable/disable docbook documentation building
  AC_ARG_ENABLE(docbook,
  AC_HELP_STRING([--enable-docbook],
                 [use docbook to build documentation [default=no]]),,
                 enable_docbook=no)

  have_docbook=no

  if test x$enable_docbook = xyes; then
    dnl check if we actually have everything we need

    dnl check for docbook tools
    AC_CHECK_PROG(HAVE_DOCBOOK2PS, docbook2ps, yes, no)
    AC_CHECK_PROG(HAVE_DOCBOOK2HTML, docbook2html, yes, no)
    AC_CHECK_PROG(HAVE_JADETEX, jadetex, yes, no)
    AC_CHECK_PROG(HAVE_PS2PDF, ps2pdf, yes, no)

    # -V option appeared in 0.6.10
    docbook2html_min_version=0.6.10
    if test "x$HAVE_DOCBOOK2HTML" != "xno"; then
      docbook2html_version=`docbook2html --version`
      AC_MSG_CHECKING([docbook2html version ($docbook2html_version) >= $docbook2html_min_version])
      if perl -w <<EOF
        (\$min_version_major, \$min_version_minor, \$min_version_micro ) = "$docbook2html_min_version" =~ /(\d+)\.(\d+)\.(\d+)/;
        (\$docbook2html_version_major, \$docbook2html_version_minor, \$docbook2html_version_micro ) = "$docbook2html_version" =~ /(\d+)\.(\d+)\.(\d+)/;
        exit (((\$docbook2html_version_major > \$min_version_major) ||
  	     ((\$docbook2html_version_major == \$min_version_major) &&
  	      (\$docbook2html_version_minor >= \$min_version_minor)) ||
  	     ((\$docbook2html_version_major == \$min_version_major) &&
  	      (\$docbook2html_version_minor >= \$min_version_minor) &&
  	      (\$docbook2html_version_micro >= \$min_version_micro)))
  	     ? 0 : 1);
EOF
      then
        AC_MSG_RESULT(yes)
      else
        AC_MSG_RESULT(no)
        HAVE_DOCBOOK2HTML=no
      fi
    fi

    dnl check if we can process docbook stuff
    AS_DOCBOOK(have_docbook=yes, have_docbook=no)
 
    dnl check for extra tools
    AC_CHECK_PROG(HAVE_DVIPS, dvips, yes, no)
    AC_CHECK_PROG(HAVE_XMLLINT, xmllint, yes, no)
 
    dnl check for image conversion tools
    AC_CHECK_PROG(HAVE_FIG2DEV, fig2dev, yes, no)
    if test "x$HAVE_FIG2DEV" = "xno" ; then
      AC_MSG_WARN([Did not find fig2dev (from xfig), images will not be generated.])
    fi
 
    dnl The following is a hack: if fig2dev doesn't display an error message
    dnl for the desired type, we assume it supports it.
    HAVE_FIG2DEV_EPS=no
    if test "x$HAVE_FIG2DEV" = "xyes" ; then
      fig2dev_quiet=`fig2dev -L eps </dev/null 2>&1 >/dev/null`
      if test "x$fig2dev_quiet" = "x" ; then
        HAVE_FIG2DEV_EPS=yes
      fi
    fi
    HAVE_FIG2DEV_PNG=no
    if test "x$HAVE_FIG2DEV" = "xyes" ; then
      fig2dev_quiet=`fig2dev -L png </dev/null 2>&1 >/dev/null`
      if test "x$fig2dev_quiet" = "x" ; then
        HAVE_FIG2DEV_PNG=yes
      fi
    fi
    HAVE_FIG2DEV_PDF=no
    if test "x$HAVE_FIG2DEV" = "xyes" ; then
      fig2dev_quiet=`fig2dev -L pdf </dev/null 2>&1 >/dev/null`
      if test "x$fig2dev_quiet" = "x" ; then
        HAVE_FIG2DEV_PDF=yes
      fi
    fi
  
    AC_CHECK_PROG(HAVE_PNGTOPNM, pngtopnm, yes, no)
    AC_CHECK_PROG(HAVE_PNMTOPS,  pnmtops,  yes, no)
    AC_CHECK_PROG(HAVE_EPSTOPDF, epstopdf, yes, no)
  
    dnl check if we can generate HTML
    if test "x$HAVE_DOCBOOK2HTML" = "xyes" && \
       test "x$enable_docbook" = "xyes" && \
       test "x$HAVE_XMLLINT" = "xyes" && \
       test "x$HAVE_FIG2DEV_PNG" = "xyes"; then
      DOC_HTML=yes
      AC_MSG_NOTICE(Will output HTML documentation)
     else
      DOC_HTML=no
      AC_MSG_NOTICE(Will not output HTML documentation)
    fi
    
    dnl check if we can generate PS
    if test "x$HAVE_DOCBOOK2PS" = "xyes" && \
       test "x$enable_docbook" = "xyes" && \
       test "x$HAVE_XMLLINT" = "xyes" && \
       test "x$HAVE_JADETEX" = "xyes" && \
       test "x$HAVE_FIG2DEV_EPS" = "xyes" && \
       test "x$HAVE_DVIPS" = "xyes" && \
       test "x$HAVE_PNGTOPNM" = "xyes" && \
       test "x$HAVE_PNMTOPS" = "xyes"; then
      DOC_PS=yes
      AC_MSG_NOTICE(Will output PS documentation)
    else
      DOC_PS=no
      AC_MSG_NOTICE(Will not output PS documentation)
    fi
    
    dnl check if we can generate PDF - using only ps2pdf
    if test "x$DOC_PS" = "xyes" && \
       test "x$enable_docbook" = "xyes" && \
       test "x$HAVE_XMLLINT" = "xyes" && \
       test "x$HAVE_PS2PDF" = "xyes"; then
      DOC_PDF=yes
      AC_MSG_NOTICE(Will output PDF documentation)
    else
      DOC_PDF=no
      AC_MSG_NOTICE(Will not output PDF documentation)
    fi

    dnl if we don't have everything, we should disable
    if test "x$have_docbook" != "xyes"; then
      enable_docbook=no
    fi
  fi

  dnl if we're going to install documentation, tell us where
  if test "x$have_docbook" = "xyes"; then
    AC_MSG_NOTICE(Installing documentation in $docdir)
    AC_SUBST(docdir)
  fi

  AM_CONDITIONAL(ENABLE_DOCBOOK,      test x$enable_docbook = xyes)
  AM_CONDITIONAL(DOC_HTML,            test x$DOC_HTML = xyes)
  AM_CONDITIONAL(DOC_PDF,             test x$DOC_PDF = xyes)
  AM_CONDITIONAL(DOC_PS,              test x$DOC_PS = xyes)
])
