# Decentralized Peer-to-Peer Load-Balancing System

A lightweight C++/gRPC demo that implements a peer-to-peer, decentralized load-balancing and failover algorithm. Each node periodically heartbeats its own “score” (40% CPU, 30% queue depth, 20% memory‐free, 10% uptime plus jitter) to its neighbors. Incoming tasks are routed to the highest-scoring alive node; on failure, they automatically retry down the score list.

---

## Features

- **Weighted scoring** based on CPU load, queue length, memory usage, and uptime.
- **Heartbeat gossip**: each node learns peer scores (and -hops) up to configurable depth.
- **Fault tolerance**: tasks forward to the next best node if the best one fails, with client and server retry logic.
- **Metrics & logs**: per-node JSONL metrics, detailed event logs.

---

## Prerequisites

- **C++17** compiler
- **gRPC** & **Protobuf** (v1.35+ recommended)
- **CMake** (v3.12+)
- Unix-like OS (Linux, macOS)

---

## Build

```bash
chmod +x start.sh
./start.sh
mkdir -p build && cd build
cmake ..
make -j
```

---

## Running

Start servers like this:

In build folder run

```bash
./server <address of your device>:<port number> <address of your neighbour>:<port number> ...
```

Start client like this:

```bash
./client <TASK_ID> "<PAYLOAD>" <REPLY_ADDR> <SERVER1> [<SERVER2> ...]
```

## Configuration

- **Score weights**: Adjust the constants in `Server::computeScore()`.
- **Max gossip hops**: Currently hard-coded to 3, can be made configurable.
- **Heartbeat interval & timeout**: Edit `heartbeatLoop()` and gRPC deadlines in `server.cpp`.

## Logging & Metrics

- **Event logs**: JSON lines of named events for debugging (`logs/*.log`).
- **Metrics**: JSON lines recording CPU%, memory%, uptime, and score per second (`logs/*.metrics.jsonl`).
