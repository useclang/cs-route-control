# CS Route Control
CS Route Control is a native Windows utility designed to manage Counter-Strike 2 matchmaking connections. It dynamically configures Windows Firewall outbound rules to restrict network traffic to specific Steam Datagram Relay (SDR) regions, ensuring strict latency control. Built natively in C++17 and Qt6.

## Usage Instructions

1. Navigate to the **[Releases](../../releases)** section and download the latest compiled archive.
2. Extract the contents to a local directory.
3. Execute `route_control.exe` **as Administrator** (elevated privileges are strictly required to modify Windows Firewall policies).
4. Use the interface checkboxes to toggle block states for specific regions.
5. Selection Mode: Isolate a single region instantly by using <kbd>Alt</kbd> + <kbd>Left Click</kbd> or <kbd>Middle Mouse Click</kbd> on a region, which automatically blocks all other available locations.

## Building from Source

The following instructions demonstrate the build process using the MSYS2 (UCRT64) environment on Windows.



### Environment Setup (MSYS2)

Install the necessary toolchain and Qt6 libraries:

```bash
pacman -S mingw-w64-ucrt-x86_64-toolchain mingw-w64-ucrt-x86_64-cmake mingw-w64-ucrt-x86_64-qt6-base
```

### Build

```bash
mkdir build && cd build
cmake .. -G "Ninja" -DCMAKE_BUILD_TYPE=Release
cmake --build .
```
