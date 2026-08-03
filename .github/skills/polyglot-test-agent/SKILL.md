---
name: polyglot-test-agent
description: 'Generate unit tests that compile, pass, and match existing project conventions. Use when asked to generate tests, write unit tests, improve or add test coverage, create test files, or test a codebase. Covers this repository''s C++ (Boost.Test via CTest) and Python (pytest) suites, and adapts to other languages by first discovering how the project already tests.'
---

# Test Generation

Add tests that build, pass, and look like the ones already in the repository.

## When to Use This Skill

- Generating tests for a file, module, or whole component
- Raising coverage on existing code
- Adding tests alongside a new feature or a bug fix

## Workflow

Work through these phases yourself, in order. Do not skip discovery: tests that
ignore existing conventions get rejected in review even when they pass.

### 1. Discover

Before writing anything, establish:

- **Which suite the code belongs to.** Changes under `Lib/`, `CLI/` or
  `TestTools/` are C++; changes under `python/` are Python.
- **How similar code is already tested.** Read the nearest existing test file
  and copy its structure, naming, and assertion style.
- **How to build and run that suite** (commands below).
- **What the code actually does.** Read the implementation. Do not infer
  behaviour from a function's name.

### 2. Plan

List the cases you intend to cover before writing them, grouped by the file
under test:

- Happy path with representative input
- Edge cases: empty input, boundaries, first and last element, absent optional data
- Error paths: invalid input, missing files, malformed data

Prefer a few sharp tests over many shallow ones. A test that cannot fail is
worse than no test, because it reads as coverage.

### 3. Implement

Write the tests, then **build and run them**. Iterate until they pass.

Assertions must be specific: check the value, not just that a call returned.
Where a fixture is large, assert against numbers derived from the source data
rather than from a previous run of the code under test — otherwise the test
locks in whatever the code currently does, bugs included.

### 4. Verify

A change is not finished until:

- The suite builds with no new warnings
- Every new test passes, and the rest of the suite still passes
- No test is skipped, commented out, or left as a stub to make the run green

If a test cannot be made to pass, say so and explain why. Do not weaken the
assertion until it passes.

## This Repository

### C++ — Boost.Test, driven by CTest

Tests live in `TestTools/src/` and are registered in `TestTools/CMakeLists.txt`.
They use header-only Boost.Test:

```cpp
#include <boost/test/included/unit_test.hpp>

BOOST_AUTO_TEST_SUITE(RealWorldConversion)

BOOST_AUTO_TEST_CASE(ConvertScan040) { runConversionTest(k_Scans[0]); }

BOOST_AUTO_TEST_SUITE_END()
```

Each case is also registered individually so it can be run on its own:

```cmake
add_test(NAME RealWorld_scan_040
    COMMAND RealWorldTest --run_test=RealWorldConversion/ConvertScan040 --log_level=message)
```

Build and run:

```bash
cmake --preset ninja-release-vcpkg-tests
cmake --build builds/ninja-release-vcpkg-tests -j
ctest --preset ninja-release-vcpkg-tests --output-on-failure
```

Sample scans for fixtures are in `TestData/`. Prefer the small ones
(`TestData/Hole3x5/`, `TestData/Handle/`) — the `real-world/` scans are several
megabytes and slow the suite down.

### Python — pytest

Tests live in `python/tests/` and exercise the compiled bindings, so the
package must be installed first:

```bash
pip install -e ".[test]"
python -m pytest python/tests -q
```

Conventions in that suite worth following:

- Parametrize over the sample scans rather than duplicating a test per file
- Expected counts come from the DCM XML attributes (`vertex_count`,
  `facet_count`, `color`), so tests check the bindings against the source data
  instead of against themselves
- `scan_path()` skips cleanly when `TestData/` is absent, so the suite still
  runs from an installed wheel

### Other languages

Nothing else is currently tested here. If that changes, discover the existing
setup first — test location, naming, framework, and runner — and follow it
rather than introducing a second convention.

## References

- [references/unit-test-generation.md](references/unit-test-generation.md) —
  detailed guidance on parameterization, coverage targets, and per-language
  patterns.
