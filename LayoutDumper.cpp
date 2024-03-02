#include "clang/AST/AST.h"
#include "clang/AST/ASTConsumer.h"
#include "clang/AST/RecursiveASTVisitor.h"
#include "clang/Frontend/CompilerInstance.h"
#include "clang/Frontend/FrontendPluginRegistry.h"
#include "llvm/Support/raw_ostream.h"
using namespace clang;

namespace std {
template <typename charT>
inline bool starts_with(const basic_string<charT> &big,
                        const basic_string<charT> &small) {
  if (&big == &small)
    return true;
  const typename basic_string<charT>::size_type big_size = big.size();
  const typename basic_string<charT>::size_type small_size = small.size();
  const bool valid_ = (big_size >= small_size);
  const bool starts_with_ = (big.compare(0, small_size, small) == 0);
  return valid_ and starts_with_;
}

template <typename charT>
inline bool ends_with(const basic_string<charT> &big,
                      const basic_string<charT> &small) {
  if (&big == &small)
    return true;
  const typename basic_string<charT>::size_type big_size = big.size();
  const typename basic_string<charT>::size_type small_size = small.size();
  const bool valid_ = (big_size >= small_size);
  const bool ends_with_ =
      (big.compare(big_size - small_size, small_size, small) == 0);
  return valid_ and ends_with_;
}
} // namespace std

namespace {

class LayoutDumpConsumer : public ASTConsumer {
  CompilerInstance &Instance;
  std::map<std::string, std::string> &ParsedArgs;

public:
  LayoutDumpConsumer(CompilerInstance &Instance,
                     std::map<std::string, std::string> &ParsedArgs)
      : Instance(Instance), ParsedArgs(ParsedArgs) {}

  void HandleTagDeclDefinition(TagDecl *D) override {
    if (RecordDecl *RD = llvm::dyn_cast<RecordDecl>(D)) {
      auto &context = Instance.getASTContext();
      std::string filter = ParsedArgs["filter"];
      std::string qualifiedName = RD->getQualifiedNameAsString();
      if (filter != "" && qualifiedName.find(filter) == std::string::npos)
        return;
      // Temporary workaround for
      // https://github.com/llvm/llvm-project/issues/83671
      if (CXXRecordDecl *CRD = llvm::dyn_cast<CXXRecordDecl>(RD)) {
        for (auto &base : CRD->bases()) {
          if (base.getType()->getAsCXXRecordDecl() == nullptr)
            return;
        }
      }
      llvm::errs() << "------ Record Decl: " << qualifiedName << "\n";
      context.DumpRecordLayout(RD, llvm::errs());
    }
  }
};

class LayoutDumpAction : public PluginASTAction {
  std::map<std::string, std::string> ParsedArgs;

protected:
  std::unique_ptr<ASTConsumer> CreateASTConsumer(CompilerInstance &CI,
                                                 llvm::StringRef) override {
    return std::make_unique<LayoutDumpConsumer>(CI, ParsedArgs);
  }

  bool ParseArgs(const CompilerInstance &CI,
                 const std::vector<std::string> &args) override {
    ParsedArgs["verbose"] = "false";
    ParsedArgs["filter"] = "";
    for (auto &arg : args) {
      if (arg == "--help") {
        PrintHelp();
        return false;
      } else if (arg == "--verbose") {
        ParsedArgs["verbose"] = "true";
      }
    }
    for (auto &arg : args) {
      if (ParsedArgs["verbose"] == "true") {
        llvm::errs() << "Arg: " << arg << "\n";
      }
      if (std::starts_with(arg, std::string("--filter="))) {
        std::string filter = arg.substr(9);
        ParsedArgs["filter"] = filter;
        if (ParsedArgs["verbose"] == "true")
          llvm::errs() << "ParseArgs: Qualified name filter is " << filter
                       << "\n";
      }
    }
    return true;
  }

  void PrintHelp() {
    std::string help = R"EOF(
clang-object-layout-dumper, A clang plugin dumps C/C++ class or struct's layout.
Options:
    --verbose           Dump detailed information when running plugin.
    --help              Print this help message.
    --filter=[string]   Only dump the layout of class/struct whose qualified name has [string].
    )EOF";
    llvm::errs() << help;
  }
};

} // namespace

static FrontendPluginRegistry::Add<LayoutDumpAction>
    X("layout_dump", "Dump struct/class layout");
