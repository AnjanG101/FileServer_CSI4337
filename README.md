# FileServer_CSI4337

Phase 1 implements a minimal TCP file transfer system in C++ for Linux using blocking sockets. The project consists of one server and one client with a single client-to-server interaction per run.

## Phase 1 Scope

- No threads
- No queue
- No cache
- No scheduling
- One client connects to one server

## Files

- `server.cpp`: Accepts one TCP connection, receives a filename, and sends back the file contents or an error message
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

Example:

```bash
./server 8080
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

After responding, the server closes the connection.

## Socket APIs Used

- `socket()`
- `bind()`
- `listen()`
- `accept()`
- `connect()`
- `send()`
- `recv()`
- `close()`
