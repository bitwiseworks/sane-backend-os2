##  Makefile.am -- an automake template for Makefile.in file
##  Copyright (C) 2009 Chris Bagwell and Sane Developers.
##
##  This file is part of the "Sane" build infra-structure.  See
##  included LICENSE file for license information.

AM_CPPFLAGS += -I. -I$(srcdir) -I$(top_builddir)/include \
 -I$(top_srcdir)/include $(USB_CFLAGS)

bin_PROGRAMS = sane-find-scanner gamma4scanimage
noinst_PROGRAMS = sane-desc
if INSTALL_UMAX_PP_TOOLS
bin_PROGRAMS += umax_pp
else
noinst_PROGRAMS += umax_pp
endif

if CROSS_COMPILING
HOTPLUG =
HOTPLUG_DIRS =
HOTPLUG_DIR =
else
HOTPLUG = hal/libsane.fdi hotplug/libsane.usermap hotplug-ng/libsane.db \
	  udev/libsane.rules
HOTPLUG_DIRS = hal hotplug hotplug-ng udev
HOTPLUG_DIR = dirs
endif

bin_SCRIPTS = sane-config
noinst_SCRIPTS = $(HOTPLUG)
BUILT_SOURCES = $(HOTPLUG_DIR)
CLEANFILES = $(bin_SCRIPTS) $(dist_noinst_SCRIPTS)

EXTRA_DIST = check-po.awk libtool-get-dll-ext mustek600iin-off.c \
	     RenSaneDlls.cmd README xerox
EXTRA_DIST += style-check.sh
EXTRA_DIST += create-changelog.sh
EXTRA_DIST += update-upstreams.sh

sane_find_scanner_SOURCES = sane-find-scanner.c
if have_usblib
sane_find_scanner_SOURCES += check-usb-chip.c
endif
sane_find_scanner_LDADD = ../sanei/libsanei.la ../lib/liblib.la \
			  $(USB_LIBS) $(IEEE1284_LIBS) $(SCSI_LIBS) \
			  ../backend/sane_strstatus.lo

gamma4scanimage_SOURCES = gamma4scanimage.c
gamma4scanimage_LDADD = $(MATH_LIB)

umax_pp_SOURCES = umax_pp.c
umax_pp_LDADD = ../sanei/libsanei.la ../lib/liblib.la $(MATH_LIB) \
	        ../backend/umax_pp_low.lo

sane_desc_SOURCES = sane-desc.c
sane_desc_LDADD = ../sanei/libsanei.la ../lib/liblib.la

EXTRA_DIST += hotplug/README hotplug/libusbscanner
EXTRA_DIST += hotplug-ng/README hotplug-ng/libsane.hotplug
EXTRA_DIST += openbsd/attach openbsd/detach

pkgconfigdir = @libdir@/pkgconfig
pkgconfig_DATA = sane-backends.pc

# When build directory is not same as source directory then any
# subdirectories that targets use must be manually created (under
# the build directory that is).
dirs:
	for subdir in $(HOTPLUG_DIRS); do \
	  $(MKDIR_P) $$subdir || exit 1; \
	  done

descriptions = ${top_srcdir}/doc/descriptions/*.desc ${top_srcdir}/doc/descriptions-external/*.desc

hotplug/libsane.usermap: sane-desc$(EXEEXT) $(descriptions)
	@./sane-desc -m usermap -s "${top_srcdir}/doc/descriptions;${top_srcdir}/doc/descriptions-external" \
	   -d 1 > $@

hotplug-ng/libsane.db: sane-desc$(EXEEXT) $(descriptions)
	@./sane-desc -m db -s "${top_srcdir}/doc/descriptions;${top_srcdir}/doc/descriptions-external" \
	   -d 0 > $@

udev/libsane.rules: sane-desc$(EXEEXT) $(descriptions)
	@./sane-desc -m udev -s "${top_srcdir}/doc/descriptions;${top_srcdir}/doc/descriptions-external" \
	   -d 0 > $@

hal/libsane.fdi: sane-desc$(EXEEXT) $(descriptions)
	@./sane-desc -m hal -s "${top_srcdir}/doc/descriptions;${top_srcdir}/doc/descriptions-external" \
	   -d 0 > $@

clean-local:
	rm -f $(HOTPLUG)
