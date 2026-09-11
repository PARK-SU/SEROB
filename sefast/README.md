# SEROB C++ WebAssembly build

From the repository root, run:

```powershell
.\sefast\build-native.ps1
```

`em++` must be available on `PATH`, or its executable can be supplied with the
`-Compiler` parameter. Output is written to `sefast/build/native`.

The generated JavaScript module, WebAssembly binary, and
`native/sefast_runtime.js` are the three runtime files required by the host
application.
