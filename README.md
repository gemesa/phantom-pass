# phantom-pass

Phantom pass is a collection of LLVM IR and machine code level obfuscation passes. The techniques are either extracted from reversed malware samples (e.g. [Mirai](https://shadowshell.io/mirai-sora-botnet) and [Hancitor](https://shadowshell.io/hancitor-loader)) or obtained via OSINT. The passes are primarily intended for AArch64, but some also work on other architectures.

The documentation can be found [here](https://shadowshell.io/phantom-pass/).

## How to build and run the passes

```
$ make
```

## How to run the executables

```
$ make run
```
