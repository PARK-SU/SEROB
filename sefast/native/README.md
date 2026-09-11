# SEROB native core

This directory contains the C++ rating engine and its JavaScript runtime
adapter. Do not compile an individual translation unit directly; the build
script supplies the complete source list and exported WebAssembly functions.

From the repository root, run:

```powershell
.\sefast\build-native.ps1
```

The output directory is `sefast/build/native`.
