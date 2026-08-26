# PostgreSQL Retirement Inventory (Archived, Non-operational)

> This inventory is retained as historical implementation context only. It is
> not an operational runbook, deployment source of truth, or maintenance
> procedure. The active repository must not use the paths listed here.

Status: historical inventory for the completed database retirement work
Repository: `arhida`

## Scope and decision rule

This inventory covers the tracked repository surfaces that can execute, build,
configure, deploy, maintain, or document a PostgreSQL path. It was produced by
checking the tracked file list and running repository-wide searches for
`PostgreSQL`, `postgres`, `POSTGRES_*`, `libpq`, `PQ*`, `psycopg2`, `psql`, SQL
schema types, migration targets, and PostgreSQL secret/network names. Existing
ignored Qdrant data and log output were not treated as source code, and no
credential files were read.

A reference is **active** when an executable, build definition, configuration
template, container definition, CI/maintenance script, or user-facing
operational instruction can still invoke or require PostgreSQL. A reference is
**historical** only when it is explicitly an archive/design record and cannot
be used as an operational instruction. Every active surface below is assigned
to one of the removal tasks.

## Removal ownership

| Surface | Active references and dependency | Owner |
|---|---|---|
| Root environment template | `.env.example:1-15` defines `POSTGRES_HOST`, `POSTGRES_DB`, `POSTGRES_USER`, `POSTGRES_PASSWORD`, `POSTGRES_PORT`, `POSTGRES_SCHEMA`, `POSTGRES_TABLE`, and Docker secret-file settings. | `t_2cc1bd1b` |
| C++ configuration API and loader | `include/config/Config.h:18-25,57-60,70-77,109-112` exposes/stores PostgreSQL and Docker credential settings; `src/config/Config.cpp:52-59,98-103` reads them from the environment and default `.env`. They are no longer used by the normal Qdrant runtime but remain part of configuration state. | `t_2cc1bd1b` |
| C++ PostgreSQL storage wrapper | `include/db/Database.h:1-53` includes `libpq-fe.h`, owns `PGconn`, and exposes SQL/migration methods. `src/db/Database.cpp:1-761` implements connection, schema/table/index creation, JSONB upserts, missing-date queries, offset/keyset reads, counts, identifier checks, and `PQ*` execution. | `t_89583b1d` |
| Relational abstraction residue | `include/db/StorageEngine.h:22-25` retains schema/table/index methods needed only by the old relational implementation. `src/db/QdrantStorage.cpp:78-103` implements those methods as no-op/compatibility calls. Simplify the interface or explicitly retain only what Qdrant needs while removing the wrapper. | `t_89583b1d` |
| SQL query helper | `include/db/QueryBuilder.h:1-28` and `src/db/QueryBuilder.cpp:1-53` implement SQL construction. The helper has no call sites other than its own implementation, but `CMakeLists.txt:52-54` still compiles it into `arhida-cpp`; delete it or remove it from the build with the wrapper cleanup. | `t_89583b1d` |
| Migration C++ target | `include/migration/PostgresToQdrantMigrator.h:1-43`, `src/migration/PostgresToQdrantMigrator.cpp:1-320`, and `src/migrate_main.cpp:1-57` implement the `arhida-migrate` CLI, PostgreSQL reads, embedding calls, Qdrant writes, checkpointing, and parity validation. | `t_89583b1d` |
| CMake/libpq path | `CMakeLists.txt:6,12-14,63-75,88-104` conditionally discovers `libpq`, builds the migration executable with `Database.cpp`, links `${LIBPQ_LIBRARIES}`, and installs `arhida-migrate`. The conditional is not sufficient for retirement because an explicit build flag and the migration script still activate it. | `t_89583b1d` |
| Main C++ image | `Dockerfile:8,12,20,35-39,52-65,76-77` defaults `BUILD_MIGRATION_TOOL=ON`, installs `libpq-dev`/`libpq5`, configures the migration target, copies `arhida-migrate`, and creates `/db`. The resulting image can still build and ship a PostgreSQL client path. Build cleanup is owned by `t_89583b1d`; deployment/image consistency should be coordinated with `t_2cc1bd1b`. | `t_89583b1d` (build), `t_2cc1bd1b` (deployment consistency) |
| Root secret ignore rules | `.gitignore:39-41` names `db/postgres-u.txt` and `db/postgres-p.txt`. These are not present as tracked files, but the ignore rules preserve the old credential contract and should be removed with the active configuration cleanup. | `t_2cc1bd1b` |
| Migration runner | `scripts/postgres_to_qdrant_migration.sh:1-196` selects `db_prdnet`/host networking, reads PostgreSQL host and all credential/schema/table variables, builds `arhida-migrate` with `BUILD_MIGRATION_TOOL=ON`, passes PostgreSQL variables into the container, and invokes `/app/arhida-migrate`. This is an active maintenance path, not merely a comment. The operational removal belongs to `t_2cc1bd1b`; its CMake/image coupling must be removed by `t_89583b1d`. | `t_2cc1bd1b` (primary), `t_89583b1d` (build coupling) |
| Cutover wrapper | `scripts/postgres_to_qdrant_cutover.sh:4-19` runs the migration runner in `migrate` and `verify` stages and therefore still requires PostgreSQL. | `t_2cc1bd1b` |
| Migration smoke test | `scripts/postgres_to_qdrant_migration_smoke.sh:4-226` starts a PostgreSQL 16 container, provisions `arxiv.metadata` with `SERIAL`/`JSONB`, seeds rows with `psql`, exports all PostgreSQL variables, runs the migration script, and checks Qdrant parity. It is an active test/maintenance path and must be deleted or replaced with a Qdrant-plus-embeddings smoke test. | `t_2cc1bd1b` |
| Legacy Python stack | `legacy_python/arhida.py:1-592` imports `psycopg2` and `sickle`, reads PostgreSQL settings/secrets, opens PostgreSQL connections, creates schema/table/indexes, performs `ON CONFLICT` upserts, queries missing dates, and exposes recent/backfill entry points. `legacy_python/requirements.txt:1-5` installs `psycopg2-binary`; `legacy_python/Dockerfile:1-35` installs `libpq-dev` and runs the script; `legacy_python/compose.yaml:1-26` mounts PostgreSQL secrets on external `db_prdnet`; `legacy_python/.env.example:1-15` documents the PostgreSQL variables; `legacy_python/README.md:1-145` documents PostgreSQL operation and Docker usage. | `t_aa213d50` |
| User-facing migration guidance | `README.md:6-10` presents PostgreSQL as historical migration tooling; `README.md:205-296` gives active migration/cutover commands, PostgreSQL endpoint examples, `psql` checks, source schema/table names, and checkpoint instructions. `README.md:478-500` still lists `legacy_python` as a reference implementation. Remove obsolete operational instructions and update the project structure after the code/script cleanup. | `t_2cc1bd1b` (root operational docs), `t_aa213d50` (legacy stack references) |
| Migration-plan operational text | `docs/vector_db_migration_plan.md` contains PostgreSQL as the original/current dependency (`:1-40`), transitional compatibility instructions (`:160-192`), the historical migration implementation inventory (`:1128-1209` and `:1523-1568`), current-status claims that migration tooling/libpq still exists (`:1756-1791`), and future migration/cleanup instructions (`:1843-1969`). The document is a design/history record, but it currently points readers toward migration tooling and is named the active source of truth by `docs/cpp_transition.md:7-15`; it must either be rewritten to a clearly archived, non-operational record or have all PostgreSQL procedures removed. | `t_2cc1bd1b` (documentation/operational status), with `t_89583b1d` supplying corrected build-target facts |

## Historical references that may remain only if inert

- `docs/cpp_transition.md:1-15` is explicitly labelled `(Archived)` and says
  the normal runtime is Qdrant plus the local embeddings service. It may remain
  as a historical note, but it must not continue to point maintainers to live
  PostgreSQL migration procedures after those procedures are removed.
- `docs/vector_db_migration_plan.md` records the migration design and completed
  phases, but it is not safe to leave operational migration commands or a
  claim that it is the active source of truth. Add an archive/non-operational
  banner and remove or rewrite the PostgreSQL execution instructions as part
  of `t_2cc1bd1b`.
- The root `.gitignore` secret names are not historical documentation: they are
  an active ignore contract and are assigned above for removal.
- No tracked SQL files, Python lockfiles, generated PostgreSQL bindings, or
  PostgreSQL credential files were found. The only Python dependency manifest
  is `legacy_python/requirements.txt`.

## Dependency graph

### Normal production/runtime path (target to preserve)

```text
.env / compose environment
  -> Config::load() [Qdrant + embeddings + arXiv settings]
  -> src/main.cpp
       -> EmbeddingClient::healthCheck() [GET /health]
       -> QdrantStorage::connect()
            -> ensureCollection() [Qdrant HTTP API]
            -> validateCollectionConfiguration()
       -> Harvester(StorageEngine)
            -> OaiClient [arXiv OAI-PMH]
            -> EmbeddingClient::embed() [POST /embed]
            -> QdrantStorage::upsertRecord()
```

Backfill uses `QdrantStorage::getMissingDates()` to scroll Qdrant payloads,
then follows the same OAI-PMH -> embeddings -> Qdrant upsert path. No
`Database`, `libpq`, PostgreSQL settings, source database, or migration
checkpoint is required in this runtime graph.

### PostgreSQL path to remove

```text
migration/cutover/smoke shell scripts
  -> Dockerfile + CMake BUILD_MIGRATION_TOOL=ON
       -> arhida-migrate / migrate_main.cpp
            -> PostgresToQdrantMigrator
                 -> Database
                      -> libpq / PGconn / PQ* / SQL schema + queries
                 -> EmbeddingClient
                 -> QdrantStorage

legacy_python/compose.yaml
  -> legacy_python/Dockerfile + requirements.txt
       -> legacy_python/arhida.py
            -> psycopg2 + PostgreSQL env/secrets + SQL schema/upserts/queries
```

## Expected Qdrant-plus-embeddings runtime contract

The removal must preserve this contract:

1. `docker-compose.yaml` runs `app`, `qdrant`, and `embeddings` on the
   `arhida` internal network. `app` receives `VECTOR_DB_PROVIDER=qdrant`,
   `QDRANT_URL`, `QDRANT_COLLECTION`, `VECTOR_SIZE`, and the embedding service
   URL/model/batch/retry settings. It must not receive PostgreSQL variables or
   secrets.
2. The embeddings service exposes `GET /health` and `POST /embed`. The default
   model is `BAAI/bge-small-en-v1.5`, and the default vector dimension is 384;
   the service validates model/tokenizer artifacts and returns vectors matching
   `MODEL_DIMENSION`.
3. `src/main.cpp` loads configuration, fails fast if the configured provider is
   not `qdrant`, checks embeddings health, constructs `QdrantStorage`, connects
   it, and passes it to `Harvester` through `StorageEngine`.
4. `QdrantStorage::connect()` ensures the configured collection exists with
   cosine distance and validates its configured vector size. Each harvested
   record is converted into deterministic FNV-1a point ID + embedding +
   `header_*`/`metadata_*` payload, then upserted through Qdrant HTTP.
5. Recent and backfill modes use the existing OAI-PMH client and embeddings
   service. Backfill date discovery is performed with Qdrant payload filters;
   it must not query a relational table.
6. CI and smoke tests should start Qdrant and embeddings (or a mock embedding
   HTTP service), wait for `/healthz` and `/health`, and run the C++ app without
   any PostgreSQL image, package, client header, URL, credential, or network.

## Handoff checklist by task

### `t_2cc1bd1b` — configuration, secrets, operations, documentation

- Remove PostgreSQL keys and secret-file defaults from root configuration.
- Remove root PostgreSQL secret ignore rules and any `/db`/external
  PostgreSQL-network assumptions from deployment definitions.
- Delete or retire the migration, cutover, and migration-smoke scripts; replace
  required smoke coverage with Qdrant/embeddings checks.
- Remove active PostgreSQL commands and credentials from `README.md` and the
  migration-plan documentation, retaining only clearly marked history.
- Verify compose/startup definitions remain internally consistent for only
  Qdrant and embeddings.

### `t_89583b1d` — C++ wrapper and build path

- Delete `Database.h/.cpp`, migration headers/implementation/entry point, and
  the unused SQL `QueryBuilder` if no replacement call site exists.
- Remove `BUILD_MIGRATION_TOOL`, `LIBPQ`, migration sources/target/install
  logic, and all libpq include/link/package requirements.
- Make the main image build/install only `arhida-cpp` and retain the
  Qdrant/embeddings C++ path.
- Simplify `StorageEngine`/Qdrant compatibility methods if they only existed
  for relational storage, while preserving harvester compilation.

### `t_aa213d50` — legacy Python stack

- Remove the legacy Python application and its Docker/Compose/env/requirements
  surfaces, or retain only Python tooling that has a demonstrated Qdrant or
  embeddings purpose.
- Ensure no active Python import, package, entry point, or test references
  `psycopg2`, PostgreSQL settings, or the old secret/network contract.
- Update root documentation that calls `legacy_python` a reference
  implementation.

### `t_f0dceaf3` — final integration validation

- Repeat repository searches after the three removal tasks complete.
- Build from a clean environment without PostgreSQL packages or headers.
- Run unit tests and Qdrant/embeddings startup/runtime smoke checks.
- Report only explicitly archived documentation references as historical; any
  executable/configuration/maintenance hit is a release-blocking omission.
