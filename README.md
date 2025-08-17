# Raft Consensus Library

## Overview

This is an implementation of the Raft consensus algorithm in C++ that I've been writing for fun.
It supports the following features:

- [x] Raft elections
- [x] Persistence
- [x] In-memory network for testing
- [x] gRPC backend for network communication
- [x] A client interface for submitting log entries

And it will support:

- [ ] Log replication (obviously the current priority)
- [ ] Log compaction/snapshotting
- [ ] A wrapper that simplifies snapshot management and log appends.
- [ ] Configuration changes
- [ ] A configuration format for both testing and production use
- [ ] A CLI demo with a linearizable key-value store

This library is designed to be high-throughput. It uses asynchronous networking
through [Asio](https://think-async.com/Asio/)
and multithreading to achieve this.

## Technologies

Here are the main ones:

- C++ 20
- Asio
- gRPC + Protobuf
- [tl::expected](https://github.com/TartanLlama/expected) for error handling
- [spdlog](https://github.com/gabime/spdlog) for logging (and fmt for formatting)
- [GTest](https://github.com/google/googletest)

In general, this code uses a lot of modern C++ features. I also avoid exceptions wherever possible and instead favor
`std::expected` style error handling.

## Design Philosophy

See [docs/design.md](docs/design.md) for more information.

## Building

This project uses CMake as its build system and conan for dependency management.
In CMakePresets.json, there is a preset for building with conan. To use it, run:

```bash
cmake --preset conan
cmake --build build
```

There are three main targets in the CMake project:

- `raft` - The library itself
- `raft_tests` - The unit tests
- `raft_cli` - A simple CLI application that uses the library

For more detailed information, see [docs/building.md](docs/building.md).

## Additional Resources

- [Postmortems](docs/bugs/lessons-learned.md): A hopefully fun and instructive list of bugs I've encountered while
  writing this library.