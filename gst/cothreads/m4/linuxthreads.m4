dnl Check for LinuxThreads
dnl COTHREADS_CHECK_LINUXTHREADS
dnl no arguments
AC_DEFUN([COTHREADS_CHECK_LINUXTHREADS], [
	AC_CACHE_CHECK([for LinuxThreads],
		[cothreads_cv_linuxthreads],
		[AC_EGREP_CPP(pthread_kill_other_threads_np,
			[#include <pthread.h>],
			[cothreads_cv_linuxthreads=yes],
			[cothreads_cv_linuxthreads=no])
		])
	if test $cothreads_cv_linuxthreads = yes; then
		AC_DEFINE(HAVE_LINUXTHREADS,1,[if you have LinuxThreads])
	fi
])
