%include "typemap.i"

%typemap(perl5,in) const guchar *
{
  int len;
  $target = (guchar *) SvPV ($source, len);
}

%typemap(perl5,in) const gchar *
{
  int len;
  $target = (gchar *) SvPV ($source, len);
}

