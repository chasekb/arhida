# Vector Database Architecture Record (Archived, Non-operational)

> This document is retained as historical design context only. It is not an
> operational runbook, migration guide, deployment source of truth, or backup
> procedure. Do not execute commands or configuration examples from archived
> project history.

## Supported architecture

The supported application is a Qdrant-plus-embeddings service:

- `app` runs the C++ arXiv metadata harvester.
- `qdrant` stores vectors and metadata payloads.
- `embeddings` provides `GET /health` and `POST /embed`.
- The services communicate on the `arhida` internal Docker network.

The application requires `VECTOR_DB_PROVIDER=qdrant`, `QDRANT_URL`,
`QDRANT_COLLECTION`, `VECTOR_SIZE`, and the embedding service URL/model and
retry settings. Recent and backfill modes use the OAI-PMH client, the
embeddings service, and Qdrant payload filters; neither mode requires a
relational database or a migration checkpoint.

## Historical scope

Earlier design work considered a relational persistence backend and a
one-time transfer into a vector database. That work is complete and its
implementation, credentials, service definitions, build options, and
maintenance scripts have been removed from the active repository. This record
contains no supported procedure for accessing or transferring data from that
obsolete backend.

The current source of truth is:

- `README.md` for supported usage and runtime checks
- `docker-compose.yaml` for service topology and health checks
- `.env.example` for supported configuration keys

## Runtime contract

1. Compose starts `app`, `qdrant`, and `embeddings` on the `arhida` network.
2. Qdrant exposes `/healthz`; embeddings exposes `/health` and `/embed`.
3. The C++ application fails fast for a provider other than `qdrant` or when
   either dependency is unavailable.
4. `QdrantStorage` creates and validates the configured collection, then
   persists deterministic point IDs, vectors, and `header_*`/`metadata_*`
   payload fields.
5. Model artifacts are mounted read-only into the embeddings service, and the
   service validates their configured dimension at startup.

## Operational verification

Use the supported smoke checks documented in `README.md`, including the
Qdrant and embeddings health checks and the application recent/backfill mode
checks. These checks must use only the services in `docker-compose.yaml` and
must not introduce an additional database image, network, credential, or
maintenance path.
