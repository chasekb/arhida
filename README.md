# arXiv Academic Paper Metadata Harvester (C++)

A high-performance C++ implementation of the arXiv Academic Paper Metadata Harvester. This application harvests academic paper metadata from arXiv.org's OAI-PMH interface and stores it in PostgreSQL.

## Features

- **OAI-PMH Client**: Harvests metadata from arXiv.org's OAI-PMH interface
- **PostgreSQL Storage**: Stores data in PostgreSQL with JSONB fields
- **Rate Limiting**: Implements 3-second delays to comply with arXiv.org's terms of use
- **Batch Processing**: Processes records in batches of up to 2,000 records
- **Backfill Support**: Fill in missing dates from the database
- **CLI Interface**: Command-line interface with multiple modes

## Requirements

- C++20 compatible compiler (GCC 13+)
- CMake 3.16+
- PostgreSQL libpq
- libcurl
- libxml2

## Building

### Local Build

```bash
# Create build directory
mkdir build && cd build

# Configure
cmake .. -DCMAKE_BUILD_TYPE=Release

# Build
make -j$(nproc)
```

### Docker Build

```bash
# Build the Docker image
docker build -t arhida:latest .

# Or use docker-compose with the build configuration file
docker-compose -f docker-compose.yaml -f docker-compose.build.yaml build
docker-compose -f docker-compose.yaml -f docker-compose.build.yaml up
```

## Configuration

Configure the application through environment variables:

| Variable | Default | Description |
|----------|---------|-------------|
| `POSTGRES_HOST` | `localhost` | PostgreSQL host |
| `POSTGRES_DB` | - | Database name |
| `POSTGRES_USER` | - | Database user |
| `POSTGRES_PASSWORD` | - | Database password |
| `POSTGRES_PORT` | `5432` | PostgreSQL port |
| `POSTGRES_SCHEMA` | `arxiv` | Schema name |
| `POSTGRES_TABLE` | `metadata` | Table name |
| `ARXIV_RATE_LIMIT_DELAY` | `3` | Delay between requests (seconds) |
| `ARXIV_BATCH_SIZE` | `2000` | Records per batch |
| `ARXIV_MAX_RETRIES` | `3` | Maximum retries |
| `ARXIV_RETRY_AFTER` | `5` | Retry delay (seconds) |

## Usage

### Command-Line Options

```bash
# Recent harvest (last 2 days)
./arhida-cpp --mode recent

# Backfill missing dates
./arhida-cpp --mode backfill

# Both recent and backfill
./arhida-cpp --mode both --start-date 2020-01-01

# Custom set specifications
./arhida-cpp --mode backfill --set-specs physics math cs
```

### Docker Usage

```bash
# Pull the pre-built image and start the application
docker-compose pull
docker-compose up -d

# Run a specific command using docker-compose
docker-compose run app ./arhida-cpp --mode recent
```

## Project Structure

```
arhida/
├── CMakeLists.txt           # Build configuration
├── Dockerfile              # Docker build
├── docker-compose.yaml     # Container orchestration
├── docker-compose.build.yaml # Local build configuration overlay
├── include/               # Header files
│   ├── config/
│   ├── db/
│   ├── harvester/
│   ├── oai/
│   └── utils/
├── src/                   # Source files
│   ├── main.cpp
│   ├── config/
│   ├── db/
│   ├── harvester/
│   ├── oai/
│   └── utils/
└── legacy_python/         # Python reference implementation
```

## Database Schema

The application creates the following schema in PostgreSQL:

```sql
CREATE TABLE IF NOT EXISTS arxiv.metadata (
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
);
```

## Rate Limiting

This application complies with arXiv.org's terms of use:

- Maximum 1 request every 3 seconds
- Single connection at a time
- Maximum 30,000 results per query

## License

MIT License

## Author

Bernard Chase
