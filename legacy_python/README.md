# arXiv Academic Paper Metadata Harvester

This project harvests academic paper metadata from arXiv.org's OAI-PMH interface, complying with their API rate limits and terms of use. The harvested data has been migrated from MongoDB to PostgreSQL for better performance and scalability.

## What Data is Retrieved

The application retrieves comprehensive metadata for academic papers from arXiv, including:

- **Paper Titles**: Full titles of research papers
- **Authors**: Complete author information and affiliations
- **Abstracts**: Paper descriptions and summaries
- **Publication Dates**: When papers were published or updated
- **Subject Categories**: arXiv classification categories (physics, math, computer science, etc.)
- **Unique Identifiers**: arXiv IDs and DOI references
- **Metadata Timestamps**: When records were last updated

## Rate Limiting Requirements

According to arxiv.org's Terms of Use:
- **Maximum 1 request every 3 seconds**
- **Single connection at a time**
- **Maximum 30,000 results per query**
- **Results retrieved in slices of up to 2,000 at a time**

## Configuration

The application is configured through environment variables defined in the `.env` file. Copy `.env.example` to `.env` and update the values for your environment. The following variables control the application's behavior:

### Database Configuration
| Variable | Default | Description |
|----------|---------|-------------|
| `POSTGRES_HOST` | `localhost` | PostgreSQL host (local development) |
| `POSTGRES_DB` | `your_database_name` | PostgreSQL database name |
| `POSTGRES_USER` | `your_username` | PostgreSQL username |
| `POSTGRES_PASSWORD` | `your_password` | PostgreSQL password |
| `POSTGRES_PORT` | `5432` | PostgreSQL port |
| `POSTGRES_SCHEMA` | `your_schema` | PostgreSQL schema name |
| `POSTGRES_TABLE` | `your_table` | PostgreSQL table name |

### Docker Environment Configuration
| Variable | Default | Description |
|----------|---------|-------------|
| `DOCKER_POSTGRES_HOST` | `db-postgres` | PostgreSQL host (Docker environment) |
| `DOCKER_POSTGRES_USER_FILE` | `db/postgres-u.txt` | Path to PostgreSQL username file |
| `DOCKER_POSTGRES_PASSWORD_FILE` | `db/postgres-p.txt` | Path to PostgreSQL password file |

### arXiv API Configuration
| Variable | Default | Description |
|----------|---------|-------------|
| `ARXIV_RATE_LIMIT_DELAY` | `3` | Delay between requests in seconds |
| `ARXIV_BATCH_SIZE` | `2000` | Maximum records per batch |
| `ARXIV_MAX_RETRIES` | `3` | Maximum retries for failed requests |
| `ARXIV_RETRY_AFTER` | `5` | Retry delay after failed requests |

### Backfill Configuration
| Variable | Default | Description |
|----------|---------|-------------|
| `BACKFILL_CHUNK_SIZE` | `7` | Number of days to process in each backfill chunk |
| `BACKFILL_START_DATE` | `2007-01-01` | Default start date for backfill operations |

## Implementation Details

### Rate Limiting
- 3-second delay before the first request
- 3-second delay between different set specifications
- 3-second delay between batches when processing large datasets

### Batch Processing
- Records are processed in batches of up to 2,000 records
- Each batch is processed with proper error handling
- Progress is logged every 100 records within a batch

### Error Handling
- Retry logic for HTTP 503 (Service Unavailable) and 429 (Rate Limit Exceeded)
- Graceful handling of connection errors
- Comprehensive logging for monitoring and debugging

### Database
- Records are stored in PostgreSQL in the configured schema (default: `your_schema`)
- Uses the configured database on the appropriate network
- Connects to the configured PostgreSQL service
- JSONB fields for complex metadata structures with GIN indexes
- Upsert operations prevent duplicate records

### Backfill Functionality
- Automatically detects missing dates in the database
- Backfills missing data for any date range
- Processes data in configurable chunks (default: 7 days)
- Supports both recent harvest and historical backfill
- Command-line interface for flexible operation

### Logging
- Structured logging with timestamps
- Progress tracking for each set specification
- Summary statistics upon completion

## Usage

The script supports multiple modes of operation with command-line arguments:

### Recent Harvest (Default)
Harvest the last 2 days of data:
```bash
python arhida.py --mode recent
```

### Backfill Missing Dates
Backfill all missing dates from 2007 to present:
```bash
python arhida.py --mode backfill
```

Backfill specific date range:
```bash
python arhida.py --mode backfill --start-date 2007-01-01 --end-date 2009-12-31
```

### Both Recent and Backfill
```bash
python arhida.py --mode both --start-date 2020-01-01
```

### Custom Set Specifications
```bash
python arhida.py --mode backfill --set-specs physics math cs
```

### Docker Usage
```bash
# Recent harvest
docker-compose run server python arhida.py --mode recent

# Backfill with custom dates
docker-compose run server python arhida.py --mode backfill --start-date 2020-01-01 --end-date 2020-12-31
```

### Environment Variables
For custom configuration, set environment variables:

```bash
export ARXIV_RATE_LIMIT_DELAY=3
export ARXIV_BATCH_SIZE=2000
export BACKFILL_CHUNK_SIZE=7
python arhida.py --mode backfill
```
