# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Repository Overview

Volume Cartographer is a C++ toolkit for virtually unwrapping volumetric datasets, designed for recovering text from CT scans of ancient manuscripts. The project uses modern C++17, CMake build system, and Qt6 for GUI applications.

## Key Commands

### Building the Project

```bash
# Standard build (without CUDA)
cmake -S . -B build/ -GNinja -DVC_WITH_CUDA_SPARSE=off
cmake --build build/

# Development build with tests and debug symbols
cmake -S . -B build/ -GNinja -DVC_BUILD_TESTS=ON -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build/

# Build with all warnings enabled
cmake -S . -B build/ -GNinja -DVC_DEVELOPER_WARNINGS=ON
cmake --build build/
```

### Running Tests

```bash
# Run all tests with verbose output
ctest -V --test-dir build/

# Run a specific test
ctest -V --test-dir build/ -R TestName
```

### Code Formatting

```bash
# Format staged changes
git clang-format

# Format all changes compared to develop branch
git clang-format develop

# Check formatting without applying changes
git clang-format --diff
```

## High-Level Architecture

### Core Components

1. **Volume Package (.volpkg)**: Primary data container format for volumes, segmentations, and metadata
2. **OME-Zarr Support**: Modern multi-resolution volumetric data format support in VC3D
3. **Segmentation Pipeline**: Tools for tracing manuscript layers through volumes
4. **Rendering System**: Generates flattened views of segmented surfaces

### Directory Structure

- `core/`: Core library with fundamental algorithms and data structures
  - Volume handling, segmentation algorithms, mesh processing
  - Public headers in `include/vc/core/`
  - Python bindings via pybind11

- `apps/`: Main applications
  - `VC3D/`: New GUI with Zarr support and 3D volume slicing
  - `VC/`: Original Volume Cartographer GUI
  - Command-line tools for specific operations

- `meshing/`: 3D mesh operations (ITK/VTK conversion, smoothing, UV mapping)
- `segmentation/`: Advanced segmentation algorithms (particle simulation, spline fitting)
- `texturing/`: Texture generation and surface flattening
- `app_support/`: Shared application utilities

### Key Design Patterns

1. **Memory-Mapped File Access**: Efficient handling of large TIFF volumes
2. **Multi-threaded Processing**: OpenMP acceleration for compute-intensive operations
3. **Undo/Redo System**: Command pattern implementation for segmentation operations
4. **Signal/Slot Architecture**: Qt-based event handling in GUI applications

### Development Workflow

1. **Branching**: Fork repository, create branches prefixed with issue numbers (e.g., `9-fixes-a-bug`)
2. **Main Branch**: `dev-next` (not `main` or `master`)
3. **Code Style**: Enforced via clang-format, follows EduceLab C++ style guide
4. **Pull Requests**: Use draft PRs for work-in-progress

### Important CMake Options

- `VC_BUILD_APPS`: Build core programs (default: ON)
- `VC_BUILD_GUI`: Build GUI programs (default: ON)
- `VC_BUILD_TESTS`: Build tests (default: OFF for faster builds)
- `VC_WITH_CUDA_SPARSE`: CUDA sparse solver support (default: ON, set OFF if no CUDA)
- `VC_DEVELOPER_WARNINGS`: Enable extensive compiler warnings

### Testing Strategy

- Google Test framework for unit tests
- Tests organized by module in `test/` subdirectories
- Test resources in `res/` subdirectories
- CI runs on Debian 12 with both static and shared library builds

### Common Development Tasks

When working on segmentation features:
1. Check `apps/VC3D/` for the new GUI implementation
2. Core algorithms are in `segmentation/` and `core/src/segmentation/`
3. Test with sample volumes in the test resources

When modifying file I/O:
1. Volume package handling is in `core/src/VolumePkg.cpp`
2. Zarr support is in `core/src/Zarr*.cpp` files
3. Maintain backward compatibility with existing .volpkg files

### Dependencies and Environment

Major dependencies include Qt6, OpenCV, Boost, Eigen3, Ceres Solver, xtensor, and spdlog. The project supports Ubuntu 22.04+ and has Docker configurations for consistent build environments.