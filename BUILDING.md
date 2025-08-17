# Building

## Dependency Management

This project requires Conan 2 for dependency management. The list of dependencies can be found in
[conanfile.py](conanfile.py).

To install conan, see the [Conan documentation](https://docs.conan.io/2/installation.html). You will need to create a
Conan profile. ou can start by detecting your default host profile:

```bash
conan profile detect --force
```

This creates a profile at `~/.conan2/profiles/default`. You can edit this file to match your system's configuration. For
example, I use a custom profile named `clangdebug` with the following contents:

``` 
[settings]
arch=x86_64
build_type=Debug
compiler=clang
compiler.cppstd=gnu20
compiler.version=20
compiler.libcxx=libstdc++
os=Linux

[conf]
tools.build:compiler_executables={"c": "/usr/bin/clang", "cpp": "/usr/bin/clang++"}
```

Then, to install dependencies, run
`conan install . --build=missing -pr:h=clangdebug -pr:b=clangdebug`.

This will generate a `conan_toolchain.cmake` somewhere in the `build` directory.

## Building

`CMakePresets.json` contains a preset for building with conan. To use it, run:

```bash
cmake --preset conan
cmake --build build
```

You can also configure a `CMakeUserPresets.json` file to set additional options and override the toolchain file. For
example, with my Conan debug profile:

```json
{
  "version": 3,
  "configurePresets": [
    {
      "name": "conan-local",
      "inherits": "conan",
      "toolchainFile": "${sourceDir}/build/Debug/generators/conan_toolchain.cmake"
    }
  ]
}
```

Then, we can simply switch to the `conan-local` preset and build.

This project has been tested with Clang 20 and GCC 14. It should build with MSVC, but I have not tested it.

