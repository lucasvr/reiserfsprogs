lib_LTLIBRARIES = libreiserfscore.la

libreiserfscore_la_SOURCES = do_balan.c fix_node.c hashes.c ibalance.c \
			     lbalance.c prints.c stree.c node_formats.c \
			     reiserfslib.c bitmap.c journal.c xattr.c includes.h
libreiserfscore_la_LIBADD = ../lib/libmisc.la

pkgconfigdir = $(libdir)/pkgconfig
pkgconfig_DATA = reiserfscore.pc
