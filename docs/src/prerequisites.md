# Prerequisites

## macOS

### LLVM

There is already a version of LLVM preinstalled but it does not contain LLVM development tools (such as `opt`). For this reason, we only add the missing tools to the path to avoid conflicts. (Alternatively, `<llvm-path>/bin` can be added to the path, but this will shadow the preinstalled LLVM tools.)

#### `brew`

```
$ brew install llvm
$ sudo ln -s /opt/homebrew/opt/llvm/bin/opt /usr/local/bin/opt
$ sudo ln -s /opt/homebrew/opt/llvm/bin/llc /usr/local/bin/llc
$ sudo ln -s /opt/homebrew/opt/llvm/bin/llvm-config /usr/local/bin/llvm-config
```

#### Build from source

```
$ git clone https://github.com/llvm/llvm-project.git
$ cd llvm-project
$ cmake -S llvm -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug -DLLVM_PARALLEL_LINK_JOBS=1 -DLLVM_ENABLE_PROJECTS="clang"
$ ninja -C build
$ sudo ln -s /<path>/llvm-project/build/bin/opt /usr/local/bin/opt
$ sudo ln -s /<path>/llvm-project/build/bin/llc /usr/local/bin/llc
$ sudo ln -s /<path>/llvm-project/build/bin/llvm-config /usr/local/bin/llvm-config
```

Refer to the [GettingStarted guide](https://llvm.org/docs/GettingStarted.html#getting-the-source-code-and-building-llvm) for more information.

### Boost

```
$ brew install boost
```

### LibreSSL or OpenSSL

```
$ brew install libressl
```

or:

```
$ brew install openssl
```

### Ghidra

```
$ wget https://github.com/NationalSecurityAgency/ghidra/releases/download/Ghidra_11.3.2_build/ghidra_11.3.2_PUBLIC_20250415.zip
```
Alternatively, Ghidra can be built and installed from [source](https://github.com/NationalSecurityAgency/ghidra/?tab=readme-ov-file#build).

### VS Code

```
$ brew install --cask visual-studio-code
```

Press `Cmd` + `Shift` + `P` and run the `C/C++: Edit Configurations (JSON)` command which will create the `.vscode/c_cpp_properties.json` file. Add the following include paths:

```
{
    "configurations": [
        {
            "name": "Mac",
            "includePath": [
                "${workspaceFolder}/**",
                "/opt/homebrew/opt/llvm/include",
                "/opt/homebrew/opt/llvm/include/llvm",
                "/opt/homebrew/opt/llvm/include/llvm/IR",
                "/opt/homebrew/opt/llvm/include/llvm/Passes",
                "/opt/homebrew/opt/llvm/include/llvm/Support",
                "/opt/homebrew/include"
            ],
            "defines": [],
            "compilerPath": "/usr/bin/clang",
            "cStandard": "c17",
            "cppStandard": "c++17",
            "intelliSenseMode": "macos-clang-arm64"
        }
    ],
    "version": 4
}
```
