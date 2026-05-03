# FileServer_CSI4337

This project is a TCP file transfer system in C++ for Linux. The client sends a filename and the server replies with the file contents or `ERROR: file not found`. The server runs continuously, uses a bounded producer-consumer queue with worker threads, supports selectable request scheduling, and can keep recently requested files in an in-memory LRU cache.

## Current Scope

- Multiple client connections
- Main accept loop acts as the producer
- Bounded blocking request queue
- Fixed worker thread pool
- Configurable `--workers` and `--queue` options
- Runtime-selectable scheduling policy: FIFO or shortest-file-first
- Optional bounded in-memory LRU file cache
- Continuous server operation until interrupted

Not included yet:

- Request logging
- Benchmark tooling

## Files

- `server.cpp`: Multithreaded TCP server with scheduling and caching support
- `client.cpp`: Connects to the server, sends a filename, and prints the response to stdout
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
g++ -std=c++17 server.cpp -o server
g++ -std=c++17 client.cpp -o client
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

## Expected Behavior

- If the file exists, the client prints the file contents
- If the file does not exist, the client prints:

```text
ERROR: file not found
```

Each client connection is handled by a worker thread after the request is placed into the bounded queue. The server stays running until stopped with `Ctrl+C`.

## Request Handling Model

Each accepted request stores:

- Client socket file descriptor
- Requested filename
- Client address string
- Request arrival timestamp
- File size estimate

Queue behavior:

- If the queue is full, the acceptor blocks until a worker removes a request
- If the queue is empty, workers block until a request is available

Scheduling behavior:

- `fifo`: requests are served in arrival order
- `sff`: the next request chosen is the one with the smallest file size estimate
- Missing files are estimated as size `0`, so they are handled cleanly under either policy

## Cache Behavior

- The server can keep file contents in memory using a bounded LRU cache
- On each request, workers first check the cache before reading from disk
- Cache hits send the stored contents directly
- Cache misses read the file from disk and insert it into the cache if it fits
- Missing files are never cached
- The cache is protected by a mutex so concurrent workers do not corrupt its state

Use `--cache-size <bytes>` to change the cache capacity or `--cache off` to disable caching entirely.

## Manual Testing

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

- Request a missing file and verify the client prints `ERROR: file not found`
- Open multiple client terminals at once to observe concurrent service
- Run the server with `--policy fifo` and `--policy sff` to compare request ordering
- Request the same large file repeatedly with cache enabled to exercise cache reuse
- Run once with `--cache-size 1048576` and once with `--cache off` to compare cached vs direct-disk behavior

## Socket APIs Used

- `socket()`
- `bind()`
- `listen()`
- `accept()`
- `connect()`
- `send()`
- `recv()`
- `close()`
