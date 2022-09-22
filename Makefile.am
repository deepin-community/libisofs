
# Copyright (c) 2007 Vreixo Formoso
# Copyright (c) 2009 - 2019 Thomas Schmitt
# Provided under the terms of the GNU General Public License version 2 or later.

# ts A90315 : LIBBURNIA_PKGCONFDIR is defined OS specific in acinclude.m4
# was: pkgconfigdir=$(libdir)/pkgconfig
pkgconfigdir=$(LIBBURNIA_PKGCONFDIR)

libincludedir=$(includedir)/libisofs

lib_LTLIBRARIES = libisofs/libisofs.la
ACLOCAL_AMFLAGS = -I ./

# Enable this if the source includes generated files like version.h
# AM_CPPFLAGS = -I $(top_builddir)/libisofs

## ========================================================================= ##

# Build libraries

libisofs_libisofs_la_LDFLAGS = \
	-version-info $(LT_CURRENT):$(LT_REVISION):$(LT_AGE) $(LIBLDFLAGS)

# Eventually enabling system adapters for ACL and EA.
# ts A90409: Eventually enabling use of zlib.
# ts B00927: Eventually enabling use of libjte (Jigdo Template Extraction)
libisofs_libisofs_la_CFLAGS = $(LIBACL_DEF) $(XATTR_DEF) $(ZLIB_DEF) \
                              $(LIBJTE_DEF)

# ts A90114 : added aaip_0_2.*

libisofs_libisofs_la_SOURCES = \
	libisofs/builder.h \
	libisofs/builder.c \
	libisofs/node.h \
	libisofs/node.c \
	libisofs/tree.h \
	libisofs/tree.c \
	libisofs/find.c \
	libisofs/image.h \
	libisofs/image.c \
	libisofs/fsource.h \
	libisofs/fsource.c \
	libisofs/fs_local.c \
	libisofs/fs_image.c \
	libisofs/messages.h \
	libisofs/messages.c \
	libisofs/libiso_msgs.h \
	libisofs/libiso_msgs.c \
	libisofs/stream.h \
	libisofs/stream.c \
	libisofs/filter.h \
	libisofs/filter.c \
	libisofs/filters/external.c \
	libisofs/filters/zisofs.c \
	libisofs/filters/gzip.c \
	libisofs/util.h \
	libisofs/util.c \
	libisofs/util_rbtree.c \
	libisofs/util_htable.c \
	libisofs/filesrc.h \
	libisofs/filesrc.c \
	libisofs/ecma119.h \
	libisofs/ecma119.c \
	libisofs/ecma119_tree.h \
	libisofs/ecma119_tree.c \
	libisofs/writer.h \
	libisofs/buffer.h \
	libisofs/buffer.c \
	libisofs/rockridge.h \
	libisofs/rockridge.c \
	libisofs/rockridge_read.c \
	libisofs/joliet.h \
	libisofs/joliet.c \
	libisofs/hfsplus.h \
	libisofs/hfsplus.c \
	libisofs/hfsplus_decompose.c \
	libisofs/hfsplus_classes.c \
	libisofs/hfsplus_case.c \
	libisofs/eltorito.h \
	libisofs/eltorito.c \
	libisofs/system_area.h \
	libisofs/system_area.c \
	libisofs/make_isohybrid_mbr.c \
	libisofs/iso1999.h \
	libisofs/iso1999.c \
	libisofs/data_source.c \
	libisofs/aaip_0_2.h \
	libisofs/aaip_0_2.c \
	libisofs/md5.h \
	libisofs/md5.c
libisofs_libisofs_la_LIBADD= \
	$(THREAD_LIBS)
libinclude_HEADERS = \
	libisofs/libisofs.h

install-exec-hook:
	$(LIBBURNIA_LDCONFIG_CMD) "$(DESTDIR)$(libdir)" || echo 'NOTE: Explicit dynamic library configuration failed. If needed, configure manually for:' "$(DESTDIR)$(libdir)"

## ========================================================================= ##

## Build demo applications
noinst_PROGRAMS = \
	demo/demo 

#	demo/tree \
#	demo/find \
#	demo/iso \
#	demo/isoread \
#	demo/isocat \
#	demo/isomodify \
#	demo/isoms

#	demo/ecma119tree \
#	demo/lsl \
#	demo/cat \
#	demo/catbuffer \
#	demo/isogrow


# ts A90807
# Consolidated demo code for having less linker messages with a make run.
demo_demo_CPPFLAGS = -I $(top_srcdir)/libisofs
demo_demo_LDADD = $(libisofs_libisofs_la_OBJECTS) $(libisofs_libisofs_la_LIBADD)
demo_demo_SOURCES = demo/demo.c

# ts A90806
# This includes fsource.h and thus is no API demo
# demo_lsl_CPPFLAGS = -Ilibisofs
# demo_lsl_LDADD = $(libisofs_libisofs_la_OBJECTS) $(libisofs_libisofs_la_LIBADD)
# demo_lsl_SOURCES = demo/lsl.c

# ts A90806
# This includes fsource.h and thus is no API demo
# demo_cat_CPPFLAGS = -Ilibisofs
# demo_cat_LDADD = $(libisofs_libisofs_la_OBJECTS) $(libisofs_libisofs_la_LIBADD)
# demo_cat_SOURCES = demo/cat.c

# ts A90806
# This inlcudes buffer.h and thus is no API demo
# demo_catbuffer_CPPFLAGS = -Ilibisofs
# demo_catbuffer_LDADD = $(libisofs_libisofs_la_OBJECTS) \
# 	$(libisofs_libisofs_la_LIBADD)
# demo_catbuffer_SOURCES = demo/cat_buffer.c

# ts A90807
# Consolidated in demo/demo
# demo_tree_CPPFLAGS = -Ilibisofs
# demo_tree_LDADD = $(libisofs_libisofs_la_OBJECTS) \
# 	$(libisofs_libisofs_la_LIBADD)
# demo_tree_SOURCES = demo/tree.c

# ts A90807
# Consolidated in demo/demo
# demo_find_CPPFLAGS = -Ilibisofs
# demo_find_LDADD = $(libisofs_libisofs_la_OBJECTS) \
# 	$(libisofs_libisofs_la_LIBADD)
# demo_find_SOURCES = demo/find.c

# ts A90806
# This inlcudes lots of internal .h files and thus is no API demo
# demo_ecma119tree_CPPFLAGS = -Ilibisofs
# demo_ecma119tree_LDADD = $(libisofs_libisofs_la_OBJECTS) \
# 	$(libisofs_libisofs_la_LIBADD)
# demo_ecma119tree_SOURCES = demo/ecma119_tree.c

# ts A90807
# Consolidated in demo/demo
# demo_iso_CPPFLAGS = -Ilibisofs
# demo_iso_LDADD = $(libisofs_libisofs_la_OBJECTS) $(libisofs_libisofs_la_LIBADD)
# demo_iso_SOURCES = demo/iso.c

# ts A90807
# Consolidated in demo/demo
# demo_isoread_CPPFLAGS = -Ilibisofs
# demo_isoread_LDADD = $(libisofs_libisofs_la_OBJECTS) \
# 	$(libisofs_libisofs_la_LIBADD)
# demo_isoread_SOURCES = demo/iso_read.c

# ts A90807
# Consolidated in demo/demo
# demo_isocat_CPPFLAGS = -Ilibisofs
# demo_isocat_LDADD = $(libisofs_libisofs_la_OBJECTS) \
# 	$(libisofs_libisofs_la_LIBADD)
# demo_isocat_SOURCES = demo/iso_cat.c

# ts A90807
# Consolidated in demo/demo
# demo_isomodify_CPPFLAGS = -Ilibisofs
# demo_isomodify_LDADD = $(libisofs_libisofs_la_OBJECTS) \
# 	$(libisofs_libisofs_la_LIBADD)
# demo_isomodify_SOURCES = demo/iso_modify.c

# ts A90807
# Consolidated in demo/demo
# demo_isoms_CPPFLAGS = -Ilibisofs
# demo_isoms_LDADD = $(libisofs_libisofs_la_OBJECTS) \
# 	$(libisofs_libisofs_la_LIBADD)
# demo_isoms_SOURCES = demo/iso_ms.c

# demo_isogrow_CPPFLAGS = -Ilibisofs -Ilibburn
# demo_isogrow_LDADD = $(libisofs_libisofs_la_OBJECTS) \
# 	$(libisofs_libisofs_la_LIBADD) \
# 	-lburn
# demo_isogrow_SOURCES = demo/iso_grow.c


## ts A90428 , ticket 147, The test code does not use the API and is totally
##                         outdated in its creation of mocked objects.
##                         A volunteer is needed to rewrite it using the API. 

# ## Build unit test
 
# check_PROGRAMS = \
# 	test/test
# 	
# test_test_CPPFLAGS = -Ilibisofs
# test_test_LDADD = $(libisofs_libisofs_la_OBJECTS) \
# 	$(libisofs_libisofs_la_LIBADD) -lcunit
# test_test_LDFLAGS = -L.. -lm
# 	
# test_test_SOURCES = \
# 	test/test.h \
# 	test/test.c \
# 	test/test_node.c \
# 	test/test_image.c \
# 	test/test_tree.c \
# 	test/test_util.c \
# 	test/test_rockridge.c \
# 	test/test_stream.c \
# 	test/mocked_fsrc.h \
# 	test/mocked_fsrc.c

# "make clean" shall remove a few stubborn .libs directories
# which George Danchev reported Dec 03 2011.
# Learned from: http://www.gnu.org/software/automake/manual/automake.html#Clean
clean-local:
	-rm -rf demo/.libs

## ========================================================================= ##

## Build documentation (You need Doxygen for this to work)

docdir = $(DESTDIR)$(prefix)/share/doc/$(PACKAGE)-$(VERSION)

doc:	doc/html

doc/html: doc/doxygen.conf
	$(RM) -r doc/html; \
	doxygen doc/doxygen.conf;

install-data-local: 
	if [ -d doc/html ]; then \
		$(mkinstalldirs) $(docdir)/html; \
		$(INSTALL_DATA) doc/html/* $(docdir)/html; \
	fi

uninstall-local:
	rm -rf $(docdir)

## ========================================================================= ##

# Extra things
nodist_pkgconfig_DATA = \
	libisofs-1.pc

# ts A80114 : added aaip-os*

EXTRA_DIST = \
	bootstrap \
	libisofs-1.pc.in \
	version.h.in \
	doc/doxygen.conf.in \
	doc/Tutorial \
	README \
	AUTHORS \
	COPYRIGHT \
	COPYING \
	NEWS \
	INSTALL \
	TODO \
	ChangeLog \
	Roadmap \
	doc/susp_aaip_2_0.txt \
	doc/susp_aaip_isofs_names.txt \
	doc/zisofs_format.txt \
	doc/zisofs2_format.txt \
	doc/checksums.txt \
	doc/boot_sectors.txt \
	libisofs/libisofs.ver \
	libisofs/aaip-os-dummy.c \
	libisofs/aaip-os-linux.c \
	libisofs/aaip-os-freebsd.c

