from datetime import date, datetime, timedelta
from time import sleep
import platform
import os
import logging
import json
import sys

from dotenv import load_dotenv

# Load environment variables from .env file
load_dotenv()

from sickle import Sickle
from sickle.oaiexceptions import NoRecordsMatch
import psycopg2
from psycopg2.extras import RealDictCursor
from psycopg2.errors import UniqueViolation
from requests.exceptions import ConnectionError

from icecream import ic

# Configure logging
logging.basicConfig(level=logging.INFO, format='%(asctime)s - %(levelname)s - %(message)s')
logger = logging.getLogger(__name__)

# Database configuration
DATABASE_NAME = os.getenv('POSTGRES_DB')
SCHEMA_NAME = os.getenv('POSTGRES_SCHEMA')
TABLE_NAME = os.getenv('POSTGRES_TABLE')


def get_records(table_name, client, metadata_prefix="oai_dc", set_spec=None, from_date=None, until_date=None):
    # Rate limiting configuration - arxiv.org requires max 1 request every 3 seconds
    RATE_LIMIT_DELAY = int(os.getenv('ARXIV_RATE_LIMIT_DELAY', '3'))  # 3 seconds default
    BATCH_SIZE = int(os.getenv('ARXIV_BATCH_SIZE', '2000'))  # Max 2000 records per batch
    MAX_RETRIES = int(os.getenv('ARXIV_MAX_RETRIES', '3'))
    
    conn = get_postgres_connection()
    cursor = conn.cursor(cursor_factory=RealDictCursor)

    logger.info(f"Starting harvest for set_spec: {set_spec}, from: {from_date}, until: {until_date}")
    
    # Harvest records using sickle with rate limiting
    total_processed = 0
    batch_count = 0
    
    try:
        # Add delay before first request to comply with rate limits
        logger.info(f"Waiting {RATE_LIMIT_DELAY} seconds before first request to comply with arxiv.org rate limits")
        sleep(RATE_LIMIT_DELAY)
        
        records = client.ListRecords(
            **{
                'metadataPrefix': metadata_prefix, 
                'set': set_spec, 
                'from': from_date, 
                'until': until_date,
                'ignore_deleted': True
            }
        )

    except NoRecordsMatch as e:
        logger.warning(f'No records found for {set_spec} from {from_date} to {until_date}: {e}')
        return 0
    except Exception as e:
        logger.error(f'Error fetching records for {set_spec}: {e}')
        return -1

    if records:
        # Process records in batches to comply with arxiv.org limits
        try:
            batch = []
            for record in records:
                header = record.header
                metadata = record.metadata
                dict_header = dict(header)
                dict_metadata = dict(metadata)

                extracted = {
                        'header_datestamp': dict_header['datestamp'],
                        'header_identifier': dict_header['identifier'],
                        'header_setSpecs': dict_header['setSpecs'],
                        'metadata_creator': dict_metadata['creator'],
                        'metadata_date': dict_metadata['date'],
                        'metadata_description': " ".join(dict_metadata['description'][0].split()),
                        'metadata_identifier': dict_metadata['identifier'],
                        'metadata_subject': dict_metadata['subject'],
                        'metadata_title': " ".join(dict_metadata['title'][0].split()),
                        'metadata_type': dict_metadata['type'],
                    }
                
                batch.append(extracted)
                
                # Process batch when it reaches the size limit
                if len(batch) >= BATCH_SIZE:
                    batch_count += 1
                    logger.info(f"Processing batch {batch_count} with {len(batch)} records")
                    processed_in_batch = process_batch(cursor, conn, table_name, batch, set_spec)
                    total_processed += processed_in_batch
                    batch = []
                    
                    # Rate limiting: wait between batches
                    logger.info(f"Rate limiting: waiting {RATE_LIMIT_DELAY} seconds before next batch")
                    sleep(RATE_LIMIT_DELAY)
            
            # Process remaining records in the last batch
            if batch:
                batch_count += 1
                logger.info(f"Processing final batch {batch_count} with {len(batch)} records")
                processed_in_batch = process_batch(cursor, conn, table_name, batch, set_spec)
                total_processed += processed_in_batch
                
        except ConnectionError as f:
            logger.error(f'Connection error during iteration: {f}')
            logger.error(f'Response text: {f.response.text if hasattr(f, "response") else "No response text"}')
            sleep(RATE_LIMIT_DELAY)
        except Exception as h:
            logger.error(f'Unexpected error during iteration: {h}')
    
    # Close database connection
    cursor.close()
    conn.close()
    
    logger.info(f"Completed harvest for {set_spec}: {total_processed} records processed in {batch_count} batches")
    return total_processed


def process_batch(cursor, conn, table_name, batch, set_spec):
    """Process a batch of records and insert them into PostgreSQL"""
    processed_count = 0
    
    for extracted in batch:
        try:
            # Safely convert data to JSON for PostgreSQL storage
            def safe_json_serialize(data):
                """Safely serialize data to JSON, handling various input types"""
                if data is None:
                    return None
                elif isinstance(data, (list, dict)):
                    return json.dumps(data)
                elif isinstance(data, str):
                    # If it's already a string, try to parse it as JSON first
                    try:
                        parsed = json.loads(data)
                        return json.dumps(parsed)
                    except (json.JSONDecodeError, TypeError):
                        # If it's not valid JSON, wrap it as a string
                        return json.dumps(data)
                else:
                    return json.dumps(data)
            
            header_set_specs = safe_json_serialize(extracted['header_setSpecs'])
            metadata_creator = safe_json_serialize(extracted['metadata_creator'])
            metadata_date = safe_json_serialize(extracted['metadata_date'])
            metadata_identifier = safe_json_serialize(extracted['metadata_identifier'])
            metadata_subject = safe_json_serialize(extracted['metadata_subject'])
            metadata_title = safe_json_serialize(extracted['metadata_title'])
            
            # Use UPSERT (INSERT ... ON CONFLICT ... DO UPDATE)
            upsert_query = f"""
                INSERT INTO {SCHEMA_NAME}.{table_name} (
                    header_datestamp, header_identifier, header_setSpecs,
                    metadata_creator, metadata_date, metadata_description,
                    metadata_identifier, metadata_subject, metadata_title, metadata_type
                ) VALUES (
                    %s, %s, %s, %s, %s, %s, %s, %s, %s, %s
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
            """
            
            # Prepare data for insertion
            insert_data = (
                extracted['header_datestamp'],
                extracted['header_identifier'],
                header_set_specs,
                metadata_creator,
                metadata_date,
                extracted['metadata_description'],
                metadata_identifier,
                metadata_subject,
                metadata_title,
                extracted['metadata_type']
            )
            
            cursor.execute(upsert_query, insert_data)
            
            processed_count += 1
            
            if processed_count % 100 == 0:  # Log progress every 100 records
                logger.info(f"Processed {processed_count} records in current batch for {set_spec}")
                
        except Exception as e:
            logger.error(f"Error processing record {extracted.get('header_identifier', 'unknown')}: {e}")
            logger.debug(f"Record data: {extracted}")
            continue
    
    # Commit the batch
    conn.commit()
    logger.info(f"Batch processing complete: {processed_count} records inserted for {set_spec}")
    return processed_count


def reset_arxiv(table_name):
    """Create PostgreSQL schema, table and indexes for arXiv data"""
    conn = get_postgres_connection()
    cursor = conn.cursor()
    
    try:
        # Create schema
        cursor.execute(f"CREATE SCHEMA IF NOT EXISTS {SCHEMA_NAME}")
        logger.info(f"Created {SCHEMA_NAME} schema")

        # Create table in schema
        create_table_query = f"""
            CREATE TABLE IF NOT EXISTS {SCHEMA_NAME}.{table_name} (
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
        """
        cursor.execute(create_table_query)
        logger.info(f"Created table {SCHEMA_NAME}.{table_name}")

        # Create indexes
        indexes = [
            f"CREATE UNIQUE INDEX IF NOT EXISTS {table_name}_header_identifier_idx ON {SCHEMA_NAME}.{table_name} (header_identifier)",
            f"CREATE INDEX IF NOT EXISTS {table_name}_header_datestamp_idx ON {SCHEMA_NAME}.{table_name} (header_datestamp)",
            f"CREATE INDEX IF NOT EXISTS {table_name}_header_setspecs_idx ON {SCHEMA_NAME}.{table_name} USING GIN (header_setSpecs)",
            f"CREATE INDEX IF NOT EXISTS {table_name}_header_datestamp_setspecs_idx ON {SCHEMA_NAME}.{table_name} (header_datestamp, header_setSpecs)",
            f"CREATE INDEX IF NOT EXISTS {table_name}_metadata_subject_idx ON {SCHEMA_NAME}.{table_name} USING GIN (metadata_subject)",
            f"CREATE INDEX IF NOT EXISTS {table_name}_created_at_idx ON {SCHEMA_NAME}.{table_name} (created_at)",
            f"CREATE INDEX IF NOT EXISTS {table_name}_updated_at_idx ON {SCHEMA_NAME}.{table_name} (updated_at)"
        ]
        
        for index_query in indexes:
            cursor.execute(index_query)
            
        conn.commit()
        logger.info(f"Created indexes for table {SCHEMA_NAME}.{table_name}")

    except Exception as e:
        logger.error(f"Error creating schema/table/indexes for {SCHEMA_NAME}.{table_name}: {e}")
        conn.rollback()
    finally:
        cursor.close()
        conn.close()


def get_postgres_connection():
    """Get PostgreSQL database connection"""
    if platform.system() == 'Darwin':
        # For local development on macOS
        connection_params = {
            'host': os.getenv('POSTGRES_HOST'),
            'database': DATABASE_NAME,
            'user': os.getenv('POSTGRES_USER'),
            'password': os.getenv('POSTGRES_PASSWORD'),
            'port': int(os.getenv('POSTGRES_PORT'))
        }
    elif platform.system() == 'Linux':
        # For Docker/Linux environment - use existing db-postgres service
        auth_usr_file = os.getenv('DOCKER_POSTGRES_USER_FILE')
        auth_pwd_file = os.getenv('DOCKER_POSTGRES_PASSWORD_FILE')

        with open(auth_usr_file) as f:
            usr = f.read().strip()

        with open(auth_pwd_file) as g:
            pwd = g.read().strip()

        connection_params = {
            'host': os.getenv('DOCKER_POSTGRES_HOST'),
            'database': DATABASE_NAME,
            'user': usr,
            'password': pwd,
            'port': int(os.getenv('POSTGRES_PORT'))
        }
    else:
        raise Exception(f"Unsupported platform: {platform.system()}")

    try:
        conn = psycopg2.connect(**connection_params)
        return conn
    except Exception as e:
        logger.error(f"Failed to connect to PostgreSQL: {e}")
        raise


def get_date_list(date_start, date_end):
    ds = date.fromisoformat(date_start)
    de = date.fromisoformat(date_end)

    return([(ds + timedelta(days=x)).isoformat() for x in range((de - ds).days)])


def get_missing_dates(start_date, end_date, set_spec=None):
    """Get missing dates from the database for a given date range and set_spec"""
    conn = get_postgres_connection()
    cursor = conn.cursor()
    
    try:
        # Get all dates in the range
        all_dates = get_date_list(start_date, end_date)
        
        # Query database for existing dates
        if set_spec:
            query = f"""
                SELECT DISTINCT DATE(header_datestamp) as harvest_date
                FROM {SCHEMA_NAME}.{TABLE_NAME}
                WHERE header_setSpecs::text LIKE %s
                AND DATE(header_datestamp) BETWEEN %s AND %s
                ORDER BY harvest_date
            """
            cursor.execute(query, (f'%{set_spec}%', start_date, end_date))
        else:
            query = f"""
                SELECT DISTINCT DATE(header_datestamp) as harvest_date
                FROM {SCHEMA_NAME}.{TABLE_NAME}
                WHERE DATE(header_datestamp) BETWEEN %s AND %s
                ORDER BY harvest_date
            """
            cursor.execute(query, (start_date, end_date))
        
        existing_dates = [row[0].isoformat() for row in cursor.fetchall()]
        
        # Find missing dates
        missing_dates = [d for d in all_dates if d not in existing_dates]
        
        logger.info(f"Date range: {start_date} to {end_date}")
        logger.info(f"Total dates in range: {len(all_dates)}")
        logger.info(f"Existing dates: {len(existing_dates)}")
        logger.info(f"Missing dates: {len(missing_dates)}")
        
        return missing_dates
        
    except Exception as e:
        logger.error(f"Error getting missing dates: {e}")
        return []
    finally:
        cursor.close()
        conn.close()


def get_earliest_available_date():
    """Get the earliest date available on arXiv OAI-PMH (approximate)"""
    # arXiv OAI-PMH interface typically starts around 2007-2008
    return "2007-01-01"


def get_latest_available_date():
    """Get the latest date available on arXiv (today)"""
    return date.today().isoformat()


def backfill_missing_dates(start_date=None, end_date=None, set_specs=None, sickle_client=None):
    """Backfill missing dates for all or specific set specifications"""
    if start_date is None:
        start_date = get_earliest_available_date()
    if end_date is None:
        end_date = get_latest_available_date()
    if set_specs is None:
        set_specs = ['physics', 'math', 'cs', 'q-bio', 'q-fin', 'stat', 'eess', 'econ']
    
    # Ensure database table exists before checking for missing dates
    logger.info("Ensuring database table exists for backfill...")
    try:
        reset_arxiv(TABLE_NAME)
        logger.info("Database table ready for backfill")
    except Exception as e:
        logger.error(f"Failed to create database table for backfill: {e}")
        return 0
    
    logger.info(f"Starting backfill process from {start_date} to {end_date}")
    logger.info(f"Set specifications: {set_specs}")
    
    total_processed = 0
    successful_sets = 0
    failed_sets = 0
    
    for set_spec in set_specs:
        logger.info(f"Processing set_spec: {set_spec}")
        
        # Get missing dates for this set_spec
        missing_dates = get_missing_dates(start_date, end_date, set_spec)
        
        if not missing_dates:
            logger.info(f"No missing dates found for {set_spec}")
            successful_sets += 1
            continue
        
        logger.info(f"Found {len(missing_dates)} missing dates for {set_spec}")
        
        # Process missing dates in chunks to avoid overwhelming the API
        chunk_size = 7  # Process one week at a time
        date_chunks = [missing_dates[i:i + chunk_size] for i in range(0, len(missing_dates), chunk_size)]
        
        for chunk_idx, date_chunk in enumerate(date_chunks):
            logger.info(f"Processing chunk {chunk_idx + 1}/{len(date_chunks)} for {set_spec}")
            
            for date_str in date_chunk:
                try:
                    # Convert single date to date range (same day)
                    current_date = date.fromisoformat(date_str)
                    next_date = current_date + timedelta(days=1)
                    
                    logger.info(f"Backfilling {set_spec} for date {date_str}")
                    
                    # Harvest records for this specific date
                    records_processed = get_records(TABLE_NAME, sickle_client, "oai_dc", set_spec, current_date, next_date)
                    
                    if records_processed > 0:
                        total_processed += records_processed
                        logger.info(f"Successfully processed {records_processed} records for {set_spec} on {date_str}")
                    elif records_processed == 0:
                        logger.info(f"No records found for {set_spec} on {date_str}")
                    else:
                        logger.error(f"Failed to process {set_spec} on {date_str}")
                    
                    # Rate limiting between individual date requests
                    sleep(3)
                    
                except Exception as e:
                    logger.error(f"Error processing {set_spec} for date {date_str}: {e}")
                    continue
            
            # Rate limiting between chunks
            if chunk_idx < len(date_chunks) - 1:
                logger.info("Rate limiting: waiting 5 seconds before next chunk")
                sleep(5)
        
        successful_sets += 1
        logger.info(f"Completed backfill for {set_spec}")
    
    logger.info("=" * 60)
    logger.info("BACKFILL COMPLETED - SUMMARY")
    logger.info("=" * 60)
    logger.info(f"Total set specifications processed: {len(set_specs)}")
    logger.info(f"Successful sets: {successful_sets}")
    logger.info(f"Failed sets: {failed_sets}")
    logger.info(f"Total records processed: {total_processed}")
    logger.info("=" * 60)
    
    return total_processed


if __name__ == "__main__":
    import argparse
    
    # Parse command line arguments
    parser = argparse.ArgumentParser(description='arXiv data harvesting with backfill support')
    parser.add_argument('--mode', choices=['recent', 'backfill', 'both'], default='recent',
                       help='Harvest mode: recent (last 2 days), backfill (missing dates), or both')
    parser.add_argument('--start-date', type=str, help='Start date for backfill (YYYY-MM-DD)')
    parser.add_argument('--end-date', type=str, help='End date for backfill (YYYY-MM-DD)')
    parser.add_argument('--set-specs', nargs='+', 
                       default=['physics', 'math', 'cs', 'q-bio', 'q-fin', 'stat', 'eess', 'econ'],
                       help='Set specifications to process')
    args = parser.parse_args()
    
    # Configuration
    max_retries = int(os.getenv('ARXIV_MAX_RETRIES', '3'))
    retry_status_codes = [503, 429]  # Added 429 for rate limit exceeded
    default_retry_after = int(os.getenv('ARXIV_RETRY_AFTER', '5'))
    table_name = TABLE_NAME
    rate_limit_delay = int(os.getenv('ARXIV_RATE_LIMIT_DELAY', '3'))

    # Initialize sickle with arXiv OAI-PMH base URL
    base_url = 'http://export.arxiv.org/oai2'
    sickle = Sickle(
        base_url, 
        max_retries=max_retries, 
        retry_status_codes=retry_status_codes, 
        default_retry_after=default_retry_after
    )

    # Ensure database table exists
    logger.info("Ensuring database table exists...")
    try:
        reset_arxiv(table_name)
        logger.info("Database table ready")
    except Exception as e:
        logger.error(f"Failed to create database table: {e}")
        sys.exit(1)

    # Define the parameters for harvesting
    metadata_prefix = 'oai_dc'
    set_specs = args.set_specs
    
    # Initialize metrics
    start_arxiv = datetime.now()
    total_records_processed = 0
    
    # Process based on mode
    if args.mode in ['recent', 'both']:
        # Recent harvest (last 2 days)
        date_from = date.today() - timedelta(days=2)
        date_until = date.today() - timedelta(days=1)
        
        logger.info(f"Starting recent harvest from: {date_from} until: {date_until}")
        logger.info(f"Rate limiting: {rate_limit_delay} seconds between requests")
        logger.info(f"Processing {len(set_specs)} set specifications: {set_specs}")

        recent_records_processed = 0
        successful_sets = 0
        failed_sets = 0

        # Process each set specification with rate limiting
        for i, set_spec in enumerate(set_specs):
            logger.info(f"Processing set_spec {i+1}/{len(set_specs)}: {set_spec}")
            
            try:
                records_processed = get_records(table_name, sickle, metadata_prefix, set_spec, date_from, date_until)
                
                if records_processed > 0:
                    recent_records_processed += records_processed
                    successful_sets += 1
                    logger.info(f"Successfully processed {records_processed} records for {set_spec}")
                elif records_processed == 0:
                    logger.info(f"No records found for {set_spec}")
                    successful_sets += 1
                else:
                    failed_sets += 1
                    logger.error(f"Failed to process {set_spec}")
                    
            except Exception as e:
                failed_sets += 1
                logger.error(f"Unexpected error processing {set_spec}: {e}")
            
            # Rate limiting: wait between different set_specs (except for the last one)
            if i < len(set_specs) - 1:
                logger.info(f"Rate limiting: waiting {rate_limit_delay} seconds before next set_spec")
                sleep(rate_limit_delay)

        total_records_processed += recent_records_processed
        
        logger.info("=" * 60)
        logger.info("RECENT HARVEST COMPLETED - SUMMARY")
        logger.info("=" * 60)
        logger.info(f"Total set specifications processed: {len(set_specs)}")
        logger.info(f"Successful sets: {successful_sets}")
        logger.info(f"Failed sets: {failed_sets}")
        logger.info(f"Recent records processed: {recent_records_processed}")
        logger.info("=" * 60)
    
    if args.mode in ['backfill', 'both']:
        # Backfill missing dates
        start_date = args.start_date
        end_date = args.end_date
        
        logger.info("Starting backfill process...")
        backfill_records_processed = backfill_missing_dates(
            start_date=start_date, 
            end_date=end_date, 
            set_specs=set_specs, 
            sickle_client=sickle
        )
        
        total_records_processed += backfill_records_processed

    # Calculate and display final metrics
    total_time = datetime.now() - start_arxiv
    
    logger.info("=" * 60)
    logger.info("OVERALL HARVEST COMPLETED - FINAL SUMMARY")
    logger.info("=" * 60)
    logger.info(f"Mode: {args.mode}")
    logger.info(f"Total records processed: {total_records_processed}")
    logger.info(f"Time elapsed: {total_time}")
    logger.info(f"Records per minute: {total_records_processed / (total_time.total_seconds() / 60) if total_time.total_seconds() > 0 else 0:.2f}")
    logger.info("=" * 60)
