# Open3SDCM - Vibe Configuration

This directory contains Vibe-specific configuration for the Open3SDCM project, derived from the `.github/` directory.

## Structure

```
.vibe/
├── README.md              # This file - overview of Vibe configuration
├── instructions.md        # Main project instructions and context
├── agents/                # Agent configurations for different tasks
│   ├── adr-generator.agent.md
│   ├── blueprint-mode.agent.md
│   ├── context-architect.agent.md
│   ├── crypto-re-expert.agent.md
│   ├── debug.agent.md
│   └── expert-cpp-software-engineer.agent.md
├── instructions/          # Domain-specific instructions
│   ├── cmake-vcpkg.instructions.md
│   ├── code-review-generic.instructions.md
│   ├── cpp-language-service-tools.instructions.md
│   ├── github-actions-ci-cd-best-practices.instructions.md
│   ├── markdown-gfm.instructions.md
│   └── oop-design-patterns.instructions.md
├── skills/                # Skill configurations (reference to .github/skills/)
└── workflows/             # Workflow configurations (reference to .github/workflows/)
```

## Project Summary

**Open3SDCM** is a C++20 library and CLI tool for parsing 3Shape DCM dental scan files.

- **Purpose**: Parse unencrypted DCM files (ZIP archives with HPS XML containing base64-encoded binary mesh data)
- **Export**: STL, PLY, OBJ formats via assimp
- **Platforms**: Cross-platform (Linux, macOS, Windows)
- **Build**: CMake + vcpkg manifest mode + Ninja

## Primary Agent

For C++ development tasks, use **`expert-cpp-software-engineer.agent.md`**:
- Modern C++20 best practices
- RAII, value semantics, explicit ownership
- Cross-platform compatibility (MSVC, Clang, GCC)
- Clean Architecture and DDD principles

## Key Files Reference

| File | Purpose |
|------|---------|
| `.github/copilot-instructions.md` | Original project instructions (source of truth) |
| `.github/workflows/ci.yml` | CI configuration (Linux/macOS/Windows, tests, sanitizers) |
| `.github/workflows/release.yml` | Release automation (tag-triggered cross-platform builds) |
| `CMakeLists.txt` | Root CMake configuration |
| `CMakePresets.json` | Build presets for different configurations |
| `vcpkg.json` | vcpkg manifest for dependencies |
| `VERSION` | Version file (read by CMake) |

## Build Presets

| Preset | Description |
|--------|-------------|
| `ninja-release-vcpkg` | Release build with vcpkg dependencies |
| `ninja-release-vcpkg-tests` | Release build with tests enabled |
| `ninja-sanitizers-vcpkg` | Build with ASAN/UBSAN for sanitizer testing |

## Dependencies (vcpkg manifest)

- boost-test
- boost-program-options
- boost-dynamic-bitset
- fmt
- poco
- spdlog
- assimp
- openssl

## Usage Notes

1. **Version Management**: Always read version from `VERSION` file - never hardcode
2. **Code Style**: Run `clang-format` before committing
3. **Testing**: Use `ctest --preset ninja-release-vcpkg-tests --output-on-failure`
4. **Windows**: Ensure `/wd4251`, `/utf-8`, and `-DNOMINMAX` flags are set
5. **XML/Zip**: Use Poco libraries exclusively (not other XML or zip libraries)
