printf_sources = [
  'asnprintf.c',
  'printf-args.c',
  'printf-parse.c',
  'vasnprintf.c',
  'printf.c',
  'printf-extension.c',
]

printf_args = gst_c_args + ['-DSTATIC=G_GNUC_INTERNAL']

# Don't have a need for that and it's not portable so just ignore for now
printf_args += ['-UHAVE_LONG_DOUBLE']

# Just use internal emulation for printing long longs for now
printf_args += ['-UHAVE_LONG_LONG_FORMAT']

# Don't need any of this widechar stuff, so just disable it for now
printf_args += ['-UHAVE_WCHAR_T', '-UHAVE_WCSLEN', '-UHAVE_WINT_T']

# Check if 'long long' works and what format can be used to print it
# jm_AC_TYPE_LONG_LONG
if cc.compiles('''long long ll = 1LL;
                  int i = 63;
                  int some_func (void) {
                    long long llmax = (long long) -1;
                    return ll << i | ll >> i | llmax / ll | llmax % ll;
                  }''', name : 'long long')
  printf_args += ['-DHAVE_LONG_LONG']
  have_long_long = true
else
  have_long_long = false
endif

# The following uintmax_t/intmax_t checks are also in glib
found_uintmax_t = false

# Define HAVE_INTTYPES_H_WITH_UINTMAX if <inttypes.h> exists,
# doesn't clash with <sys/types.h>, and declares uintmax_t.
# jm_AC_HEADER_INTTYPES_H
if cc.compiles('''#include <sys/types.h>
                  #include <inttypes.h>
                  uintmax_t i = (uintmax_t) -1;
               ''', name : 'uintmax_t in inttypes.h')
  printf_args += ['-DHAVE_INTTYPES_H_WITH_UINTMAX']
  found_uintmax_t = true
endif

# Define HAVE_STDINT_H_WITH_UINTMAX if <stdint.h> exists,
# doesn't clash with <sys/types.h>, and declares uintmax_t.
# jm_AC_HEADER_STDINT_H
if cc.compiles('''#include <sys/types.h>
                  #include <stdint.h>
                  uintmax_t i = (uintmax_t) -1;
               ''', name : 'uintmax_t in stdint.h')
  printf_args += ['-DHAVE_STDINT_H_WITH_UINTMAX']
  found_uintmax_t = true
endif


# Define intmax_t to 'long' or 'long long'
# if it is not already defined in <stdint.h> or <inttypes.h>.
# For simplicity, we assume that a header file defines 'intmax_t'
# if and only if it defines 'uintmax_t'.
printf_args += ['-DHAVE_INTMAX_T']
if not found_uintmax_t
  if have_long_long
    printf_args += ['-Dintmax_t=long long']
  else
    printf_args += ['-Dintmax_t=long']
  endif
endif

printf_lib = static_library('gstprintf',
    printf_sources,
    include_directories : [configinc],
    c_args : printf_args,
    install : false,
    pic: true,
    dependencies : [glib_dep])
