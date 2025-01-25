add_lldb_library(lldbPluginTypeSystemZig PLUGIN
  TypeSystemZig.cpp
  ValueObjectZig.cpp
  ZigValue.cpp

  LINK_LIBS
    lldbPluginExpressionParserZig
)
