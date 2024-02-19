from datetime import date, datetime, timedelta
from time import sleep
import platform

from sickle import Sickle
from sickle.oaiexceptions import NoRecordsMatch
from pymongo import MongoClient
from pymongo.errors import DuplicateKeyError
from requests.exceptions import ConnectionError

from icecream import ic


def get_records(mongo_collection, client, metadata_prefix="oai_dc", set_spec=None, from_date=None, until_date=None):
    time_sleep = 5

    client_mongo = get_mongo_client()
    db = client_mongo['_05456258']
    collection = db[mongo_collection]

    # Harvest records using sickle
    try:
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
        print(f'get_records: records error for {set_spec} from {from_date} to {until_date}: {e}')
        return(-1)

    if records:
        # Iterate through fetched records
        try:
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
                print(f'get_records: extracted {extracted['metadata_identifier'][0]}')

                if extracted:
                    res = collection.replace_one(
                        {'header_identifier': extracted['header_identifier']}, extracted, upsert=True
                    )
                    print(f'get_records: inserted {extracted['metadata_identifier']}')
                
        except ConnectionError as f:
            print(f'get_records: iterate error: {f}')
            print(f'get_records: iterate error: {f.response.text}')
            sleep(time_sleep)
        except Exception as h:
            print(f'get_records: iterate error: {h}')


def reset_arxiv(mongo_collection):

    try:
        client = get_mongo_client()
        db = client['_05456258']
        db.create_collection(mongo_collection)
    except Exception as e:
        print(f"reset_arxiv create collection error: {e}")

    try:
        collection = db[mongo_collection]

        # create indexes
        collection.create_index(
            ['header_identifier'], 
            name=f"{mongo_collection}_header_identifier_idx", 
            unique=True
        )

        collection.create_index(
            ['header_datestamp'], 
            name=f"{mongo_collection}_header_datestamp_idx"
        )

        collection.create_index(
            ['header_setSpecs'], 
            name=f"{mongo_collection}_header_setSpecs_idx"
        )

        collection.create_index(
            ['header_datestamp', 'header_setSpecs'], 
            name=f"{mongo_collection}_header_datestamp_setSpecs_idx"
        )

    except Exception as f:
        print(f"reset_arxiv create indexes error: {f}")


def get_mongo_client():
    if platform.system() == 'Darwin': 
        file_auth = "/Users/bernardchase/.config/authenticate/kahlilcollard"
    elif platform.system() == 'Linux':
        auth_usr = "/db/mongodb-u.txt"
        auth_pwd = "/db/mongodb-p.txt"

        with open(auth_usr) as f:
            line = f.readlines()[0]
            usr = line.strip()

        with open(auth_pwd) as g:
            line = g.readlines()[0]
            pwd = line.strip()

        connection_string = f'mongodb://{usr}:{pwd}@mongo/admin'

    return(
        MongoClient(connection_string)
    )


def get_date_list(date_start, date_end):
    ds = date.fromisoformat(date_start)
    de = date.fromisoformat(date_end)

    return([(ds + timedelta(days=x)).isoformat() for x in range((de - ds).days)])


if __name__ == "__main__":
    # init
    max_retries = 2
    retry_status_codes = [503]
    default_retry_after = 5
    mongo_collection = "r"

    # Initialize sickle with arXiv OAI-PMH base URL
    base_url = 'http://export.arxiv.org/oai2'
    sickle = Sickle(
        base_url, 
        max_retries=max_retries, 
        retry_status_codes=retry_status_codes, 
        default_retry_after=default_retry_after
    )

    # Define the parameters for harvesting
    metadata_prefix = 'oai_dc'
    set_specs = ['physics', 'math', 'cs', 'q-bio', 'q-fin', 'stat', 'eess', 'econ']
    # set_spec = 'cs'  # Specify a set (if required), e.g., 'physics'
    # set_spec = 'math'  # Specify a set (if required), e.g., 'physics'
    # set_spec = 'physics'  # Specify a set (if required), e.g., 'physics'
    # set_spec = 'q-bio'  # Specify a set (if required), e.g., 'physics'
    # set_spec = 'q-fin'  # Specify a set (if required), e.g., 'physics'
    # set_spec = 'stat'  # Specify a set (if required), e.g., 'physics'
    # set_spec = 'eess'  # Specify a set (if required), e.g., 'physics'
    # set_spec = 'econ'  # Specify a set (if required), e.g., 'physics'
    # set_spec = 'CoRR'  # Specify a set (if required), e.g., 'physics'
    date_from = date.today() - timedelta(days=2)  # Specify a date range if needed, e.g., '2022-01-01'
    date_until = date.today() - timedelta(days=1) # Specify an end date if needed, e.g., '2022-12-31'
    print(f'from: {date_from}\tuntil: {date_until}')

    # get records
    start_arxiv = datetime.now()

    # reset_arxiv(mongo_collection)
    for set_spec in set_specs:
        print(f'beginning set_spec: {set_spec}')
        res0 = get_records(mongo_collection, sickle, metadata_prefix, set_spec, date_from, date_until)

    time_arxiv = datetime.now() - start_arxiv

    ###########
    # present job metrics
    ###########
    print(f"elapsed - set_arxiv: \t{time_arxiv}")
    print(f"elapsed - total: \t{datetime.now() - start_arxiv}")
    pass
