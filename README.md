# clang-object-layout-dumper

A clang plugin dumps C/C++ class or struct's layout.

## Build

```bash
sudo apt update
sudo apt install -y build-essential cmake clang gcc ninja-build
git clone https://github.com/llvm/llvm-project.git
cd llvm-project
git clone https://github.com/bjrjk/clang-object-layout-dumper.git clang/tools/clang-object-layout-dumper
pushd clang/tools/clang-object-layout-dumper
git checkout 19.0.0
popd

mkdir build_debug
cd build_debug
cmake -G Ninja ../llvm -DCMAKE_BUILD_TYPE=Debug -DLLVM_ENABLE_PROJECTS=clang -DLLVM_TARGETS_TO_BUILD=X86 -DBUILD_SHARED_LIBS=ON -DCMAKE_EXPORT_COMPILE_COMMANDS=1
autoninja
cd ..

mkdir build_release
cd build_release
cmake -G Ninja ../llvm -DCMAKE_BUILD_TYPE=Release -DLLVM_ENABLE_PROJECTS=clang -DLLVM_TARGETS_TO_BUILD=X86 -DBUILD_SHARED_LIBS=ON
autoninja
```

## Run

```bash
bin/clang++ \
  -Xclang -load -Xclang lib/LayoutDumper.so -Xclang -plugin -Xclang layout_dump \
  -fplugin-arg-layout_dump-[ARG1] -fplugin-arg-layout_dump-[ARG2] ... -fplugin-arg-layout_dump-[ARGn] \
  test.cpp
```