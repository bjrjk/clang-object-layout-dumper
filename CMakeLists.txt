add_llvm_library(LayoutDumper MODULE LayoutDumper.cpp PLUGIN_TOOL clang)

if(WIN32 OR CYGWIN)
  set(LLVM_LINK_COMPONENTS
    Support
  )
  clang_target_link_libraries(LayoutDumper PRIVATE
    clangAST
    clangBasic
    clangFrontend
    )
endif()