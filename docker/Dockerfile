FROM ubuntu:22.04

ENV DEBIAN_FRONTEND=noninteractive

# Install Homebrew dependencies and some Gstreamer deps
RUN apt-get update && apt-get install -y \
    build-essential \
    procps \
    curl \
    file \
    git \
    libdw-dev \
    valgrind \
    pkg-config \
    && rm -rf /var/lib/apt/lists/*

# Install Homebrew
RUN /bin/bash -c "$(curl -fsSL https://raw.githubusercontent.com/Homebrew/install/HEAD/install.sh)"
ENV PATH="/home/linuxbrew/.linuxbrew/bin:${PATH}"

# Install GStreamer via Homebrew
RUN brew install \
    cmake \
    gstreamer 

WORKDIR /workspace