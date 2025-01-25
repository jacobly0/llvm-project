add_lldb_library(lldbPluginExpressionParserZig
  ZigExpressionVariable.cpp
  ZigUserExpression.cpp

  LINK_LIBS
    lldbCore
    lldbExpression
    lldbPluginZigLanguage
    lldbPluginTypeSystemZig
  LINK_COMPONENTS
    Core
    Support
)
