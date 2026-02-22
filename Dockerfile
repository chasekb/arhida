# arhida-cpp Dockerfile
# Multi-stage build for C++ application

# Build arguments
ARG BUILD_DATE
ARG VERSION=main
ARG REVISION=unknown

# Stage 1: Builder
FROM debian:bookworm-slim AS builder

# Install build dependencies
RUN apt-get update && apt-get install -y \
    build-essential \
    cmake \
    make \
    pkg-config \
    libpq-dev \
    libcurl4-openssl-dev \
    libxml2-dev \
    git \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /build

# Copy source code
COPY . .

# Create build directory
RUN mkdir -p build

# Configure with CMake
RUN cmake -B build -S . \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_INSTALL_PREFIX=/usr/local \
    -DBUILD_TESTS=ON

# Build
RUN cmake --build build -j$(nproc)
RUN cmake --install build

# Stage 2: Final runtime image
FROM debian:bookworm-slim

ARG BUILD_DATE
ARG VERSION
ARG REVISION

# Install runtime dependencies only
RUN apt-get update && apt-get install -y \
    libpq5 \
    libcurl4 \
    libxml2 \
    ca-certificates \
    && rm -rf /var/lib/apt/lists/* \
    && useradd -m -s /bin/bash appuser

WORKDIR /app

# Copy binary from builder
COPY --from=builder /usr/local/bin/arhida-cpp .

# Copy source files (headers, config) for potential runtime needs
COPY --from=builder /build/include/ ./include/
COPY --from=builder /build/src/config/ ./config/
COPY --from=builder /build/CMakeLists.txt ./CMakeLists.txt
COPY --from=builder /build/src/ ./src/

# Copy .env.example as template (users should rename and configure)
COPY .env.example .env

# Create directory for database credentials
RUN mkdir -p /db /app/logs && chown -R appuser:appuser /app

# Switch to non-root user
USER appuser

# Labels
LABEL org.opencontainers.image.title="arXiv Harvester (C++)"
LABEL org.opencontainers.image.version=$VERSION
LABEL org.opencontainers.image.revision=$REVISION
LABEL org.opencontainers.image.created=$BUILD_DATE

ENTRYPOINT ["./arhida-cpp"]
