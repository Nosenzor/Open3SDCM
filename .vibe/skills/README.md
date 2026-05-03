# Open3SDCM - Available Skills

This directory references the skills available from `.github/skills/` for use with Vibe.

## Available Skills

| Skill | Directory | Purpose |
|-------|-----------|---------|
| **codeql** | `.github/skills/codeql/` | Static analysis for security vulnerabilities |
| **conventional-commit** | `.github/skills/conventional-commit/` | Commit message conventions |
| **documentation-writer** | `.github/skills/documentation-writer/` | Documentation generation and writing |
| **gh-cli** | `.github/skills/gh-cli/` | GitHub CLI usage and best practices |
| **polyglot-test-agent** | `.github/skills/polyglot-test-agent/` | Test generation across multiple languages |
| **refactor** | `.github/skills/refactor/` | Code refactoring assistance |
| **security-review** | `.github/skills/security-review/` | Security audit and review |

## Using Skills

To use a skill, reference its directory path or load it using the skill name.

### Example Usage

```bash
# Load the security-review skill for auditing
skill name=security-review

# Load the documentation-writer skill for docs
documentation-writer/SKILL.md
```

## Skill Structure

Each skill directory contains:
- `SKILL.md` - Main skill definition and instructions
- `references/` - Reference materials and documentation (where applicable)

## Recommended Skills for Open3SDCM

| Task | Recommended Skill |
|------|------------------|
| Code review | `code-review-generic.instructions.md` (in `.vibe/instructions/`) |
| Security audit | `security-review` |
| Documentation | `documentation-writer` |
| CMake/vcpkg help | `cmake-vcpkg.instructions.md` (in `.vibe/instructions/`) |
| Test generation | `polyglot-test-agent` |
| C++ best practices | `expert-cpp-software-engineer.agent.md` (in `.vibe/agents/`) |
