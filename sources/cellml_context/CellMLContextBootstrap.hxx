#include "IfaceCellML_Context.hxx"
#include "Utilities.hxx"

#ifdef IN_CELLMLCONTEXT_MODULE
#define CELLML_CONTEXT_PUBLIC_PRE CDA_EXPORT_PRE
#define CELLML_CONTEXT_PUBLIC_POST CDA_EXPORT_POST
#else
#define CELLML_CONTEXT_PUBLIC_PRE CDA_IMPORT_PRE
#define CELLML_CONTEXT_PUBLIC_POST CDA_IMPORT_POST
#endif

CELLML_CONTEXT_PUBLIC_PRE already_AddRefd<iface::cellml_context::CellMLContext>
  CreateCellMLContext() CELLML_CONTEXT_PUBLIC_POST;
