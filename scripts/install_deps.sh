#!/usr/bin/env bash
# install_deps.sh — Install all build dependencies for open-lotto
#
# Supported platforms:
#   Ubuntu / Debian / Pop!_OS / Linux Mint
#   Fedora / RHEL / CentOS / Rocky Linux / AlmaLinux
#   Arch Linux / Manjaro
#   openSUSE Leap / Tumbleweed
#   macOS (via Homebrew)
#
# Usage:
#   ./scripts/install_deps.sh
#
# Dependencies installed:
#   - cmake (>= 3.10)
#   - ccache
#   - gcc / clang (C11 compiler)
#   - pkg-config
#   - make
#   - SDL2 development libraries
#   - SDL2_ttf development libraries
#   - OpenGL / Mesa development libraries

set -euo pipefail

# ---------------------------------------------------------------------------
# Helpers
# ---------------------------------------------------------------------------
info()    { printf '\033[1;34m[INFO]\033[0m  %s\n' "$*"; }
success() { printf '\033[1;32m[OK]\033[0m    %s\n' "$*"; }
warn()    { printf '\033[1;33m[WARN]\033[0m  %s\n' "$*"; }
die()     { printf '\033[1;31m[ERROR]\033[0m %s\n' "$*" >&2; exit 1; }

require_root_or_sudo() {
    if [[ $EUID -ne 0 ]] && ! command -v sudo &>/dev/null; then
        die "This script requires root or sudo. Run as root or install sudo first."
    fi
    SUDO=""
    [[ $EUID -ne 0 ]] && SUDO="sudo"
}

# ---------------------------------------------------------------------------
# Platform detection
# ---------------------------------------------------------------------------
detect_platform() {
    if [[ "$(uname -s)" == "Darwin" ]]; then
        echo "macos"
        return
    fi

    if [[ -f /etc/os-release ]]; then
        # shellcheck source=/dev/null
        source /etc/os-release
        case "${ID:-}" in
            ubuntu|debian|linuxmint|pop)   echo "debian" ;;
            fedora)                        echo "fedora" ;;
            rhel|centos|rocky|almalinux)   echo "rhel"   ;;
            arch|manjaro|endeavouros)      echo "arch"   ;;
            opensuse*|sles)               echo "opensuse" ;;
            *)
                # Fall back to ID_LIKE
                case "${ID_LIKE:-}" in
                    *debian*) echo "debian"  ;;
                    *fedora*|*rhel*) echo "fedora" ;;
                    *arch*)   echo "arch"    ;;
                    *suse*)   echo "opensuse" ;;
                    *) echo "unknown" ;;
                esac
                ;;
        esac
    else
        echo "unknown"
    fi
}

# ---------------------------------------------------------------------------
# Installers per platform
# ---------------------------------------------------------------------------
install_debian() {
    require_root_or_sudo
    info "Detected Ubuntu / Debian based system"
    $SUDO apt-get update -qq
    $SUDO apt-get install -y \
        build-essential \
        ccache \
        cmake \
        pkg-config \
        libsdl2-dev \
        libsdl2-ttf-dev \
        libgl-dev \
        mesa-common-dev
}

install_fedora() {
    require_root_or_sudo
    info "Detected Fedora"
    $SUDO dnf install -y \
        ccache \
        gcc \
        cmake \
        pkgconf-pkg-config \
        make \
        SDL2-devel \
        SDL2_ttf-devel \
        mesa-libGL-devel \
        mesa-libGLU-devel
}

install_rhel() {
    require_root_or_sudo
    info "Detected RHEL / CentOS / Rocky / AlmaLinux"
    # Enable EPEL for SDL2 packages
    if command -v dnf &>/dev/null; then
        $SUDO dnf install -y epel-release || warn "Could not install EPEL; SDL2 packages may not be available."
        $SUDO dnf install -y \
            ccache \
            gcc \
            cmake \
            pkgconf-pkg-config \
            make \
            SDL2-devel \
            SDL2_ttf-devel \
            mesa-libGL-devel
    else
        $SUDO yum install -y epel-release || warn "Could not install EPEL."
        $SUDO yum install -y \
            ccache \
            gcc \
            cmake \
            pkgconfig \
            make \
            SDL2-devel \
            SDL2_ttf-devel \
            mesa-libGL-devel
    fi
}

install_arch() {
    require_root_or_sudo
    info "Detected Arch Linux / Manjaro"
    $SUDO pacman -Sy --noconfirm \
        base-devel \
        ccache \
        cmake \
        pkgconf \
        sdl2 \
        sdl2_ttf \
        mesa
}

install_opensuse() {
    require_root_or_sudo
    info "Detected openSUSE"
    $SUDO zypper install -y \
        ccache \
        gcc \
        cmake \
        pkg-config \
        make \
        libSDL2-devel \
        libSDL2_ttf-devel \
        Mesa-libGL-devel
}

install_macos() {
    info "Detected macOS"
    if ! command -v brew &>/dev/null; then
        die "Homebrew is not installed. Install it from https://brew.sh and re-run this script."
    fi
    brew install cmake ccache sdl2 sdl2_ttf pkg-config
    # OpenGL is provided by the macOS SDK — no extra package needed
    success "macOS dependencies installed via Homebrew"
}

# ---------------------------------------------------------------------------
# Verification
# ---------------------------------------------------------------------------

# Build a pkg-config search path that always includes system directories,
# even when PKG_CONFIG_PATH has been overridden by a cross-build environment
# (e.g. Buildroot, Yocto, ROS).
system_pkg_config_path() {
    local sys_paths=(
        /usr/lib/x86_64-linux-gnu/pkgconfig
        /usr/lib/aarch64-linux-gnu/pkgconfig
        /usr/lib/arm-linux-gnueabihf/pkgconfig
        /usr/lib/pkgconfig
        /usr/share/pkgconfig
        /usr/local/lib/pkgconfig
        /usr/local/share/pkgconfig
    )
    local joined=""
    for p in "${sys_paths[@]}"; do
        [[ -d "$p" ]] && joined="${joined:+$joined:}$p"
    done
    echo "$joined"
}

verify_deps() {
    info "Verifying installed tools..."
    local missing=()

    command -v cmake      &>/dev/null || missing+=("cmake")
    command -v ccache     &>/dev/null || missing+=("ccache")
    command -v pkg-config &>/dev/null || missing+=("pkg-config")
    command -v make       &>/dev/null || missing+=("make")
    { command -v gcc &>/dev/null || command -v clang &>/dev/null; } || missing+=("gcc or clang")

    # Augment PKG_CONFIG_PATH with system directories so verification works
    # even when a cross-build environment (Buildroot, Yocto, …) has replaced it.
    local sys_path
    sys_path=$(system_pkg_config_path)
    local effective_path="${PKG_CONFIG_PATH:+$PKG_CONFIG_PATH:}$sys_path"

    if ! PKG_CONFIG_PATH="$effective_path" pkg-config --exists sdl2 2>/dev/null; then
        missing+=("SDL2 (libsdl2-dev / SDL2-devel)")
    fi
    if ! PKG_CONFIG_PATH="$effective_path" pkg-config --exists SDL2_ttf 2>/dev/null; then
        missing+=("SDL2_ttf (libsdl2-ttf-dev / SDL2_ttf-devel)")
    fi

    if [[ ${#missing[@]} -gt 0 ]]; then
        warn "The following dependencies could not be verified:"
        for dep in "${missing[@]}"; do
            warn "  - $dep"
        done
        warn "Try installing them manually and ensure pkg-config is on PATH."
        return 1
    fi

    success "All required dependencies are present."
}

# ---------------------------------------------------------------------------
# Main
# ---------------------------------------------------------------------------
main() {
    info "open-lotto dependency installer"
    info "================================"

    local platform
    platform=$(detect_platform)

    case "$platform" in
        debian)   install_debian  ;;
        fedora)   install_fedora  ;;
        rhel)     install_rhel    ;;
        arch)     install_arch    ;;
        opensuse) install_opensuse ;;
        macos)    install_macos   ;;
        unknown)
            warn "Unrecognised Linux distribution."
            warn "Please install the following packages manually:"
            warn "  cmake (>= 3.10), gcc/clang, make, pkg-config"
            warn "  SDL2 development libraries"
            warn "  SDL2_ttf development libraries"
            warn "  OpenGL / Mesa development libraries"
            exit 1
            ;;
    esac

    echo ""
    verify_deps
    echo ""
    success "Dependencies installed. You can now build:"
    info "  mkdir build && cd build"
    info "  cmake .."
    info "  cmake --build . -j"
}

main "$@"
