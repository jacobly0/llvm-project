add_lldb_library(lldbPluginZigLanguage PLUGIN
  ZigDataFormatters.cpp
  ZigLanguage.cpp
  ZigStdDataFormatters.cpp

  LINK_LIBS
    lldbCore
    lldbDataFormatters
    lldbPluginTypeSystemZig

  LINK_COMPONENTS
    Support
)
