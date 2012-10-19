dnl Process this file with autoconf to produce a configure script.
AC_INIT([reiserfsprogs], [3.6.22], [reiserfs-devel@vger.kernel.org])
AC_CANONICAL_HOST
AC_PREREQ(2.50)

AH_TEMPLATE([HAVE_LIBUUID], [Define for enable libuuid using.])
AH_TEMPLATE([ENABLE_DEBUG], [Define for enable debug info.])

AM_INIT_AUTOMAKE([1.11.1 dist-xz silent-rules])
AM_SILENT_RULES([yes])
AM_MAINTAINER_MODE

dnl We install in /sbin, the utils are to be available on boot
dnl AC_PREFIX(/)

AM_CONFIG_HEADER(include/config.h)

AC_SUBST(sbindir)

PRESET_CFLAGS=$CFLAGS

dnl Checks for programs.
AC_PROG_CC
AC_PROG_AWK 
AC_PROG_INSTALL
AC_PROG_LN_S
AC_PROG_MAKE_SET
AC_PROG_RANLIB

dnl Checks for libraries.

# Check for LIBUUID
AC_CHECK_LIB(uuid, uuid_generate, , AC_MSG_WARN(libuuid could not be found))
UUID_LIBS="$LIBS"
AC_SUBST(UUID_LIBS)

dnl Checks for header files.
AC_HEADER_STDC
AC_CHECK_HEADERS(fcntl.h limits.h malloc.h sys/ioctl.h unistd.h uuid/uuid.h)
AC_HEADER_MAJOR

dnl Checks for typedefs, structures, and compiler characteristics.
AC_C_CONST
AC_C_INLINE
AC_TYPE_SIZE_T
AC_STRUCT_ST_RDEV

AC_ARG_ENABLE(debug, [  --enable-debug          enable extra checks], 
	      enable_debug=yes,
	      enable_debug=no
)

# Checking for compiler warning options
AC_CACHE_CHECK([whether -Wuninitialized works], [uninitialized], [
	saved_CPPFLAGS="$CPPFLAGS"
	CPPFLAGS="-O1 -Wuninitialized"
	AC_TRY_COMPILE(, , [uninitialized=yes], [uninitialized=no])
	CPPFLAGS="$saved_CPPFLAGS"
])

AC_CACHE_CHECK([whether -Wno-unused-parameter works], [no_unused_parameter], [
	saved_CPPFLAGS="$CPPFLAGS"
	CPPFLAGS="-Wno-unused-parameter"
	AC_TRY_COMPILE(, , [no_unused_parameter=yes], [no_unused_parameter=no])
	CPPFLAGS="$saved_CPPFLAGS"
		])

AC_CACHE_CHECK([whether -Wredundant-decls works], [redundant_decls], [
	saved_CPPFLAGS="$CPPFLAGS"
	CPPFLAGS="-Wredundant-decls"
	AC_TRY_COMPILE(, , [redundant_decls=yes], [redundant_decls=no])
	CPPFLAGS="$saved_CPPFLAGS"
])

# Check for large file
AC_SYS_LARGEFILE

if test -z "${ac_cv_sys_file_offset_bits}"; then
	AC_MSG_WARN(Can't detect right _FILE_OFFSET_BITS. Will be forced to 64bit.)
	ac_cv_sys_file_offset_bits=64
fi

AC_CHECK_SIZEOF(off_t, 64, [ 
	#include <stdio.h>
	#include <sys/types.h>
	#include <unistd.h> 
])

AC_CHECK_SIZEOF(blkcnt_t, 64, [ 
	#include <stdio.h>
	#include <sys/types.h>
	#include <unistd.h> 
])

dnl Checks for library functions.
AC_PROG_GCC_TRADITIONAL
AC_FUNC_MEMCMP
AC_FUNC_STRFTIME
AC_FUNC_VPRINTF
AC_CHECK_FUNCS(strerror strstr strtol statfs getmntent hasmntopt memset time \
	       uname strptime ctime_r register_printf_modifier \
	       register_printf_specifier)

if test -z "${ac_cv_func_register_printf_function}" -a -z "${ac_cv_func_register_printf_specifier}"; then
	AC_MSG_ERROR(reiserfsprogs requires a method to add printf modifiers)
fi


dnl Never enable this. It is for debugging only
AC_ARG_ENABLE(io-failure-emulation,
[  --enable-io-failure-emulation      Never use that. It is for debugging only],
[ if test "$enableval" = "yes" ; then
    echo -e "\nCarefull! IO failure emulation is ON\n"
    AC_DEFINE(IO_FAILURE_EMULATION, 1, [gets set when configure --enable-io-failure-emulation])
  fi
])

if test x$enable_debug = xyes; then
	if test "x" = "x$PRESET_CFLAGS"; then
		CFLAGS="-g -Wall"
	fi
	
	if test x$no_unused_parameter = xyes; then
		CFLAGS="$CFLAGS -Wno-unused-parameter"
	else
		CFLAGS="$CFLAGS -Wno-unused"
	fi
	
	if test x$redundant_decls = xyes; then
		CFLAGS="$CFLAGS -Wredundant-decls"
	fi
	
	if test x$uninitialized = xyes; then
		CFLAGS="$CFLAGS -Wuninitialized -O1"
	else
		CFLAGS="$CFLAGS -O2"
	fi
else
	if test "x" = "x$PRESET_CFLAGS"; then
		CFLAGS="-g -Wall -O2"
	fi
fi

AC_OUTPUT(
    include/Makefile 
    mkreiserfs/Makefile 
    resize_reiserfs/Makefile 
    fsck/Makefile 
    lib/Makefile 
    Makefile 
    reiserfscore/Makefile 
    debugreiserfs/Makefile 
    tune/Makefile)

AC_MSG_NOTICE([

Type 'make' to compile reiserfsprogs])
