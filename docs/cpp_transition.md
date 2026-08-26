# C++ Transition Note (Archived, Non-operational)

This note is retained for historical context only. It is not an operational
runbook or source of truth; use the root README and `docker-compose.yaml` for
the supported deployment.

## Current Runtime Direction

- Normal runtime persistence: **Qdrant**
- Embedding generation: **local embeddings service**
- Application services: **app**, **qdrant**, and **embeddings**
