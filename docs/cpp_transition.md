# C++ Transition Note (Archived)

This document previously described the PostgreSQL-first C++ transition path.

That architecture is obsolete for normal runtime operation.

## Active Source of Truth

- `docs/vector_db_migration_plan.md`

## Current Runtime Direction

- Normal runtime persistence: **Qdrant**
- Embedding generation: **local embeddings service**
- PostgreSQL role: **migration utility only**
