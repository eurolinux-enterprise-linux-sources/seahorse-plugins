## Process this file with automake to produce Makefile.in

ACLOCAL_AMFLAGS = -I m4

if WITH_AGENT
AGENT_DIR = agent
else
AGENT_DIR = 
endif

SUBDIRS = libseahorse \
          po \
          data \
          help \
          pixmaps \
          plugins \
          $(AGENT_DIR)

EXTRA_DIST = \
    AUTHORS \
    COPYING \
    COPYING-DOCS \
    ChangeLog \
    ChangeLog.pre-auto \
    ChangeLog.pre-split \
    HACKING \
    INSTALL \
    MAINTAINERS \
    NEWS \
    README \
    THANKS \
    intltool-extract.in \
    intltool-merge.in \
    intltool-update.in \
    gnome-doc-utils.make \
    omf.make 

DISTCLEANFILES = \
    gnome-doc-utils.make \
    intltool-extract \
    intltool-merge \
    intltool-update
    
DISTCHECK_CONFIGURE_FLAGS = \
    --disable-scrollkeeper \
    --disable-update-mime-database \
    --with-nautilus-ext-dir=/tmp \
    --with-epiphany-ext-dir=/tmp

CHANGELOG_START = \
	8a4d5f9e45a8e05e319ce45bb883d265d92c1a6d

dist-hook:
	@if test -d "$(srcdir)/.git"; \
	then \
		echo Creating ChangeLog && \
		( cd "$(top_srcdir)" && \
		  echo '# Generated automatically. Do not edit.'; echo; \
		  $(top_srcdir)/missing --run git log $(CHANGELOG_START).. --stat --date=short ) > ChangeLog.tmp \
		&& mv -f ChangeLog.tmp $(top_distdir)/ChangeLog \
		|| ( rm -f ChangeLog.tmp ; \
		     echo Failed to generate ChangeLog >&2 ); \
	else \
		echo A git clone is required to generate a ChangeLog >&2; \
	fi

distuninstallcheck:
	@:
