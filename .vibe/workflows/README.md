# Open3SDCM - Workflows

This directory references the GitHub Actions workflows from `.github/workflows/` and provides Vibe-specific workflow guidance.

## Available Workflows

| Workflow | File | Trigger | Purpose |
|----------|------|---------|---------|
| **CI** | `.github/workflows/ci.yml` | Push to main, PR, manual | Continuous integration with builds and tests |
| **Release** | `.github/workflows/release.yml` | Tag push (`v*`), manual | Build and publish cross-platform releases |

## CI Workflow (`ci.yml`)

### Jobs

| Job | Platforms | Description |
|-----|-----------|-------------|
| **build** | Linux (GCC/Clang), macOS, Windows | Standard build with tests |
| **sanitizers** | Linux | Build with ASAN/UBSAN for memory error detection |

### Matrix Configuration

| Platform | Compiler | Notes |
|----------|----------|-------|
| ubuntu-latest | GCC | g++ |
| ubuntu-latest | Clang | clang++ |
| macos-latest | AppleClang | Native compiler |
| windows-latest | MSVC | Visual Studio compiler |

### Build Presets Used

- `ninja-release-vcpkg-tests` - Release build with tests
- `ninja-sanitizers-vcpkg` - Sanitizer-enabled build

## Release Workflow (`release.yml`)

### Jobs

| Job | Description |
|-----|-------------|
| **prepare** | Validate version and tag, extract metadata |
| **build** | Build binaries for Linux, macOS, Windows |
| **publish** | Create GitHub release and upload assets |

### Release Process

1. **Tag creation**: Push a tag in format `vX.Y.Z` (e.g., `v1.0.0`)
2. **Version validation**: Workflow checks `VERSION` file matches tag
3. **Cross-platform builds**: Parallel builds on all platforms
4. **Asset packaging**: Archives created per platform
5. **GitHub release**: Release created with all assets attached

### Platform Configuration

| Platform | Asset Suffix | Archive Format | Executable Name |
|----------|--------------|----------------|-----------------|
| Linux | linux | tar.gz (gnutar) | Open3SDCMCLI |
| macOS | macos | tar.gz (gnutar) | Open3SDCMCLI |
| Windows | windows | zip | Open3SDCMCLI.exe |

## Vibe-Specific Workflows

For Vibe usage, these are the recommended workflows:

### Development Workflow

```
1. Feature branch creation
2. Local development with CMake presets
3. Run clang-format before committing
4. Push to branch, create PR
5. CI workflow validates builds and tests
6. Merge to main after approval
```

### Release Workflow

```
1. Bump VERSION file
2. Commit: "Bump version to X.Y.Z"
3. Push tag: vX.Y.Z
4. Release workflow builds and publishes
5. GitHub Actions creates release automatically
```

### Testing Workflow

```
# Standard tests
ctest --preset ninja-release-vcpkg-tests --output-on-failure

# With sanitizers
ctest --preset ninja-sanitizers-vcpkg --output-on-failure
```

## Environment Variables

| Variable | Purpose | Used In |
|----------|---------|---------|
| `VCPKG_FORCE_SYSTEM_BINARIES` | Force system binaries for vcpkg | Linux builds |
| `VCPKG_DEFAULT_TRIPLET` | Default triplet for vcpkg | Windows (`x64-windows`) |
| `SCCache_GHA_ENABLED` | Enable sccache for GitHub Actions | Windows builds |
| `ASAN_OPTIONS` | AddressSanitizer options | Sanitizers job |
| `UBSAN_OPTIONS` | UndefinedBehaviorSanitizer options | Sanitizers job |
