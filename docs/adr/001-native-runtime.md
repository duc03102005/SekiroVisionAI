# ADR-001: C++ GPU core and a planned C# WinUI 3 frontend

Date: 2026-09-10. Status: accepted for the M1 prototype; frontend and packaging remain unimplemented.

## Context

M0 is PASS. Capture needs explicit ownership of WinRT frames, D3D textures and GPU completion. The eventual desktop UI needs responsive controls without moving image buffers through UI bindings. Python remains an offline tooling language.

## Options

| Option | Benefits | Costs and evidence still needed |
|---|---|---|
| C++ core and C++ UI | Direct SDK access and one runtime | More UI implementation work; still needs careful COM and thread ownership |
| C++ core and C# WinUI 3 UI | GPU objects stay native; managed UI development | Two build systems, ABI and shutdown contracts; deployment and UI overhead need measurement |
| C# WinUI 3 with native bindings | One main application language | GPU APIs still cross native boundaries; binding coverage, allocations and frame lifetimes need a separate prototype |

## Decision and reason

Use C++20 for CaptureEngine and GPU scheduling. Plan a C# WinUI 3 product frontend. This is an ownership and maintainability decision, **not a measured claim that one language is faster**. Microsoft supports C# and C++ with WinUI and integration of Windows App SDK into desktop applications ([Windows App SDK overview](https://learn.microsoft.com/en-us/windows/apps/windows-app-sdk/), consulted 2026-09-10).

M1 uses a small native Win32 diagnostic window with HWND selection, Start, Stop, measured counters and trace export. It has no .NET dependency and is not the final frontend. It does not implement Auto Dodge, overlay or prediction controls. UI snapshots update at 4 Hz; per-frame resources remain in the core. No WinUI package or .NET version is selected or installed in this milestone.

The future C ABI must use opaque handles, versioned POD snapshots, explicit string ownership and error codes. Do not export the prototype C++ class directly through P/Invoke. Native stop acknowledgement must precede delegate/handle disposal. No STL objects or exceptions may cross that ABI.

## Toolchain and tradeoffs

The M1 preset selects x64, Visual Studio 2022/v143, Windows SDK 10.0.26100.0 and CMake >=3.25. The CI image is windows-2022; its compiler servicing patch is not immutable, so CI records the actual toolchain and SDK. The [official runner inventory](https://github.com/actions/runner-images/blob/main/images/windows/Windows2022-Readme.md) was checked on 2026-09-10. Release uses the static MSVC CRT; no third-party runtime package is needed for this probe.

The SDK is newer than the minimum API runtime: HWND WGC requires Windows 10 1903. Hardware capture and deployment on that minimum OS remain untested. The first supported test configuration is Windows x64, SDR, Sekiro borderless/windowed, hardware D3D11.

## Validation and follow-up

Compile the actual Windows target in CI, run portable timing tests, then follow the live-game M1 protocol. Hosted CI cannot validate game contention, UI costs, RTX 3070 interoperability or packaging. Before the product UI is implemented, pin Windows App SDK/.NET packages and measure idle/active UI CPU, allocations, capture age and shutdown with both UI hosts. ADR-003/004/005 will cover inference, temporal modeling and overlay only when their milestones have evidence.
