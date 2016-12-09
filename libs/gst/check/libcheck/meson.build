libcheck_files = [
  'check.c',
  'check_error.c',
  'check_list.c',
  'check_log.c',
  'check_msg.c',
  'check_pack.c',
  'check_print.c',
  'check_run.c',
  'check_str.c',
  'libcompat/libcompat.c'
]

if not cdata.has('HAVE_ALARM')
  libcheck_files += ['libcompat/alarm.c']
endif

if not cdata.has('HAVE_GETTIMEOFDAY')
  libcheck_files += ['libcompat/gettimeofday.c']
endif

if not cdata.has('HAVE_CLOCK_GETTIME')
  libcheck_files += ['libcompat/clock_gettime.c']
endif

if not cdata.has('HAVE_DECL_LOCALTIME_R')
  libcheck_files += ['libcompat/localtime_r.c']
endif

if not cdata.has('HAVE_DECL_STRSIGNAL')
  libcheck_files += ['libcompat/strsignal.c']
endif

if not cdata.has('HAVE_DECL_STRDUP') and not cdata.has('HAVE__STRDUP')
  libcheck_files += ['libcompat/strdup.c']
endif

if not cdata.has('HAVE_GETLINE')
  libcheck_files += ['libcompat/getline.c']
endif

# FIXME: check that timer_create, timer_settime, timer_delete are in rt_lib
if not rt_lib.found()
  libcheck_files += [
    'libcompat/timer_create.c',
    'libcompat/timer_settime.c',
    'libcompat/timer_delete.c'
  ]
endif

configure_file(input : 'check.h.in',
  output : 'check.h',
  configuration : check_cdata)

internal_check_h_inc = include_directories('..')

libcheck = static_library('check',
  libcheck_files,
  include_directories : [configinc, internal_check_h_inc],
  dependencies : [rt_lib, mathlib],
  c_args: gst_c_args +
	  # Don't want libcompat to think we don't have these and substitute
	  # replacements since we don't check for or define these. See libcompat.h
	  ['-DHAVE_VSNPRINTF', '-DHAVE_SNPRINTF', '-DHAVE_MALLOC', '-DHAVE_REALLOC'],
  pic: true)
