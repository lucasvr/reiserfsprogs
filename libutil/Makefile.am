noinst_LIBRARIES          = libutil-static.a

libutil_static_a_SOURCES  = badblock.c device.c misc.c mntent.c print.c
libutil_static_a_LIBADD   = $(top_builddir)/libmisc/libmisc-static.a
libutil_static_a_CFLAGS   = @CFLAGS@

INCLUDES                  = -I$(top_srcdir)/include
