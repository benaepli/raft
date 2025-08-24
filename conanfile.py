from conan import ConanFile
from conan.tools.cmake import CMakeDeps, CMakeToolchain, cmake_layout


class RaftConan(ConanFile):
    settings = "os", "compiler", "build_type", "arch"

    def requirements(self):
        self.requires("asio/1.30.2")
        self.requires("grpc/1.72.0")
        self.requires("gtest/1.14.0")
        self.requires("spdlog/1.15.3")
        self.requires("sqlitecpp/3.3.2")
        self.requires("tl-expected/1.1.0")

    def layout(self):
        cmake_layout(self)

    def generate(self):
        deps = CMakeDeps(self)
        deps.generate()
        tc = CMakeToolchain(self)
        tc.generate()
