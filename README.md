# FileServer_CSI4337

This project is a multithreaded TCP file transfer system in C++ for Linux. The client sends a filename and the server replies with the file contents or `ERROR: file not found`. The server uses a bounded producer-consumer request queue, a worker thread pool, runtime-selectable scheduling, an in-memory LRU cache, and thread-safe request logging.

## Files

- `server.cpp`: Multithreaded TCP server with queueing, scheduling, caching, and logging
- `client.cpp`: Connects to the server, sends a filename, and prints the response to stdout
- `benchmark.py`: Concurrent benchmark tool for measuring throughput and response time
- `test.txt`, `big.txt`: Sample files for manual testing
- `Makefile`: Builds both executables

## Build

Use `make`:

```bash
make
```

This produces:

- `server`
- `client`

The code also compiles with:

```bash
g++ -std=c++17 -pthread server.cpp -o server
g++ -std=c++17 -pthread client.cpp -o client
```

## Run

Start the server:

```bash
./server <port>
```

Optional arguments:

```bash
./server <port> --workers <n> --queue <size> --policy <fifo|sff> --cache-size <bytes>
./server <port> --cache off
```

Example:

```bash
./server 8080
./server 8080 --workers 4 --queue 20
./server 8080 --workers 4 --queue 20 --policy fifo
./server 8080 --workers 4 --queue 20 --policy sff
./server 8080 --workers 4 --queue 20 --policy fifo --cache-size 1048576
./server 8080 --cache off
```

Defaults:

- `workers = 4`
- `queue size = 20`
- `policy = fifo`
- `cache = enabled`
- `cache size = 1048576` bytes

Run the client from another terminal:

```bash
./client <ip> <port> <filename>
```

Example:

```bash
./client 127.0.0.1 8080 test.txt
```

## Overview

- The accept loop receives client connections and acts as the producer
- Each request stores the socket fd, filename, client address, arrival time, and file size estimate
- The bounded queue blocks the acceptor when full and blocks workers when empty
- Worker threads pop requests and send file contents back to the client
- The server continues running until interrupted with `Ctrl+C`

If the file exists, the client prints the contents. If the file does not exist, the client prints:

```text
ERROR: file not found
```

## Scheduling

- `fifo`: serve requests in arrival order
- `sff`: choose the queued request with the smallest file size estimate first
- Missing files are estimated as size `0`, so they are still handled cleanly under either policy

## Cache

- The server keeps file contents in a bounded in-memory LRU cache when caching is enabled
- Workers check the cache before reading from disk
- Cache hits send the stored bytes directly
- Cache misses load the file from disk and insert it if the file fits within the cache capacity
- Missing files are never cached
- The cache is protected by a mutex so multiple workers can use it safely

Use `--cache-size <bytes>` to change the cache capacity or `--cache off` to disable caching entirely.

## Logging

Each completed request appends a line to `server.log` with:

- Timestamp
- Client address
- Filename
- Scheduling policy
- Worker thread id
- Cache hit or miss
- Success or error status
- Response time in milliseconds

Example:

```text
2026-05-01 13:42:10 | client=127.0.0.1:51432 | file=test.txt | policy=fifo | worker=2 | cache=miss | status=success | response_ms=4.72
```

Log writes are protected by a mutex so lines are not interleaved under concurrent load.

## Testing

Build:

```bash
make
```

Terminal 1:

```bash
./server 8080 --workers 4 --queue 20
```

Terminal 2:

```bash
./client 127.0.0.1 8080 test.txt
```

Additional checks:

- Valid file:
```bash
./client 127.0.0.1 8080 test.txt
```
- Missing file:
```bash
./client 127.0.0.1 8080 missing.txt
```
- Multiple clients:
```bash
./client 127.0.0.1 8080 test.txt &
./client 127.0.0.1 8080 big.txt &
./client 127.0.0.1 8080 test.txt &
wait
```
- Repeated same file for cache hits:
```bash
./client 127.0.0.1 8080 big.txt
./client 127.0.0.1 8080 big.txt
tail -n 5 server.log
```
- FIFO vs SFF:
Run once with `--policy fifo` and once with `--policy sff`, then send a mix of `test.txt` and `big.txt` requests.
- Cache on vs off:
Run once with `--cache-size 1048576` and once with `--cache off`, then compare `server.log` cache fields and benchmark throughput.
- Different worker counts:
Restart the server with values such as `--workers 1`, `--workers 4`, and `--workers 8` and compare response times.

## Benchmark

Example:

```bash
python3 benchmark.py --host 127.0.0.1 --port 8080 --requests 100 --concurrency 10 --files test.txt big.txt
```

The script reports:

- Total benchmark time
- Average response time
- Throughput in requests per second
- Total bytes received
- Number of completed and failed requests

Cache hit rate can be estimated afterward by inspecting `server.log`.

## Known Limitations

- The server expects the client to send only the filename as raw bytes in a single request
- File paths are interpreted relative to the server's current working directory
- The current protocol does not send file metadata such as length, content type, or explicit status codes
- The shortest-file-first policy uses `stat()` as a best-effort size estimate before service

## Socket APIs Used

- `socket()`
- `bind()`
- `listen()`
- `accept()`
- `connect()`
- `send()`
- `recv()`
- `close()`
