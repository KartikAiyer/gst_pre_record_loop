FROM ubuntu:22.04

ENV DEBIAN_FRONTEND=noninteractive

# Install Homebrew dependencies and build tools
RUN apt-get update && apt-get install -y \
    build-essential \
    procps \
    curl \
    file \
    git \
    libdw-dev \
    valgrind \
    pkg-config \
    sudo \
    && rm -rf /var/lib/apt/lists/*

# Create a non-root user for Homebrew (Homebrew refuses to run as root)
RUN useradd -m -s /bin/bash linuxbrew && \
    echo 'linuxbrew ALL=(ALL) NOPASSWD:ALL' >> /etc/sudoers

# Switch to linuxbrew user and install Homebrew
USER linuxbrew
WORKDIR /home/linuxbrew

# Install Homebrew with CI flag to make it non-interactive
ENV CI=1
RUN /bin/bash -c "$(curl -fsSL https://raw.githubusercontent.com/Homebrew/install/HEAD/install.sh)"

# Set up Homebrew environment
ENV PATH="/home/linuxbrew/.linuxbrew/bin:/home/linuxbrew/.linuxbrew/sbin:${PATH}"
ENV HOMEBREW_NO_AUTO_UPDATE=1
ENV HOMEBREW_NO_INSTALL_CLEANUP=1

# Install GStreamer 1.26+ and CMake via Homebrew
RUN brew install \
    cmake \
    gstreamer

# Switch back to root for the rest of the container operations
USER root
WORKDIR /workspace

# Set up the environment for all users
RUN echo 'export PATH="/home/linuxbrew/.linuxbrew/bin:/home/linuxbrew/.linuxbrew/sbin:${PATH}"' >> /etc/profile.d/homebrew.sh && \
    chmod +x /etc/profile.d/homebrew.sh

# Make workspace accessible to all users (important for CI workflows)
RUN chmod 777 /workspace

# Optional: Set a default shell that sources the environment
ENV BASH_ENV=/etc/profile.d/homebrew.sh