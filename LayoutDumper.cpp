#include <fstream>

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
  std::map<std::string, std::string> ParsedArgs;
  using Layout_t = std::unordered_map<std::string, std::string>;
  Layout_t Layouts;

public:
  LayoutDumpConsumer(CompilerInstance &Instance,
                     std::map<std::string, std::string> ParsedArgs)
      : Instance(Instance), ParsedArgs(ParsedArgs) {}

  void HandleTagDeclDefinition(TagDecl *D) override {
    if (RecordDecl *RD = llvm::dyn_cast<RecordDecl>(D)) {
      auto &context = Instance.getASTContext();
      std::string filter = ParsedArgs["filter"];
      std::string qualifiedName = RD->getQualifiedNameAsString();
      if (filter != "" && qualifiedName.find(filter) == std::string::npos)
        return;
      // Temporary workaround for
      // https://github.com/llvm/llvm-project/issues/83684
      if (RD->isDependentType())
        return;
      // Temporary workaround for
      // https://github.com/llvm/llvm-project/issues/83671
      if (CXXRecordDecl *CRD = llvm::dyn_cast<CXXRecordDecl>(RD)) {
        for (auto &base : CRD->bases()) {
          if (base.getType()->getAsCXXRecordDecl() == nullptr)
            return;
        }
      }
      if (!Layouts.count(qualifiedName)) {
        std::string layoutStr;
        llvm::raw_string_ostream ss(layoutStr);
        context.DumpRecordLayout(RD, ss);
        Layouts.insert({qualifiedName, layoutStr});
      }
    }
  }

  ~LayoutDumpConsumer() override {
    if (ParsedArgs["output"] == "")
      LayoutOutput(llvm::errs(), Layouts);
    else
      HandleFileOutput(ParsedArgs["output"],
                       ParsedArgs["concurrent"] == "true");
  }

  template <typename Stream>
  void QualifiedNameOutput(Stream &os, Layout_t layouts) {
    for (const auto &[qualifiedName, layout] : layouts) {
      os << qualifiedName << "\n";
    }
  }

  template <typename Stream> void LayoutOutput(Stream &os, Layout_t layouts) {
    for (const auto &[qualifiedName, layout] : layouts) {
      os << "------ Record Decl: " << qualifiedName << "\n";
      os << layout;
    }
  }

  void HandleFileOutput(const std::string &path, bool isConcurrent) {
    std::error_code EC;
    if (!isConcurrent) {
      llvm::raw_fd_ostream listStream(path + ".list.log", EC);
      if (EC)
        abort();
      QualifiedNameOutput(listStream, Layouts);
      llvm::raw_fd_ostream layoutStream(path + ".layout.log", EC);
      if (EC)
        abort();
      LayoutOutput(layoutStream, Layouts);
      return;
    }
    // Concurrent

    // Calculate calculatedLayoutList and sort
    std::vector<std::string> calculatedLayoutList;
    for (auto &[qualifiedName, layout] : Layouts) {
      calculatedLayoutList.push_back(qualifiedName);
    }
    std::sort(calculatedLayoutList.begin(), calculatedLayoutList.end());

    // Lock files
    llvm::raw_fd_ostream lockStream(path + ".lock", EC);
    if (EC)
      abort();
    if (auto L = lockStream.lock()) {
      std::ifstream listIStream(path + ".list.log");
      std::ofstream listOStream(path + ".list.log", std::ios::app);
      std::ofstream layoutStream(path + ".layout.log", std::ios::app);
      // Get existedLayoutList and sort
      auto existedLayoutList = getlines(listIStream);
      std::sort(existedLayoutList.begin(), existedLayoutList.end());
      // Get difference
      std::vector<std::string> toBeStoredLayoutList;
      std::set_difference(
          calculatedLayoutList.begin(), calculatedLayoutList.end(),
          existedLayoutList.begin(), existedLayoutList.end(),
          std::inserter(toBeStoredLayoutList, toBeStoredLayoutList.begin()));
      // Output
      for (auto &qualifiedName : toBeStoredLayoutList) {
        listOStream << qualifiedName << "\n";
        layoutStream << "------ Record Decl: " << qualifiedName << "\n";
        layoutStream << Layouts[qualifiedName];
      }
    } else
      abort();
  }

  static std::vector<std::string> getlines(std::ifstream &fs) {
    std::vector<std::string> result;
    fs.seekg(0, std::ios::beg);
    if (!fs.is_open())
      return result;
    while (!fs.eof()) {
      std::string tmp;
      std::getline(fs, tmp);
      result.emplace_back(tmp);
    }
    return result;
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
    ParsedArgs["output"] = "";
    ParsedArgs["concurrent"] = "false";
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
      } else if (std::starts_with(arg, std::string("--output="))) {
        std::string path = arg.substr(9);
        ParsedArgs["output"] = path;
        if (ParsedArgs["verbose"] == "true")
          llvm::errs() << "ParseArgs: Output path is " << path << "\n";
      } else if (arg == "--concurrent") {
        ParsedArgs["concurrent"] = "true";
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
    --output=[path]     Instead of dumping to stderr, dump the list and layout of class/struct
                        to [path].list.log and [path].layout.log, respectively.
    --concurrent        Enable concurrent dumping to the same file by locking the file.
                        Also deduplicate.
    )EOF";
    llvm::errs() << help;
  }
};

} // namespace

static FrontendPluginRegistry::Add<LayoutDumpAction>
    X("layout_dump", "Dump struct/class layout");
