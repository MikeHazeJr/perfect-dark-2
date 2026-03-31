# SA-1: Session Catalog & Modular API Design

## Overview

The session catalog is a per-match u16 translation layer that maps catalog string IDs to compact wire IDs for network transmission. It eliminates the need to send full string IDs in every in-match message by establishing a shared mapping at match start.

## Motivation

The match manifest (SA-0 / Phase A) already establishes a canonical list of all assets required for a match. The session catalog builds on this by assigning each manifest entry a compact u16 wire ID, enabling future protocol messages to reference assets with 2 bytes instead of 64.

## Architecture

```
Server                          Clients
------                          -------
manifestBuild()                 (receives SVC_MATCH_MANIFEST)
sessionCatalogBuild()           (receives SVC_SESSION_CATALOG)
sessionCatalogBroadcast()  -->  sessionCatalogReceive()
  SVC_SESSION_CATALOG           g_SessionCatalog populated
```

### Wire Format — SVC_SESSION_CATALOG (0x67)

```
u8   opcode = 0x67
u16  num_entries
[for each entry]:
  u16  wire_id       1-based sequential ID
  u8   asset_type    MANIFEST_TYPE_*
  str  catalog_id    null-terminated catalog string ID
```

Wire ID 0 is reserved as "not found / no entry".

### Lifecycle

- **sessionCatalogBuild(manifest)** — server-side, after manifestBuild(). Assigns wire IDs 1..N in manifest order.
- **sessionCatalogLogMapping()** — server-side debug logging.
- **sessionCatalogBroadcast()** — server-side, after SVC_MATCH_MANIFEST. Encodes and sends SVC_SESSION_CATALOG to all room clients.
- **sessionCatalogReceive(src)** — client-side SVC_SESSION_CATALOG handler. Populates g_SessionCatalog.
- **sessionCatalogTeardown()** — both sides on match end.

### Lookup API

- `sessionCatalogLookupWireId(catalog_id)` — string → u16 wire ID (returns 0 if not found)
- `sessionCatalogLookupEntry(wire_id)` — u16 → session_catalog_entry_t* (returns NULL if not found)

## Future Use

Once the session catalog is established, future in-match messages can reference assets by wire_id instead of string ID. For example:
- SVC_PROP_SPAWN could reference weapon models by wire_id
- Custom skin references in player move packets
- Modular API (SA-2+): asset-keyed events using wire IDs for compact encoding

## Files

- `port/include/net/sessioncatalog.h` — types and API
- `port/src/net/sessioncatalog.c` — implementation
- Hooks in `port/src/net/netmsg.c`:
  - `netmsgSvcStageEndRead` → `sessionCatalogTeardown()` (client teardown)
  - `netmsgClcLobbyStartRead` → `sessionCatalogBuild()` + `sessionCatalogLogMapping()` + `sessionCatalogBroadcast()`
  - `netmsgSvcSessionCatalogRead` → `sessionCatalogReceive()` (new handler)
- Dispatch in `port/src/net/net.c`: `case SVC_SESSION_CATALOG`
