# Project Governance

This document describes the governance structure, decision-making processes, and contribution guidelines for the open-lotto project.

## Table of Contents

- [Project Vision](#project-vision)
- [Maintainers and Roles](#maintainers-and-roles)
- [Decision Making](#decision-making)
- [Contribution Process](#contribution-process)
- [Code Ownership](#code-ownership)
- [Review and Approval](#review-and-approval)
- [Release Management](#release-management)
- [Security](#security)
- [Conflict Resolution](#conflict-resolution)
- [Community Guidelines](#community-guidelines)

## Project Vision

**open-lotto** is a transparent, physics-based lottery simulation engine featuring:
- Realistic 3D ball physics with OpenGL visualization
- Pluggable game system for multiple lottery types
- Deterministic, auditable random number generation
- Educational and professional-grade simulation capabilities

The project emphasizes:
- **Transparency**: Open-source implementation, verifiable algorithms
- **Quality**: Comprehensive testing, performance benchmarks, security practices
- **Accessibility**: Clear documentation, contribution guidelines, community support
- **Extensibility**: Plugin-based architecture, well-defined interfaces

## Maintainers and Roles

### Project Maintainer

**Wissem Boussetta** (@Boussetta)
- **Role**: Primary maintainer, project owner
- **Responsibilities**:
  - Final decision authority on major features and architecture
  - Security vulnerability response and disclosure
  - Release management and versioning decisions
  - Maintainer recruitment and delegation
  - Community communication and announcements

**Contact**: 
- GitHub: https://github.com/Boussetta
- Issues: https://github.com/Boussetta/open-lotto/issues

### Code Owners

Code ownership is managed via `.github/CODEOWNERS` file. Domain experts provide:
- First-level code review for their areas
- Technical guidance and design feedback
- Approval authority for PRs in their domain

Current code owner assignments by domain:
- **Graphics & GUI** (SDL2, OpenGL): @Boussetta
- **Physics Simulation** (Ball dynamics, collisions): @Boussetta
- **Randomness & Entropy** (RNG, seeding): @Boussetta
- **Plugin System** (Loaders, registry, interfaces): @Boussetta
- **Configuration & Validation**: @Boussetta

Note: As the project grows, additional code owners may be assigned based on contributions and expertise.

## Decision Making

### Decision Framework

The project uses a consensus-based decision-making model with the following hierarchy:

#### 1. **Minor Decisions** (Code style, documentation, small fixes)
- **Authority**: Code owner or reviewer
- **Process**: 
  - No formal approval needed
  - Use good judgment and project conventions
  - Request input if uncertain
- **Example**: Formatting changes, typo fixes, small refactors

#### 2. **Standard Decisions** (Features, bug fixes, tests)
- **Authority**: Code owner + 1 additional reviewer
- **Process**:
  - Submit PR with clear description
  - At least 1 approval required (preferably from code owner)
  - Address feedback and concerns
  - Code quality checks must pass
- **Timeline**: No minimum merge delay; merge when criteria met

#### 3. **Major Decisions** (Architecture, breaking changes, governance changes)
- **Authority**: Project maintainer
- **Process**:
  - Propose via GitHub Discussion or Issue
  - Allow 5-7 days for community feedback
  - Document decision in ADR (Architecture Decision Record)
  - Implement via standard PR process
  - Announce in release notes
- **Example**:
  - Plugin system redesign
  - Major API changes
  - New core dependencies
  - Governance changes

#### 4. **Emergency Decisions** (Security hotfixes, critical bugs)
- **Authority**: Project maintainer
- **Process**:
  - Fast-track review and approval
  - Security issues follow responsible disclosure
  - Release as patch version if needed
  - Document post-implementation

### Architecture Decisions

Significant technical decisions are documented using **Architecture Decision Records (ADRs)**.

- **Location**: `docs/adr/`
- **Format**: Use `docs/adr/0000-template.md` as template
- **Numbering**: Sequential (0001, 0002, etc.)
- **Statuses**: Proposed, Accepted, Deprecated, Superseded
- **Who proposes**: Anyone can propose; maintainer approves
- **Review**: Community feedback encouraged during proposal phase

**When to write an ADR**:
- Plugin system redesign ✓
- Major dependency changes ✓
- Physics algorithm changes ✓
- GUI architecture decisions ✓
- Breaking API changes ✓
- Small bug fixes ✗
- Documentation updates ✗

## Contribution Process

### Getting Started

1. **Review** [CONTRIBUTING.md](CONTRIBUTING.md) for:
   - Build and test instructions
   - Code style requirements
   - Plugin development guide

2. **Identify work**:
   - Check [GitHub Issues](https://github.com/Boussetta/open-lotto/issues) for open issues
   - Discuss major features via [GitHub Discussions](https://github.com/Boussetta/open-lotto/discussions) first
   - Create an issue if your idea isn't tracked

3. **Create a feature branch**:
   ```bash
   git checkout -b feature/N-description  # where N is issue number
   ```

### Contribution Types

#### Bug Fixes
- Expected timeline: Address and merge within 1-2 weeks
- Requires:
  - Reproduction steps
  - Test case demonstrating the fix
  - Root cause explanation

#### New Features
- Expected timeline: Discussion (3-5 days) → Implementation → Review (5-7 days)
- Requires:
  - Issue discussion and approval
  - Clear specification
  - Comprehensive tests
  - Documentation updates

#### Documentation
- Expected timeline: Review and merge within 3-5 days
- Examples:
  - README improvements
  - Plugin guide updates
  - API documentation
  - Architecture decision records

#### Plugin Development
- Encouraged contribution type
- Refer to [docs/plugin-guide.md](docs/plugin-guide.md)
- Plugins can be maintained in separate repositories

### Pull Request Process

1. **Before submitting**:
   - Ensure all tests pass locally
   - Run code quality checks:
     ```bash
     ./scripts/run_format_check.sh
     ./scripts/run_cppcheck.sh
     ./scripts/run_clang_tidy.sh
     ./scripts/run_sanitizers.sh
     ```
   - Update documentation if needed
   - Write clear commit messages

2. **Create PR** using template in `.github/pull_request_template.md`:
   - Link related issue (`Closes #N`)
   - Describe changes concisely
   - Indicate type of change
   - Confirm testing and quality checks

3. **PR Review**:
   - Expect feedback within 3-5 business days
   - Address comments promptly
   - Push updates rather than rebasing (easier review)
   - Mark resolved conversations

4. **Approval & Merge**:
   - Requires 1 approval (preferably from code owner)
   - All CI checks must pass
   - Maintainer merges (to maintain clean history)

### Commit Message Format

Follow [Conventional Commits](https://www.conventionalcommits.org/) format:

```
type(scope): subject

body (optional)

footer (optional)
```

**Types**:
- `feat`: New feature
- `fix`: Bug fix
- `docs`: Documentation changes
- `style`: Code style (formatting, etc.)
- `refactor`: Code refactoring without feature change
- `perf`: Performance improvements
- `test`: Test additions/changes
- `chore`: Build, CI, tooling changes

**Scopes** (optional but recommended):
- `combogen`: Ball physics
- `plugin`: Plugin system
- `random`: RNG implementation
- `gui`: GUI components
- `export`: Export functionality
- `cli`: Command-line interface
- etc.

**Examples**:
```
feat(plugin): add Powerball lottery plugin
fix(combogen): handle collision edge case for small counts
docs(readme): update installation instructions
perf(random): optimize entropy pooling
chore(ci): upgrade GitHub Actions versions
```

## Code Ownership

Code ownership ensures domain expertise is applied to reviews.

### Ownership Domains

| Domain | Owner | Directory |
|--------|-------|-----------|
| Core Physics | @Boussetta | `src/combogen.c`, `include/combogen.h` |
| Graphics & GUI | @Boussetta | `src/gui_*.c`, `include/gui_*.h` |
| Plugin System | @Boussetta | `src/plugin_*.c`, `src/plugins/`, `include/plugin_*.h` |
| Random Number Generation | @Boussetta | `src/random*.c`, `include/random*.h` |
| Export | @Boussetta | `src/export.c`, `include/export.h` |
| Configuration | @Boussetta | `src/config.c`, `include/config.h` |
| Tests | @Boussetta | `tests/` |
| Documentation | @Boussetta | `README.md`, `CONTRIBUTING.md`, `docs/` |

### Review Expectations

- **Code owner reviews** PRs affecting their domain
- **Secondary review** from another contributor is encouraged
- **No self-approval**: Authors don't approve their own PRs
- **Blocking reviews**: Code owners can request changes
- **Consensus**: Disagreements escalated to maintainer

## Review and Approval

### Code Review Criteria

Reviewers check:

1. **Correctness**:
   - Does the change address the issue?
   - Are edge cases handled?
   - Is the algorithm/logic sound?

2. **Code Quality**:
   - Follows project style (clang-format, naming conventions)
   - Is the code maintainable?
   - Are complex sections commented?
   - Could it be simplified?

3. **Testing**:
   - Are new features tested?
   - Do all tests pass?
   - Is coverage maintained or improved?

4. **Documentation**:
   - Are API changes documented in headers?
   - Is user-facing behavior described?
   - Are comments present where needed?

5. **Performance**:
   - Does the change introduce regressions?
   - Benchmark results reviewed for performance features

6. **Security**:
   - No hardcoded secrets or credentials
   - No unsafe memory operations without justification
   - Dependencies are appropriate

### Approval Process

- **Standard PRs**: 1 approval required
- **Documentation-only**: 1 review (non-blocking)
- **Major changes**: 2+ approvals encouraged
- **Conflicts**: Escalated to maintainer

### Automated Checks

All PRs require passing:
- CMake build (gcc)
- Unit tests (CTest)
- Code formatting (clang-format)
- Static analysis (cppcheck)
- Code quality (clang-tidy)
- Memory safety (AddressSanitizer, UBSanitizer)
- License compliance (REUSE)

## Release Management

### Versioning

The project uses [Semantic Versioning](https://semver.org/):
- **MAJOR**: Breaking changes (API, plugin interface)
- **MINOR**: New backward-compatible features
- **PATCH**: Bug fixes and minor improvements

Examples: `0.1.0`, `1.0.0`, `1.2.3`

### Release Process

1. **Prepare**:
   - Decide on version number
   - Ensure all PRs for the release are merged
   - Verify all tests pass

2. **Create release**:
   ```bash
   # Update VERSION file
   echo "1.0.0" > VERSION
   git add VERSION
   git commit -m "chore(release): bump version to 1.0.0"
   git push origin main
   
   # Create git tag
   git tag -a v1.0.0 -m "Release version 1.0.0"
   git push origin v1.0.0
   ```

3. **Automated steps** (GitHub Actions):
   - Build Release binary
   - Generate artifacts and checksums
   - Create GitHub Release with changelog
   - Publish release notes

4. **Post-release**:
   - Verify artifacts are available
   - Test installation from release
   - Announce on GitHub

### Release Schedule

- **Patches** (bug fixes): As needed, typically within 1-2 weeks
- **Minor releases** (features): Quarterly or as needed
- **Major releases** (breaking changes): Announced in advance with migration guide

### Changelog

The `CHANGELOG.md` file documents all releases using [Keep a Changelog](https://keepachangelog.com/) format:
- Unreleased section for in-progress work
- Sections for Added, Changed, Deprecated, Removed, Fixed, Security
- Links to GitHub tag comparisons

Changelog entries are auto-generated from commits using conventional commit format (feat:, fix:, perf:).

## Security

### Security Vulnerability Reporting

**Do not** open public issues for security vulnerabilities. Instead:

1. **Contact the maintainer privately** via GitHub Security Advisory or email
2. **Provide details**:
   - Description and impact
   - Steps to reproduce
   - Suggested fix (if available)
3. **Allow time** for maintainer to develop and release a fix before disclosure

**Expected response timeline**: 
- Acknowledgment within 48 hours
- Fix release within 1-2 weeks (depending on severity)
- Coordinated public disclosure

### Dependency Management

- **Monitoring**: Dependabot scans dependencies weekly
- **Auto-updates**: Minor/patch versions auto-merge if CI passes
- **Major versions**: Manual review required
- **Security updates**: Prioritized and released ASAP

### Supported Versions

- **Current version**: Latest main branch
- **Security updates**: Only latest version (no long-term support branches)
- **End of life**: Previous major versions receive no updates

## Conflict Resolution

### Issue Escalation

If disagreements arise during PR review:

1. **First level**: Discuss in PR comments; seek consensus
2. **Second level**: Involve code owner if not already involved
3. **Third level**: Escalate to project maintainer for binding decision
4. **Record decision**: Document reasoning in commit message or ADR

### Disputed Features

For disputed major features:

1. **Proposal**: Write GitHub Discussion with motivation and design
2. **Community feedback**: Allow 5-7 days for input
3. **Decision**: Maintainer makes final call with reasoning
4. **Documentation**: Record decision in ADR

### Respectful Discourse

The project operates under [Contributor Covenant Code of Conduct](CODE_OF_CONDUCT.md):
- Respectful communication
- No personal attacks
- Focus on ideas, not individuals
- Inclusive environment for all contributors

**Enforcement**: Violations reported to maintainer; may result in temporary or permanent restrictions.

## Community Guidelines

### Getting Help

- **Questions**: Open a [GitHub Discussion](https://github.com/Boussetta/open-lotto/discussions)
- **Bug reports**: Open an [Issue](https://github.com/Boussetta/open-lotto/issues) with reproduction steps
- **Feature requests**: Open an Issue or Discussion to gauge interest
- **Security concerns**: Contact maintainer privately (see Security section)

### Communication Channels

| Channel | Purpose |
|---------|---------|
| GitHub Issues | Bug reports, feature requests with clear requirements |
| GitHub Discussions | Questions, brainstorming, design discussions |
| Pull Requests | Code changes, implementation discussion |
| GitHub Security Advisory | Private security vulnerability reports |

### Recognition

Contributors are recognized in:
- Release notes
- GitHub contributors page
- [Sponsor page](https://github.com/sponsors/Boussetta) (if applicable)
- Project README (major contributors)

### Expectations

All community members are expected to:
- **Be respectful**: Treat others with courtesy and professionalism
- **Be constructive**: Focus feedback on improving the project
- **Be patient**: Responses may take time; volunteers maintain this project
- **Be helpful**: Share knowledge and assist other contributors
- **Report issues**: Security issues privately; other issues publicly for transparency

### Stale Issues and PRs

- **Issues**: Marked inactive after 30 days without response; closed after 60 days
- **PRs**: Marked inactive after 14 days without updates; author may be contacted
- **Re-opening**: Contributors can re-open closed items with new information

---

## Document History

| Version | Date | Changes |
|---------|------|---------|
| 1.0 | 2025 | Initial governance document for issue #36 |

## Questions?

For clarification on governance or decision-making processes, please:
1. Check [CONTRIBUTING.md](CONTRIBUTING.md) for developer guidelines
2. Check [CODE_REVIEW.md](CODE_REVIEW.md) for review process
3. Check [RELEASES.md](RELEASES.md) for release procedures
4. Check [SECURITY.md](SECURITY.md) for security policies
5. Open a [GitHub Discussion](https://github.com/Boussetta/open-lotto/discussions)

---

**Last Updated**: 2025  
**Maintained By**: Wissem Boussetta  
**License**: MIT
