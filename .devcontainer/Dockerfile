FROM ubuntu:24.04

ARG DEBIAN_FRONTEND=noninteractive

RUN apt-get update && apt-get install -y --no-install-recommends \
    build-essential \
    ccache \
    clang \
    clang-tidy \
    cmake \
    gdb \
    git \
    llvm \
    lldb \
    make \
    ninja-build \
    pkg-config \
    valgrind \
    libsdl2-dev \
    libsdl2-ttf-dev \
    libgl-dev \
    mesa-common-dev \
    ca-certificates \
    curl \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /workspace/open-lotto
