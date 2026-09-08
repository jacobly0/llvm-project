add_lldb_library(lldbPluginHighlighterZig PLUGIN
  ZigHighlighter.cpp
  ZigLexer.cpp

  LINK_COMPONENTS
    Support
  LINK_LIBS
    lldbCore
    lldbUtility
)
