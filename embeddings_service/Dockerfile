FROM debian:bookworm-slim AS builder

WORKDIR /src

RUN apt-get update && apt-get install -y --no-install-recommends \
    build-essential \
    cmake \
    git \
    curl \
    ca-certificates \
    pkg-config \
    libjsoncpp-dev \
    libssl-dev \
    zlib1g-dev \
    uuid-dev \
    && update-ca-certificates \
    && rm -rf /var/lib/apt/lists/*

COPY embeddings_service /src/embeddings_service

# Ensure host-side build artifacts are not reused inside container context.
RUN rm -rf /src/embeddings_service/build

RUN cmake -S /src/embeddings_service -B /src/embeddings_service/build -DCMAKE_BUILD_TYPE=Debug \
    && cmake --build /src/embeddings_service/build --target arhida-embeddings-service -j"$(nproc)"

FROM debian:bookworm-slim

WORKDIR /app

RUN apt-get update && apt-get install -y --no-install-recommends \
    ca-certificates \
    libstdc++6 \
    libgcc-s1 \
    libjsoncpp25 \
    libssl3 \
    zlib1g \
    libuuid1 \
    && rm -rf /var/lib/apt/lists/*

COPY --from=builder /src/embeddings_service/build/arhida-embeddings-service /app/arhida-embeddings-service

EXPOSE 8000

ENTRYPOINT ["/app/arhida-embeddings-service"]
