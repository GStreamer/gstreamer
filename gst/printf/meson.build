printf_sources = [
  'asnprintf.c',
  'printf-args.c',
  'printf-parse.c',
  'vasnprintf.c',
  'printf.c',
  'printf-extension.c',
]

printf_args = gst_c_args + [
#  '-UHAVE_LONG_DOUBLE',
#  '-UHAVE_LONG_LONG_FORMAT',
#  ' -UHAVE_WCHAR_T',
#  '-UHAVE_WCSLEN',
#  '-UHAVE_WINT_T',
  '-DSTATIC=G_GNUC_INTERNAL',
]

printf_lib = static_library('gstprintf',
    printf_sources,
    include_directories : [configinc],
    c_args : printf_args + pic_args,
    install : false,
    dependencies : [glib_dep])
