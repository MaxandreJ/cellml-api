# Interfaces for CellML context...

include_HEADERS += \
  $(top_builddir)/interfaces/IfaceCellML_Context.hxx

lib_LTLIBRARIES += libcellml_context.la

libcellml_context_la_SOURCES := \
  $(top_srcdir)/sources/cellml_context/CellMLContextImplementation.cpp

libcellml_context_la_CXXFLAGS = -I$(top_builddir)/interfaces -I$(top_srcdir)/sources -I$(top_srcdir)/sources/cellml $(AM_CXXFLAGS)

libcellml_context_la_LIBADD := \
  libcellml.la \
  $(STLLINK)

BUILT_SOURCES += $(top_builddir)/interfaces/IfaceCellML_Context.hxx
