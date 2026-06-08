<!--
SPDX-FileCopyrightText: 2025 Wissem Boussetta
SPDX-License-Identifier: MIT
-->

# Security Policy

## Reporting Security Vulnerabilities

If you discover a security vulnerability in open-lotto, please **do not** open a public GitHub issue. Instead, please follow responsible disclosure by:

1. **Contacting the maintainer privately**: Email security concerns to the project maintainer
2. **Providing details**: Include:
   - Description of the vulnerability
   - Steps to reproduce
   - Potential impact
   - Suggested fix (if available)
3. **Allow time for response**: Give maintainers reasonable time to develop and release a fix before public disclosure

## Security Update Process

### Monitoring Dependencies

We use **Dependabot** for automated dependency scanning:

- **Frequency**: Weekly checks (Mondays at 03:00 UTC)
- **Coverage**: 
  - Python dependencies (if added)
  - GitHub Actions dependencies
- **Automation**: Minor and patch updates auto-merge with passing tests
- **Critical Updates**: Maintainer reviews and approves major version updates

### REUSE Compliance

We enforce REUSE compliance to track software licenses and copyright properly:

- **Tool**: [REUSE Software](https://reuse.software/)
- **Configuration**: `.reuse/dep5` (DEP5 manifest)
- **License**: MIT (main project code)
- **CI Check**: Automated in `security-checks` job
- **Failure Policy**: CI fails if REUSE compliance issues detected

### Supported Versions

Currently, only the latest main branch is actively maintained and receives security updates.

## Dependency Updates

### Automatic Updates
- **Minor/Patch**: Automatically merged if CI passes
- **Frequency**: Weekly pull requests from Dependabot
- **Review**: Commits reviewed and validated before merge

### Major Version Updates
- **Review**: Manual review required
- **Testing**: Full test suite must pass
- **Documentation**: Update CHANGELOG.md if breaking changes

## Known Security Considerations

### Address Space Layout Randomization (ASLR)
The project uses memory safety tools in CI:
- **AddressSanitizer (ASan)**: Detects memory errors
- **UndefinedBehaviorSanitizer (UBSan)**: Detects undefined behavior
- **Testing**: Sanitizers run on every commit

### Input Validation
All CLI arguments are validated:
- Unknown flags rejected
- Invalid values rejected with clear errors
- Tests cover both success and failure paths

### Memory Safety
The C codebase is scanned with:
- **Cppcheck**: Static analysis for memory leaks, null pointer dereferences
- **Clang-tidy**: Code quality and safety checks

## Security Headers

N/A - This is a CLI application without network access or web interfaces.

## Cryptographic Components

The random number generation uses system entropy sources for lottery draws:
- Not suitable for cryptographic use
- Sufficient for lottery draw randomness

## Compliance

### License Compliance (REUSE)
- All files have copyright and license information
- Centralized declaration in `.reuse/dep5`
- Automated CI verification

### Code Quality
- All PRs require passing:
  - Build tests (gcc)
  - Sanitizer checks (ASan, UBSan)
  - Format compliance (clang-format)
  - Code quality (cppcheck, clang-tidy)

## Future Security Enhancements

- [ ] Add SAST (Static Application Security Testing) scanning
- [ ] Consider signing releases with GPG
- [ ] Add dependency lock file for reproducible builds
- [ ] Implement semantic commit validation

## Questions or Suggestions?

For non-security questions about the project, please open a GitHub issue.

---

**Last Updated**: 2025
**Maintained By**: Wissem Boussetta
