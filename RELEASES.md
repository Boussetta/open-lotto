<!--
SPDX-FileCopyrightText: 2025 Wissem Boussetta
SPDX-License-Identifier: MIT
-->

# Release Process

This document describes how to create and publish releases for the open-lotto project.

## Versioning Scheme

open-lotto uses [Semantic Versioning](https://semver.org/) in the format `MAJOR.MINOR.PATCH`:

- **MAJOR**: Significant changes, breaking API changes
- **MINOR**: New features that are backward compatible
- **PATCH**: Bug fixes and minor improvements

Examples: `0.1.0`, `1.0.0`, `1.2.3`

## Version File

The current version is maintained in the `VERSION` file at the repository root:

```
0.1.0
```

Update this file before creating a release tag.

## Creating a Release

### 1. Update the VERSION file

```bash
# Update VERSION file with the new version
echo "1.0.0" > VERSION

# Commit the version bump
git add VERSION
git commit -m "chore(release): bump version to 1.0.0"
git push origin main
```

### 2. Create a git tag

```bash
# Create an annotated tag (recommended)
git tag -a v1.0.0 -m "Release version 1.0.0"

# Push the tag to GitHub
git push origin v1.0.0
```

### 3. Automated Release Creation

Once you push the tag, GitHub Actions automatically:

1. **Builds the project** in Release mode
2. **Generates artifacts**:
   - Source tarball: `open-lotto-1.0.0.tar.gz`
   - Binary tarball: `open-lotto-1.0.0-linux-x64.tar.gz`
   - SBOM file: `open-lotto-v1.0.0.spdx.json`
   - SHA256 checksums for both
3. **Creates a GitHub Release** with:
   - Automated changelog extracted from commit messages
   - All artifacts attached for download
   - Installation instructions
   - Verification steps using checksums

## Release Workflow Details

The automated release workflow (`.github/workflows/release.yml`) performs:

- **Trigger**: On push of git tags matching `v*` (e.g., `v1.0.0`)
- **Build**: CMake Release build configuration
- **Artifacts**: 
  - Source tarball (full repository with git history excluded)
  - Binary tarball (compiled binaries only)
   - SPDX JSON SBOM generated from repository contents
  - SHA256 checksums for integrity verification
- **Changelog**: Automatically extracted from commits since last tag, filtering for `feat:`, `fix:`, and `perf:` commits
- **Release Publication**: Creates GitHub release with artifacts and changelog

## Changelog Generation

The workflow automatically generates a changelog from commits since the previous tag:

```bash
git log <previous-tag>..HEAD --oneline --grep='^feat\|^fix\|^perf'
```

Commits to include in changelog must follow this naming pattern:
- `feat(...): description` - New features
- `fix(...): description` - Bug fixes
- `perf(...): description` - Performance improvements

Example good commit messages:
```
feat(combogen): add new game selection algorithm
fix(cli): handle invalid input gracefully
perf(random): optimize random seed generation
```

## Verifying Downloaded Releases

After downloading a release, verify its integrity:

```bash
# Download the release files
wget https://github.com/Boussetta/open-lotto/releases/download/v1.0.0/open-lotto-1.0.0-linux-x64.tar.gz
wget https://github.com/Boussetta/open-lotto/releases/download/v1.0.0/open-lotto-1.0.0-linux-x64.tar.gz.sha256

# Verify the checksum
sha256sum -c open-lotto-1.0.0-linux-x64.tar.gz.sha256

# Expected output: open-lotto-1.0.0-linux-x64.tar.gz: OK
```

## Installation from Release

### From Binary Tarball

```bash
VERSION=1.0.0
wget https://github.com/Boussetta/open-lotto/releases/download/v${VERSION}/open-lotto-${VERSION}-linux-x64.tar.gz
tar -xzf open-lotto-${VERSION}-linux-x64.tar.gz
cd open-lotto-${VERSION}
./bin/open-lotto
```

### From Source Tarball

```bash
VERSION=1.0.0
wget https://github.com/Boussetta/open-lotto/releases/download/v${VERSION}/open-lotto-${VERSION}.tar.gz
tar -xzf open-lotto-${VERSION}.tar.gz
cd open-lotto-${VERSION}
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
./build/open-lotto
```

## Rollback / Removing a Release

If you need to remove or correct a release:

```bash
# Delete the local tag
git tag -d v1.0.0

# Delete the remote tag
git push --delete origin v1.0.0

# GitHub Actions will not trigger, and the release won't be created
```

To fix and re-release:
1. Fix the issue in code or VERSION file
2. Create a new tag with the corrected version
3. Push the tag

## Pre-Release/Beta Versions

For beta or pre-release versions, use version tags like:
- `v1.0.0-beta.1`
- `v1.0.0-rc.1`

These will also trigger the release workflow and create releases marked as "Pre-release" on GitHub.

## CI/CD Integration

Release tags trigger:
1. **Build & Test**: Full CI suite runs to verify release integrity
2. **Artifact Generation**: Binary and source tarballs created
3. **Checksum Generation**: SHA256 hashes for verification
4. **GitHub Release**: Automatic publication with changelog

No manual GitHub release creation is needed—the workflow handles everything automatically.
