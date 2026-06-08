class OpenLotto < Formula
  desc "Modular, high-entropy lottery number generator"
  homepage "https://github.com/Boussetta/open-lotto"
  url "https://github.com/Boussetta/open-lotto/archive/refs/tags/v@VERSION@.tar.gz"
  sha256 "@SHA256@"
  license "MIT"

  depends_on "cmake" => :build
  depends_on "pkg-config" => :build
  depends_on "sdl2"
  depends_on "sdl2_ttf"
  depends_on "llvm" => :build  # For OpenMP support

  def install
    # Build with cmake
    system "cmake", "-B", "build", "-DCMAKE_BUILD_TYPE=Release",
                   "-DCMAKE_INSTALL_PREFIX=#{prefix}",
                   "-DCMAKE_FIND_FRAMEWORK=LAST"
    system "cmake", "--build", "build"
    system "cmake", "--install", "build"
  end

  test do
    system "#{bin}/open-lotto", "--help"
  end
end
