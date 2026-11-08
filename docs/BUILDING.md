# Building and draft releases

Requirements: Git, CMake 3.20+, C++17, and a 32-bit toolchain matching the server. Initialize the pinned SDK first:

```sh
git submodule update --init --recursive
```

Windows (Visual Studio Build Tools with x86 C++ and a Windows SDK):

```sh
cmake -S . -B build -A Win32
cmake --build build --config Release --parallel
```

Output: `build/Release/Addon-SQLite.dll`. The target links SQLite statically; install the x86 MSVC runtime if the target machine lacks it.

Ubuntu with multilib:

```sh
sudo apt-get install cmake gcc-multilib g++-multilib
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DCMAKE_C_FLAGS=-m32 -DCMAKE_CXX_FLAGS=-m32
cmake --build build --parallel
```

Output: `build/Addon-SQLite.so`. The server host needs the compatible 32-bit C/C++ runtimes. CI uses Ubuntu 22.04 as the Linux baseline.

Copy the platform library into a stopped server's `addons` directory and add its name to `setup.json`. No Lua files are required. All consumers must use the same SDK fingerprint; API version remains 1.

## GitHub Actions

Pushes and pull requests on `main` build both Windows x86 and Linux x86. Actions packages each binary, documentation and a manifest identifying the source and SDK commits. For a release, run **Build plugins** manually on `main`, enter a new tag such as `v1.0.0`, and wait for both builds. The final job creates a draft using `GITHUB_TOKEN`; organization settings must allow Actions and repository write permission for that job. Review artifacts and requirements, then publish the draft yourself. Leaving the tag empty builds artifacts only.

The workflow never deploys files or publishes a release automatically. SDK must be pushed before consumers, since recursive checkout fetches their pinned SDK commit.
