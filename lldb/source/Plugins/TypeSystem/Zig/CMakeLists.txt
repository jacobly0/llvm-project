add_lldb_library(lldbPluginTypeSystemZig PLUGIN
  TypeSystemZig.cpp
  ValueObjectVariableZig.cpp
  ValueObjectZig.cpp
  VariableZig.cpp
  ZigValue.cpp

  LINK_LIBS
    lldbPluginExpressionParserZig
    lldbPluginHighlighterZig
    lldbPluginSymbolFileDWARF
)
