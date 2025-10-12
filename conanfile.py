from conan import ConanFile
from conan import ConanFile
from conan.tools.cmake import CMakeToolchain, CMake, cmake_layout

class CampiConan(ConanFile):
    settings = "os", "compiler", "build_type", "arch"
    generators = "CMakeDeps", "CMakeToolchain"

    def requirements(self):
        self.requires("spdlog/1.9.2")

    def layout(self):
        cmake_layout(self)
