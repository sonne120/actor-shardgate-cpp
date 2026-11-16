![C++20](https://img.shields.io/badge/C%2B%2B-20-00599C)
![Language](https://img.shields.io/github/languages/top/sonne120/actor-shardgate-cpp)
![Stars](https://img.shields.io/github/stars/sonne120/actor-shardgate-cpp)
![Last commit](https://img.shields.io/github/last-commit/sonne120/actor-shardgate-cpp)
![Repo size](https://img.shields.io/github/repo-size/sonne120/actor-shardgate-cpp)

# actor-shardgate-cpp

> Actor-based reimplementation of the PacketShard **MasterNode** in modern C++20.

ShardGate is a TCP ingestion gate from the
[PacketShard](https://github.com/sonne120/PacketShard) ecosystem: it
authenticates clients, routes packet snapshots by protocol and persists them
into per-protocol shards.

## Architecture

```
┌────────────────────────┐
│ packet_client          │   --demo N · file.jsonl · stdin
│ (or any TCP client)    │
└────────────────────────┘
             │ TCP :8000, newline-framed
             │ ① SHA-256(api key)  ② JSON snapshots
             ▼
┌──────────────────────────────────────────────────────────────┐
│ ShardGate                                                    │
│                                                              │
│  ┌───────────────────┐   Authenticate     ┌────────────────┐ │
│  │ epoll Reactor     │ ─────────────────▶ │ AuthActor      │ │
│  │ (one event loop)  │ ◀───────────────── │ SHA-256 keyset │ │
│  └───────────────────┘    true / false    └────────────────┘ │
│            │ WriteToShard(proto, json)                       │
│            ▼                                                 │
│  ┌───────────────────┐                                       │
│  │ ShardRouterActor  │   routes by the "proto" field         │
│  └───────────────────┘                                       │
│      │      │      │      │      │        (Forward)          │
│      ▼      ▼      ▼      ▼      ▼                           │
│  ┌─────┐┌─────┐┌─────┐┌─────┐┌─────┐                         │
│  │https││ tcp ││ udp ││ arp ││other│   ShardWriterActor × 5  │
│  └─────┘└─────┘└─────┘└─────┘└─────┘                         │
│      │      │      │      │      │                           │
│      ▼      ▼      ▼      ▼      ▼                           │
│   data/shard-*.jsonl   (IShardStore seam → e.g. mongocxx)    │
└──────────────────────────────────────────────────────────────┘
             │
             ▼  Result<StoredPacket> as a reply document
  {"success":1,"n":1,"shard":"tcp","packetId":"...","transaction_id":"..."}
  {"success":0,"code":18,"codeName":"AuthenticationFailed","errmsg":"..."}
```

The network edge is a single-threaded epoll reactor (Reactor pattern:
non-blocking sockets, level-triggered `epoll_wait`, an `eventfd` wakeup for
replies coming from actor threads); every other server-side box is an actor —
a mailbox drained by one thread, all mutable state isolated behind messages,
contracts as C++20 concepts. Half-sync/half-async: the event loop owns all
sockets, actors own all business state, the only shared point is a task
queue into the loop.

## Protocol

1. Client connects over raw TCP (default port `8000`).
2. First line: lowercase-hex `SHA256(apiKey + "\n")` of a valid API key.
3. Every subsequent line is a flat JSON packet snapshot; the `proto` field
   selects the shard (`HTTPS/TLS/SSL → https`, `TCP`, `UDP`, `ARP`,
   anything else → `other`).

## Result pattern & wire format

Both sides share a custom `Result<T>` (`include/result.hpp`): typed
`Error{code, message}`, `success/failure` factories, guarded `value()`, and
`map / and_then / match`. On the wire a `Result` is a MongoDB-style reply
document:

```jsonc
{"success":1,"n":1,"shard":"tcp","packetId":"...","transaction_id":"..."}
{"success":0,"code":18,"codeName":"AuthenticationFailed","errmsg":"Invalid API Key"}
{"success":0,"code":9,"codeName":"FailedToParse","errmsg":"snapshot is not a JSON object"}
{"success":0,"code":1,"codeName":"InternalError","errmsg":"..."}
```

The client separates **business rejections** (attempt consumed) from
**transport failures** (`HostUnreachable`, code 6 — the packet may have been
stored; exit code 2, redelivery is left to dedup on `transaction_id`).

## Client

```bash
./packet_client --demo 5                          # generated sample packets
./packet_client --key valid_api_key_1 file.jsonl  # one snapshot per line
cat packets.jsonl | ./packet_client --host 10.0.0.5 --port 8000
```

## Build & run

```bash
make                       # or: cmake -B build && cmake --build build
./shardgate
```

| Variable           | Default                                     |
|--------------------|---------------------------------------------|
| `SHARDGATE_PORT`   | `8000`                                      |
| `SHARDGATE_KEYS`   | `valid_api_key_1,valid_api_key_2`           |
| `SHARDGATE_SHARDS` | `./data` (one `shard-*.jsonl` per protocol) |

## Try it

```bash
{
  ./keyhash valid_api_key_1
  echo '{"transaction_id":"t-1","source_ip":"10.0.0.1","dest_ip":"10.0.0.2","proto":"HTTPS"}'
  sleep 0.3
} | nc 127.0.0.1 8000
# -> {"success":1,"msg":"API Key authenticated. You can now send messages."}
# -> {"success":1,"n":1,"shard":"https","packetId":"...","transaction_id":"t-1"}
cat data/shard-https.jsonl
```

## Docker

```bash
docker build -t actor-shardgate-cpp .
docker run -p 8000:8000 actor-shardgate-cpp
```

## Toolchain

Built and analyzed with the Clang ecosystem (GCC still works: `make CXX=g++`).

```bash
make                # clang++ -std=c++20 -O2 -Wall -Wextra
make asan           # -g -fsanitize=address,undefined  (memory + UB)
make tsan           # -g -fsanitize=thread             (data races)
make format         # clang-format -i  (style in .clang-format)
make format-check   # CI mode: fail on unformatted code
make tidy           # clang-tidy static analysis (.clang-tidy)
make compile_commands.json   # compilation database for clangd (VS Code/CLion)
```

Sanitizer builds are smoke-tested end-to-end: the TSan build survives six
concurrent clients streaming through the reactor and actor pipeline with zero
reports; the release build handles 300 concurrent authenticated sessions.
