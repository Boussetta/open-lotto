# Download & Installation Guide

Welcome! **Open Lotto** is available for download on all major platforms. Choose your operating system below for step-by-step installation instructions.

---

## 📥 Quick Download

**Latest Release:** [![GitHub Release](https://img.shields.io/github/v/release/Boussetta/open-lotto)](https://github.com/Boussetta/open-lotto/releases/latest)

All binaries are available at: **[GitHub Releases Page](https://github.com/Boussetta/open-lotto/releases)**

---

## 🐧 Linux

### Option 1: AppImage (Recommended for all distros)

The easiest way—no installation required!

1. Download the latest `open-lotto-x86_64.AppImage` from [Releases](https://github.com/Boussetta/open-lotto/releases)
2. Make it executable:
   ```bash
   chmod +x open-lotto-x86_64.AppImage
   ```
3. Run it:
   ```bash
   ./open-lotto-x86_64.AppImage
   ```

**Compatibility:** Works on any Linux distro with glibc 2.29+ (Ubuntu 18.04+, Debian 10+, Fedora 30+, Arch, etc.)

### Option 2: Flatpak

Sandboxed, system-integrated installation.

1. Install Flatpak:
   ```bash
   # Ubuntu/Debian
   sudo apt install flatpak
   
   # Fedora
   sudo dnf install flatpak
   
   # Arch
   sudo pacman -S flatpak
   ```

2. Install Open Lotto:
   ```bash
   flatpak install flathub com.boussetta.openlotto
   ```

3. Run it:
   ```bash
   flatpak run com.boussetta.openlotto
   ```

**Benefit:** Automatic updates, sandboxed for security.

### Option 3: Build from Source

For developers and advanced users:

```bash
git clone https://github.com/Boussetta/open-lotto.git
cd open-lotto
./scripts/install_deps.sh   # Install build dependencies
./scripts/configure.sh      # Create build directory
cd build && ctest           # Run tests
./open-lotto --help         # Verify installation
```

Requirements: CMake 3.10+, GCC/Clang, SDL2, SDL2_ttf, OpenGL

---

## 🍎 macOS

### Option 1: Homebrew (Easiest)

```bash
brew install boussetta/open-lotto/open-lotto
open-lotto
```

To uninstall:
```bash
brew uninstall open-lotto
```

### Option 2: Direct Download

1. Download the latest `open-lotto-macos-*.zip` from [Releases](https://github.com/Boussetta/open-lotto/releases)
   - Choose `x86_64` for Intel Macs
   - Choose `arm64` for Apple Silicon (M1/M2/M3)

2. Unzip the file:
   ```bash
   unzip open-lotto-macos-*.zip
   ```

3. Double-click `open-lotto.app` in Finder, or run from Terminal:
   ```bash
   ./open-lotto
   ```

### Code Signature Verification

Open Lotto is code-signed and notarized by Apple. On first run, macOS will verify the signature (this happens automatically and is a good sign!).

To manually verify:
```bash
codesign --verify --verbose open-lotto
```

**System Requirements:**
- macOS 12.0+ (Intel or Apple Silicon)
- SDL2, SDL2_ttf (bundled in direct download)

---

## 🪟 Windows

### Option 1: Installer (Easiest)

1. Download `open-lotto-x64-setup.exe` from [Releases](https://github.com/Boussetta/open-lotto/releases)
2. Double-click to run the installer
3. Follow the wizard (Next → Install → Done)
4. Open Lotto will appear in your Start Menu

To uninstall:
- Go to **Settings** → **Apps** → **Apps & features**
- Find "Open Lotto" and click **Uninstall**

### Option 2: Portable Binary (No Installation)

1. Download `open-lotto-windows-x64.exe` from [Releases](https://github.com/Boussetta/open-lotto/releases)
2. Run it directly (no installation needed)
3. All dependencies are bundled

**System Requirements:**
- Windows 10/11 (64-bit)
- No additional software required (dependencies bundled)

---

## ⚙️ Verification

All binaries are signed with SHA256 checksums. To verify your download:

1. Download the binary and corresponding `.sha256` checksum file
2. Run:
   ```bash
   # Linux/macOS
   sha256sum --check open-lotto-x86_64.AppImage.sha256
   
   # Windows (PowerShell)
   CertUtil -hashfile open-lotto-x64-setup.exe SHA256
   ```
3. Compare the output to the checksum file

---

## 🐛 Troubleshooting

### "Permission denied" on Linux
```bash
chmod +x open-lotto-*
./open-lotto-*
```

### "Cannot open" on macOS
Right-click the app → **Open** → **Open** (bypass Gatekeeper)

### DLL not found on Windows
Download the full installer (`.exe`), not just the binary—dependencies are bundled.

### "command not found" on Linux
Either:
- Use absolute path: `./open-lotto-x86_64.AppImage`
- Add to PATH: `mkdir -p ~/.local/bin && cp open-lotto ~/.local/bin/ && export PATH=$PATH:~/.local/bin`

---

## 📦 Building from Source

See [BUILDING_FROM_SOURCE.md](BUILDING_FROM_SOURCE.md) for detailed build instructions across all platforms.

---

## 🤝 Questions?

- **GitHub Issues:** [Report a problem](https://github.com/Boussetta/open-lotto/issues)
- **Discussions:** [Ask a question](https://github.com/Boussetta/open-lotto/discussions)
- **README:** [Back to main documentation](README.md)

---

**Happy drawing!** 🎲✨
