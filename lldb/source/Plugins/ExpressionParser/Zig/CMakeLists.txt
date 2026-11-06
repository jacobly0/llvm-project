add_lldb_library(lldbPluginExpressionParserZig
  ZigExpressionVariable.cpp
  ZigPersistentVariables.cpp
  ZigUserExpression.cpp

  LINK_COMPONENTS
    Core
    Support
  LINK_LIBS
    lldbCore
    lldbExpression
    lldbPluginHighlighterZig
    lldbPluginTypeSystemZig
)
