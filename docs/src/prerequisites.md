# Prerequisites

## macOS

### LLVM

There is already a version of LLVM preinstalled but it does not contain LLVM development tools (such as `opt`). For this reason, we install LLVM via `brew` but only add the missing tools to the path to avoid conflicts.

```
$ brew install llvm
$ sudo ln -s /opt/homebrew/opt/llvm/bin/opt /usr/local/bin/opt
$ sudo ln -s /opt/homebrew/opt/llvm/bin/llc /usr/local/bin/llc
$ sudo ln -s /opt/homebrew/opt/llvm/bin/llvm-config /usr/local/bin/llvm-config
```

### Ghidra

```
$ wget https://github.com/NationalSecurityAgency/ghidra/releases/download/Ghidra_11.3.2_build/ghidra_11.3.2_PUBLIC_20250415.zip
```
Alternatively, Ghidra can be built and installed from [source](https://github.com/NationalSecurityAgency/ghidra/?tab=readme-ov-file#build).
