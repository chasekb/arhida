# C++ Migration Plan for arXiv Harvester

## Project Overview

The current Python project (`arhida.py`) is an arXiv Academic Paper Metadata Harvester that:
- Harvests metadata from arXiv.org's OAI-PMH interface
- Stores data in PostgreSQL with JSONB fields
- Implements rate limiting (3-second delays)
- Supports batch processing and backfill functionality
- Command-line interface with multiple modes

---

## Phase 1: C++ Equivalent Technologies

| Python Component | C++ Equivalent | Library/Framework |
|-----------------|----------------|-------------------|
| `sickle` (OAI-PMH) | Custom OAI-PMH client | libcurl + libxml2 |
| `psycopg2-binary` | PostgreSQL driver | libpq (PostgreSQL C API) |
| `requests` | HTTP client | libcurl (curlpp or raw libcurl) |
| `python-dotenv` | Environment variables | boost::filesystem or manual |
| `json` | JSON serialization | nlohmann/json |
| `logging` | Structured logging | spdlog |
| `icecream` | Debugging | Custom macros or spdlog debug |

---

## Phase 2: Project Structure

```
arhida-cpp/
├── CMakeLists.txt           # Build configuration
├── .gitignore
├── Dockerfile               # Multi-stage build
├── docker-compose.yaml      # Container orchestration
├── src/
│   ├── main.cpp            # Entry point with CLI
│   ├── config/
│   │   └── Config.h        # Environment configuration
│   ├── oai/
│   │   ├── OaiClient.h     # OAI-PMH client interface
│   │   ├── OaiClient.cpp
│   │   ├── Record.h        # Metadata record model
│   │   └── Record.cpp
│   ├── db/
│   │   ├── Database.h      # PostgreSQL connection
│   │   ├── Database.cpp
│   │   └── QueryBuilder.h  # SQL query builder
│   ├── harvester/
│   │   ├── Harvester.h     # Main harvester logic
│   │   ├── Harvester.cpp
│   │   └── RateLimiter.h   # Rate limiting
│   └── utils/
│       ├── Logger.h        # Logging utilities
│       └── JsonHelper.h    # JSON serialization
├── include/                 # Header-only libraries
│   └── nlohmann/
└── tests/
    └── test_main.cpp
```

---

## Phase 3: Implementation Roadmap

### Step 1: Project Setup (Week 1)
- [x] Create CMakeLists.txt with dependencies
- [x] Set up Docker build environment
- [x] Integrate libpq, libcurl, libxml2, nlohmann/json, spdlog

### Step 2: Core Infrastructure (Week 1-2)
- [x] Implement Config class for environment variables
- [x] Set up spdlog logging system
- [x] Create Database class with connection pooling
- [x] Implement JSON serialization utilities

### Step 3: OAI-PMH Client (Week 2-3)
- [x] Implement HTTP client with libcurl
- [x] Parse OAI-PMH XML responses ( Dublin Core metadata)
- [x] Handle rate limiting (3-second delays)
- [x] Implement retry logic for 503/429 errors

### Step 4: Harvester Logic (Week 3-4)
- [x] Implement record extraction from XML
- [x] Create batch processing (2000 records/batch)
- [x] Implement upsert logic for PostgreSQL
- [x] Add progress tracking and logging

### Step 5: CLI & Backfill (Week 4-5)
- [x] Implement command-line interface (CLI11 or boost::program_options)
- [x] Implement date range detection
- [x] Create backfill missing dates logic
- [x] Add statistics and final summary

### Step 6: Testing & Optimization (Week 5-6)
- [ ] Unit tests for core components
- [ ] Integration tests with PostgreSQL
- [ ] Performance optimization
- [ ] Memory leak detection

---

## Phase 4: Key Implementation Details

### Database Schema (PostgreSQL)

```cpp
// Same schema as Python version
struct ArxivRecord {
    int id;
    std::string header_datestamp;
    std::string header_identifier;
    json header_setSpecs;
    json metadata_creator;
    json metadata_date;
    std::string metadata_description;
    json metadata_identifier;
    json metadata_subject;
    json metadata_title;
    std::string metadata_type;
    timestamp created_at;
    timestamp updated_at;
};
```

### PostgreSQL Table Creation (from Python version)

```cpp
const char* CREATE_SCHEMA_QUERY = 
    "CREATE SCHEMA IF NOT EXISTS {schema}";

const char* CREATE_TABLE_QUERY = R"(
    CREATE TABLE IF NOT EXISTS {schema}.{table} (
        id SERIAL PRIMARY KEY,
        header_datestamp TIMESTAMP,
        header_identifier VARCHAR(255) UNIQUE NOT NULL,
        header_setSpecs JSONB,
        metadata_creator JSONB,
        metadata_date JSONB,
        metadata_description TEXT,
        metadata_identifier JSONB,
        metadata_subject JSONB,
        metadata_title JSONB,
        metadata_type VARCHAR(100),
        created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
        updated_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP
    )
)";

const char* CREATE_INDEXES_QUERY[] = {
    "CREATE UNIQUE INDEX IF NOT EXISTS {table}_header_identifier_idx ON {schema}.{table} (header_identifier)",
    "CREATE INDEX IF NOT EXISTS {table}_header_datestamp_idx ON {schema}.{table} (header_datestamp)",
    "CREATE INDEX IF NOT EXISTS {table}_header_setspecs_idx ON {schema}.{table} USING GIN (header_setSpecs)",
    "CREATE INDEX IF NOT EXISTS {table}_header_datestamp_setspecs_idx ON {schema}.{table} (header_datestamp, header_setSpecs)",
    "CREATE INDEX IF NOT EXISTS {table}_metadata_subject_idx ON {schema}.{table} USING GIN (metadata_subject)",
    "CREATE INDEX IF NOT EXISTS {table}_created_at_idx ON {schema}.{table} (created_at)",
    "CREATE INDEX IF NOT EXISTS {table}_updated_at_idx ON {schema}.{table} (updated_at)"
};
```

### OAI-PMH Request Example

```
http://export.arxiv.org/oai2?metadataPrefix=oai_dc&set=physics&from=2024-01-01&until=2024-01-02
```

### OAI-PMH XML Response Parsing

```cpp
// Dublin Core metadata structure (from arxiv.org OAI-PMH)
struct DublinCoreMetadata {
    std::string creator;
    std::string date;
    std::string description;
    std::string identifier;
    std::vector<std::string> subject;
    std::string title;
    std::string type;
};

// OAI-PMH Header structure
struct OaiHeader {
    std::string datestamp;
    std::string identifier;
    std::vector<std::string> setSpecs;
};
```

### Rate Limiting Strategy

- 3-second delay before first request
- 3-second delay between set_specs
- 3-second delay between batches
- Configurable via environment variables

```cpp
class RateLimiter {
public:
    RateLimiter(int delay_seconds) : delay_ms_(delay_seconds * 1000) {}
    
    void wait_before_request() {
        auto now = std::chrono::steady_clock::now();
        if (last_request_.time_since_epoch().count() > 0) {
            auto elapsed = now - last_request_;
            auto remaining = delay_ms_ - std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count();
            if (remaining > 0) {
                std::this_thread::sleep_for(std::chrono::milliseconds(remaining));
            }
        }
        last_request_ = std::chrono::steady_clock::now();
    }
    
private:
    int delay_ms_;
    std::chrono::steady_clock::time_point last_request_;
};
```

### Upsert Logic (PostgreSQL)

```cpp
const char* UPSERT_QUERY = R"(
    INSERT INTO {schema}.{table} (
        header_datestamp, header_identifier, header_setSpecs,
        metadata_creator, metadata_date, metadata_description,
        metadata_identifier, metadata_subject, metadata_title, metadata_type
    ) VALUES (
        $1, $2, $3, $4, $5, $6, $7, $8, $9, $10
    )
    ON CONFLICT (header_identifier) 
    DO UPDATE SET
        header_datestamp = EXCLUDED.header_datestamp,
        header_setSpecs = EXCLUDED.header_setSpecs,
        metadata_creator = EXCLUDED.metadata_creator,
        metadata_date = EXCLUDED.metadata_date,
        metadata_description = EXCLUDED.metadata_description,
        metadata_identifier = EXCLUDED.metadata_identifier,
        metadata_subject = EXCLUDED.metadata_subject,
        metadata_title = EXCLUDED.metadata_title,
        metadata_type = EXCLUDED.metadata_type,
        updated_at = CURRENT_TIMESTAMP
)";
```

---

## Dependencies (C++)

### Required Libraries

| Library | Purpose | Package |
|---------|---------|---------|
| libpq | PostgreSQL C API | `libpq-dev` |
| libcurl | HTTP client | `libcurl4-openssl-dev` |
| libxml2 | XML parsing | `libxml2-dev` |
| nlohmann/json | JSON serialization | Header-only |
| spdlog | Logging | Header-only |
| CLI11 | Command-line interface | Header-only |
| boost | Utilities | `libboost-all-dev` |

### Docker Build (Multi-stage)

```dockerfile
# Build stage
FROM gcc:12 AS builder

# Install build dependencies
RUN apt-get update && apt-get install -y \
    cmake \
    make \
    libpq-dev \
    libcurl4-openssl-dev \
    libxml2-dev \
    git

WORKDIR /build

# Copy source code
COPY .. .

# Build
RUN mkdir -p build && cd build && \
    cmake .. -DCMAKE_BUILD_TYPE=Release && \
    make -j$(nproc)

# Final stage
FROM debian:bookworm-slim

# Install runtime dependencies
RUN apt-get update && apt-get install -y \
    libpq5 \
    libcurl4 \
    libxml2 \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /app

# Copy binary from builder
COPY --from=builder /build/build/arhida-cpp .

# Copy config
COPY --from=builder /app/config/ ./config/

CMD ["./arhida-cpp"]
```

### CMakeLists.txt Configuration

```cmake
cmake_minimum_required(VERSION 3.16)
project(arhida-cpp VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Find packages
find_package(PkgConfig REQUIRED)
pkg_check_modules(LIBPQ REQUIRED libpq)
pkg_check_modules(LIBCURL REQUIRED libcurl)
pkg_check_modules(LIBXML2 REQUIRED libxml-2.0)

# Fetch header-only libraries
include(FetchContent)

FetchContent_Declare(
    json
    GIT_REPOSITORY https://github.com/nlohmann/json.git
    GIT_TAG v3.11.2
)
FetchContent_MakeAvailable(json)

FetchContent_Declare(
    spdlog
    GIT_REPOSITORY https://github.com/gabime/spdlog.git
    GIT_TAG v1.12.0
)
FetchContent_MakeAvailable(spdlog)

FetchContent_Declare(
    cli11
    GIT_REPOSITORY https://github.com/CLIUtils/CLI11.git
    GIT_TAG v2.4.2
)
FetchContent_MakeAvailable(cli11)

# Include directories
include_directories(${LIBPQ_INCLUDE_DIRS})
include_directories(${LIBCURL_INCLUDE_DIRS})
include_directories(${LIBXML2_INCLUDE_DIRS})

# Source files
set(SOURCES
    src/main.cpp
    src/config/Config.cpp
    src/oai/OaiClient.cpp
    src/oai/Record.cpp
    src/db/Database.cpp
    src/db/QueryBuilder.cpp
    src/harvester/Harvester.cpp
    src/harvester/RateLimiter.cpp
    src/utils/Logger.cpp
    src/utils/JsonHelper.cpp
)

# Create executable
add_executable(arhida-cpp ${SOURCES})

# Link libraries
target_link_libraries(arhida-cpp
    ${LIBPQ_LIBRARIES}
    ${LIBCURL_LIBRARIES}
    ${LIBXML2_LIBRARIES}
    nlohmann_json::nlohmann_json
    spdlog::spdlog
    CLI11::CLI11
    pthread
)

# Install target
install(TARGETS arhida-cpp DESTINATION bin)
```

---

## Command-Line Interface

### Supported Modes

| Mode | Description |
|------|-------------|
| `recent` | Harvest last 2 days (default) |
| `backfill` | Harvest missing dates |
| `both` | Both recent and backfill |

### CLI Options

```cpp
#include <CLI/CLI.hpp>

int main(int argc, char** argv) {
    CLI::App app{"arXiv Academic Paper Metadata Harvester"};
    
    std::string mode = "recent";
    app.add_option("-m,--mode", mode, "Harvest mode: recent, backfill, or both")
       ->check(CLI::IsMember({"recent", "backfill", "both"}));
    
    std::string start_date, end_date;
    app.add_option("--start-date", start_date, "Start date for backfill (YYYY-MM-DD)");
    app.add_option("--end-date", end_date, "End date for backfill (YYYY-MM-DD)");
    
    std::vector<std::string> set_specs;
    app.add_option("--set-specs", set_specs, "Set specifications to process")
       ->default_val({"physics", "math", "cs", "q-bio", "q-fin", "stat", "eess", "econ"});
    
    CLI11_PARSE(app, argc, argv);
    
    return 0;
}
```

### Usage Examples

```bash
# Recent harvest (default)
./arhida-cpp --mode recent

# Backfill missing dates
./arhida-cpp --mode backfill

# Backfill specific date range
./arhida-cpp --mode backfill --start-date 2007-01-01 --end-date 2009-12-31

# Both recent and backfill
./arhida-cpp --mode both --start-date 2020-01-01

# Custom set specifications
./arhida-cpp --mode backfill --set-specs physics math cs
```

---

## Configuration

### Environment Variables

| Variable | Default | Description |
|----------|---------|-------------|
| `POSTGRES_HOST` | `localhost` | PostgreSQL host |
| `POSTGRES_DB` | - | Database name |
| `POSTGRES_USER` | - | Database user |
| `POSTGRES_PASSWORD` | - | Database password |
| `POSTGRES_PORT` | `5432` | PostgreSQL port |
| `POSTGRES_SCHEMA` | - | Schema name |
| `POSTGRES_TABLE` | - | Table name |
| `ARXIV_RATE_LIMIT_DELAY` | `3` | Delay between requests |
| `ARXIV_BATCH_SIZE` | `2000` | Records per batch |
| `ARXIV_MAX_RETRIES` | `3` | Maximum retries |
| `ARXIV_RETRY_AFTER` | `5` | Retry delay (seconds) |

### Config Class Implementation

```cpp
class Config {
public:
    static Config& instance() {
        static Config config;
        return config;
    }
    
    void load() {
        // PostgreSQL
        host_ = get_env("POSTGRES_HOST", "localhost");
        database_ = get_env("POSTGRES_DB", "");
        user_ = get_env("POSTGRES_USER", "");
        password_ = get_env("POSTGRES_PASSWORD", "");
        port_ = std::stoi(get_env("POSTGRES_PORT", "5432"));
        schema_ = get_env("POSTGRES_SCHEMA", "");
        table_ = get_env("POSTGRES_TABLE", "");
        
        // arXiv
        rate_limit_delay_ = std::stoi(get_env("ARXIV_RATE_LIMIT_DELAY", "3"));
        batch_size_ = std::stoi(get_env("ARXIV_BATCH_SIZE", "2000"));
        max_retries_ = std::stoi(get_env("ARXIV_MAX_RETRIES", "3"));
        retry_after_ = std::stoi(get_env("ARXIV_RETRY_AFTER", "5"));
    }
    
    // Getters...
    
private:
    Config() = default;
    std::string get_env(const char* key, const char* default_value);
};
```

---

## Migration Considerations

### Pros of C++ Migration

| Benefit | Description |
|---------|-------------|
| **Performance** | 5-10x faster execution due to native compilation |
| **Memory Efficiency** | Lower memory footprint (~50MB vs ~200MB) |
| **Binary Distribution** | No interpreter or runtime needed |
| **Type Safety** | Compile-time type checking reduces runtime errors |
| **Single Executable** | Easier deployment and distribution |

### Cons/Challenges

| Challenge | Impact | Mitigation |
|-----------|--------|------------|
| Longer Development | ~6 weeks vs 1 week in Python | Consider partial migration |
| Complex Build System | CMake, Docker multi-stage builds | Provide pre-built Docker images |
| Verbose Code | More lines for XML/JSON handling | Use helper libraries |
| Debugging Complexity | Less introspection than Python | Use IDE debuggers, spdlog |

### Alternative Approach: Hybrid

Consider a **hybrid approach** for easier migration:

1. **Keep Python** for main application logic
2. **Use C++** for performance-critical components:
   - OAI-PMH XML parsing
   - Batch processing
   - Rate limiting
3. **Use PyBind11** to create Python bindings

```cpp
#include <pybind11/pybind11.h>

namespace py = pybind11;

PYBIND11_MODULE(arhida_core, m) {
    m.def("harvest", &harvest_records, "Harvest OAI-PMH records");
    m.def("parse_records", &parse_records, "Parse XML records");
    m.def("process_batch", &process_batch, "Process batch of records");
}
```

```python
# Python wrapper using C++ extension
from arhida_core import harvest, parse_records, process_batch

# Use C++ for heavy lifting, Python for orchestration
def main():
    records = harvest(set_spec, from_date, until_date)
    parsed = parse_records(records)
    process_batch(parsed)
```

---

## Implementation Checklist

- [x] **Phase 1: Setup**
  - [x] Create CMakeLists.txt
  - [x] Set up Docker build
  - [x] Configure CI/CD pipeline

- [x] **Phase 2: Core**
  - [x] Config class
  - [x] Logging system
  - [x] Database connection

- [x] **Phase 3: OAI Client**
  - [x] HTTP client
  - [x] XML parser
  - [x] Rate limiter
  - [x] Retry logic

- [x] **Phase 4: Harvester**
  - [x] Record extraction
  - [x] Batch processing
  - [x] Upsert operations

- [x] **Phase 5: CLI**
  - [x] Command-line parsing
  - [x] Backfill logic
  - [x] Statistics

- [ ] **Phase 6: Testing**
  - [ ] Unit tests
  - [ ] Integration tests
  - [ ] Performance benchmarks

---

## GitHub Actions CI/CD

### Build Workflow

```yaml
# .github/workflows/build.yml
name: Build and Push Docker Image

on:
  push:
    branches: [main, develop]
    tags:
      - 'v*'
  pull_request:
    branches: [main]
  workflow_dispatch:

env:
  REGISTRY: ghcr.io
  IMAGE_NAME: ${{ github.repository }}

jobs:
  build:
    runs-on: ubuntu-latest
    permissions:
      contents: read
      packages: write
    
    steps:
      - name: Checkout repository
        uses: actions/checkout@v4
      
      - name: Set up Docker Buildx
        uses: docker/setup-buildx-action@v3
      
      - name: Log in to Container Registry
        if: github.event_name != 'pull_request'
        uses: docker/login-action@v3
        with:
          registry: ${{ env.REGISTRY }}
          username: ${{ github.actor }}
          password: ${{ secrets.GITHUB_TOKEN }}
      
      - name: Extract metadata for Docker
        id: meta
        uses: docker/metadata-action@v5
        with:
          images: ${{ env.REGISTRY }}/${{ env.IMAGE_NAME }}
          tags: |
            type=ref,event=branch
            type=ref,event=pr
            type=semver,pattern={{version}}
            type=sha
      
      - name: Build and push Docker image
        uses: docker/build-push-action@v5
        with:
          context: .
          push: ${{ github.event_name != 'pull_request' }}
          tags: ${{ steps.meta.outputs.tags }}
          labels: ${{ steps.meta.outputs.labels }}
          build-args: |
            BUILD_DATE=${{ steps.meta.outputs.created }}
            VERSION=${{ github.ref_name }}
            REVISION=${{ github.sha }}
          cache-from: type=gha
          cache-to: type=gha,mode=max
      
      - name: Build multi-platform image
        if: github.event_name != 'pull_request'
        run: |
          # Build for multiple platforms
          docker buildx build \
            --platform linux/amd64,linux/arm64 \
            --tag ${{ env.REGISTRY }}/${{ env.IMAGE_NAME }}:${{ github.ref_name }} \
            --push .

  test:
    runs-on: ubuntu-latest
    needs: build
    steps:
      - name: Pull image
        run: docker pull ${{ env.REGISTRY }}/${{ env.IMAGE_NAME }}:latest
      
      - name: Run tests
        run: |
          docker run --rm \
            -e POSTGRES_HOST=test-db \
            -e POSTGRES_DB=test \
            -e POSTGRES_USER=test \
            -e POSTGRES_PASSWORD=test \
            ${{ env.REGISTRY }}/${{ env.IMAGE_NAME }}:latest \
            --mode recent

  release:
    runs-on: ubuntu-latest
    needs: test
    if: startsWith(github.ref, 'refs/tags/v')
    permissions:
      contents: write
    steps:
      - name: Download artifacts
        uses: actions/download-artifact@v4
      
      - name: Create Release
        uses: softprops/action-gh-release@v1
        with:
          files: |
            build/*.tar.gz
            build/*.deb
          generate_release_notes: true
```

### Multi-Stage Build Dockerfile

```dockerfile
# Dockerfile
ARG BUILD_DATE
ARG VERSION=main
ARG REVISION=unknown

FROM gcc:13 AS builder

# Install build dependencies
RUN apt-get update && apt-get install -y \
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
RUN mkdir -p build && cd build

# Configure with CMake
RUN cmake .. \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_INSTALL_PREFIX=/usr/local \
    -DBUILD_TESTS=ON

# Build
RUN make -j$(nproc)
RUN make install

# Final runtime image
FROM debian:bookworm-slim

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

# Copy configuration
COPY --from=builder /app/config/ ./config/

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
```

---

## Podman-Compose Local Development

### Podman-Compose Configuration

```yaml
# compose.yaml
name: arhida-cpp

services:
  app:
    image: ghcr.io/chasekb/arhida-cpp:latest
    container_name: arhida-cpp
    build:
      context: .
      dockerfile: Dockerfile
    environment:
      # Database (using external PostgreSQL or local)
      - POSTGRES_HOST=db-postgres
      - POSTGRES_DB=${POSTGRES_DB}
      - POSTGRES_USER=${POSTGRES_USER}
      - POSTGRES_PASSWORD=${POSTGRES_PASSWORD}
      - POSTGRES_PORT=5432
      - POSTGRES_SCHEMA=${POSTGRES_SCHEMA}
      - POSTGRES_TABLE=${POSTGRES_TABLE}
      
      # Docker secrets (alternative)
      - DOCKER_POSTGRES_HOST=db-postgres
      - DOCKER_POSTGRES_USER_FILE=/run/secrets/postgres-u
      - DOCKER_POSTGRES_PASSWORD_FILE=/run/secrets/postgres-p
      
      # Application settings
      - ARXIV_RATE_LIMIT_DELAY=3
      - ARXIV_BATCH_SIZE=2000
      - ARXIV_MAX_RETRIES=3
      - ARXIV_RETRY_AFTER=5
    volumes:
      - ./logs:/app/logs
      - ./config:/app/config:ro
    networks:
      - arhida-net
    secrets:
      - postgres-u
      - postgres-p
    restart: unless-stopped
    
    # Health check
    healthcheck:
      test: ["CMD", "./arhida-cpp", "--help"]
      interval: 30s
      timeout: 10s
      retries: 3
      start_period: 10s

  # Optional: Local PostgreSQL for testing
  db-local:
    image: postgres:16-alpine
    container_name: arhida-postgres
    environment:
      - POSTGRES_DB=${POSTGRES_DB}
      - POSTGRES_USER=${POSTGRES_USER}
      - POSTGRES_PASSWORD=${POSTGRES_PASSWORD}
    volumes:
      - postgres-data:/var/lib/postgresql/data
    networks:
      - arhida-net
    restart: unless-stopped
    healthcheck:
      test: ["CMD-SHELL", "pg_isready -U ${POSTGRES_USER}"]
      interval: 10s
      timeout: 5s
      retries: 5

  # Optional: Adminer for database management
  adminer:
    image: adminer:latest
    container_name: arhida-adminer
    ports:
      - "8080:8080"
    networks:
      - arhida-net
    depends_on:
      - db-local
    restart: unless-stopped

secrets:
  postgres-u:
    file: db/postgres-u.txt
  postgres-p:
    file: db/postgres-p.txt

networks:
  arhida-net:
    driver: bridge

volumes:
  postgres-data:
```

### Environment Configuration

```bash
# .env
# Database configuration
POSTGRES_DB=arhida
POSTGRES_USER=arhida_user
POSTGRES_PASSWORD=your_secure_password_here
POSTGRES_SCHEMA=arxiv
POSTGRES_TABLE=metadata
POSTGRES_HOST=db-local
POSTGRES_PORT=5432

# Application settings
ARXIV_RATE_LIMIT_DELAY=3
ARXIV_BATCH_SIZE=2000
ARXIV_MAX_RETRIES=3
ARXIV_RETRY_AFTER=5

# Docker secrets paths
DOCKER_POSTGRES_HOST=db-local
DOCKER_POSTGRES_USER_FILE=/run/secrets/postgres-u
DOCKER_POSTGRES_PASSWORD_FILE=/run/secrets/postgres-p
```

---

## Usage Examples

### Local Development with Remote Build

```bash
# 1. Pull the latest image from GitHub Container Registry
podman pull ghcr.io/chasekb/arhida-cpp:latest

# 2. Create environment file
cat > .env << 'EOF'
POSTGRES_DB=arhida
POSTGRES_USER=arhida_user
POSTGRES_PASSWORD=secure_password
POSTGRES_SCHEMA=arxiv
POSTGRES_TABLE=metadata
POSTGRES_HOST=db-local
ARXIV_RATE_LIMIT_DELAY=3
ARXIV_BATCH_SIZE=2000
EOF

# 3. Create database credentials files
echo "arhida_user" > db/postgres-u.txt
echo "secure_password" > db/postgres-p.txt

# 4. Start the services with podman-compose
podman-compose up -d

# 5. View logs
podman-compose logs -f app

# 6. Run a recent harvest
podman-compose exec app ./arhida-cpp --mode recent

# 7. Run a backfill
podman-compose exec app ./arhida-cpp --mode backfill --start-date 2020-01-01

# 8. Stop services
podman-compose down
```

### Running with Specific Versions

```bash
# Pull specific version
podman pull ghcr.io/chasekb/arhida-cpp:v1.0.0

# Run with specific version tag
podman run --rm \
  -e POSTGRES_HOST=db-postgres \
  -e POSTGRES_DB=arhida \
  -e POSTGRES_USER=arhida_user \
  -e POSTGRES_PASSWORD=password \
  -e POSTGRES_SCHEMA=arxiv \
  -e POSTGRES_TABLE=metadata \
  ghcr.io/chasekb/arhida-cpp:v1.0.0 \
  --mode recent --set-specs physics math
```

### Development Build with Local Code

```bash
# Build locally for testing
podman build -t arhida-cpp:local .

# Run with local build
podman run --rm \
  -e POSTGRES_HOST=db-local \
  -e POSTGRES_DB=arhida \
  -e POSTGRES_USER=arhida_user \
  -e POSTGRES_PASSWORD=password \
  -e POSTGRES_SCHEMA=arxiv \
  -e POSTGRES_TABLE=metadata \
  -v $(pwd)/logs:/app/logs \
  arhida-cpp:local \
  --mode backfill --start-date 2024-01-01 --end-date 2024-01-31
```

### CI/CD Pipeline Usage

```bash
# In CI/CD pipeline (GitHub Actions example)
podman build \
  --build-arg BUILD_DATE=$(date -u +"%Y-%m-%dT%H:%M:%SZ") \
  --build-arg VERSION=${{ github.ref_name }} \
  --build-arg REVISION=${{ github.sha }} \
  -t ${{ env.REGISTRY }}/${{ env.IMAGE_NAME }}:${{ github.sha }} \
  .

podman push ${{ env.REGISTRY }}/${{ env.IMAGE_NAME }}:${{ github.sha }}
```

### Podman Systemd Service

```bash
# Generate systemd unit file for production
podman generate systemd --name arhida-cpp --files

# Or use quadlet for modern podman
cat > /etc/containers/systemd/arhida-cpp.container << 'EOF'
[Container]
Image=ghcr.io/chasekb/arhida-cpp:latest
ContainerName=arhida-cpp
Environment=POSTGRES_HOST=db-postgres
Environment=POSTGRES_DB=arhida
Environment=POSTGRES_SCHEMA=arxiv
Environment=POSTGRES_TABLE=metadata
Volume=/opt/arhida/logs:/app/logs
Volume=/opt/arhida/config:/app/config:ro
Secret=db-user.conf,type=file,uid=0,gid=0,mode=0600,destination=/run/secrets/postgres-u
Secret=db-pass.conf,type=file,uid=0,gid=0,mode=0600,destination=/run/secrets/postgres-p
Network=arhida-net
Restart=unless-stopped

[Service]
# Optional: Scheduled harvests via timer
ExecStartPre=/usr/bin/podman wait --condition=running arhida-cpp

[Install]
WantedBy=multi-user.target
EOF

# Reload and enable
systemctl daemon-reload
systemctl enable --now arhida-cpp
```

---

## References

- [arXiv OAI-PMH Interface](http://export.arxiv.org/oai2)
- [libpq Documentation](https://www.postgresql.org/docs/current/libpq.html)
- [libcurl Tutorial](https://curl.se/libcurl/c/libcurl-tutorial.html)
- [libxml2 Tutorial](https://xmlsoft.org/tutorial/)
- [nlohmann/json Documentation](https://json.nlohmann.me/)
- [spdlog Documentation](https://spdlog.docsforge.com/)
- [CLI11 Documentation](https://cliutils.github.io/CLI11/)
- [GitHub Actions Documentation](https://docs.github.com/en/actions)
- [Podman Documentation](https://docs.podman.io/)
- [Podman-Compose Documentation](https://github.com/containers/podman-compose)

---

*Document Version: 1.1*  
*Last Updated: 2026-02-22*
