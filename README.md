# FileServer_CSI4337

This project is a TCP file transfer system in C++ for Linux. The client sends a filename and the server replies with the file contents or `ERROR: file not found`. The server runs continuously and can serve multiple clients through a bounded producer-consumer queue and a fixed worker thread pool.

## Current Scope

- Multiple client connections
- Main accept loop acts as the producer
- Bounded blocking request queue
- Fixed worker thread pool
- Configurable `--workers` and `--queue` options
- Continuous server operation until interrupted

Not included yet:

- Scheduling policies beyond FIFO queue order
- Caching
- Request logging
- Benchmark tooling

## Files

- `server.cpp`: Multithreaded TCP server with a bounded blocking request queue and worker threads
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
./server <port> --workers <n> --queue <size>
```

Example:

```bash
./server 8080
./server 8080 --workers 4 --queue 20
```

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
- Requests are served in FIFO queue order

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

Open more client terminals and request files concurrently to observe multiple workers serving requests.

## Socket APIs Used

- `socket()`
- `bind()`
- `listen()`
- `accept()`
- `connect()`
- `send()`
- `recv()`
- `close()`
