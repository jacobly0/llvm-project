add_lldb_library(lldbPluginZigLanguage PLUGIN
  ZigDataFormatters.cpp
  ZigHighlighter.cpp
  ZigLanguage.cpp
  ZigLexer.cpp
  ZigStdDataFormatters.cpp

  LINK_LIBS
    lldbCore
    lldbDataFormatters
    lldbPluginTypeSystemZig

  LINK_COMPONENTS
    Support
)
