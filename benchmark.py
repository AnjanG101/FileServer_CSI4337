#!/usr/bin/env python3

import argparse
import random
import socket
import statistics
import sys
import threading
import time
from concurrent.futures import ThreadPoolExecutor, as_completed


def fetch_file(host: str, port: int, filename: str) -> tuple[float, int]:
    start = time.perf_counter()
    with socket.create_connection((host, port)) as sock:
        sock.sendall(filename.encode())
        total_bytes = 0
        while True:
            chunk = sock.recv(4096)
            if not chunk:
                break
            total_bytes += len(chunk)
    elapsed_ms = (time.perf_counter() - start) * 1000.0
    return elapsed_ms, total_bytes


def build_request_list(total_requests: int, files: list[str]) -> list[str]:
    return [random.choice(files) for _ in range(total_requests)]


def main() -> int:
    parser = argparse.ArgumentParser(description="Benchmark the TCP file server.")
    parser.add_argument("--host", default="127.0.0.1", help="Server host. Default: 127.0.0.1")
    parser.add_argument("--port", required=True, type=int, help="Server port")
    parser.add_argument("--requests", required=True, type=int, help="Total request count")
    parser.add_argument("--concurrency", required=True, type=int, help="Concurrent client count")
    parser.add_argument("--files", nargs="+", required=True, help="Files to request")
    args = parser.parse_args()

    if args.port <= 0 or args.port > 65535:
        print("Port must be between 1 and 65535", file=sys.stderr)
        return 1
    if args.requests <= 0:
        print("Request count must be positive", file=sys.stderr)
        return 1
    if args.concurrency <= 0:
        print("Concurrency must be positive", file=sys.stderr)
        return 1
    if not args.files:
        print("At least one filename is required", file=sys.stderr)
        return 1

    request_files = build_request_list(args.requests, args.files)
    response_times: list[float] = []
    total_bytes = 0
    failures = 0
    response_lock = threading.Lock()

    started = time.perf_counter()
    with ThreadPoolExecutor(max_workers=args.concurrency) as executor:
        futures = [executor.submit(fetch_file, args.host, args.port, filename) for filename in request_files]
        for future in as_completed(futures):
            try:
                elapsed_ms, bytes_received = future.result()
            except OSError as exc:
                failures += 1
                print(f"Request failed: {exc}", file=sys.stderr)
                continue

            with response_lock:
                response_times.append(elapsed_ms)
                total_bytes += bytes_received

    total_time = time.perf_counter() - started
    completed = len(response_times)
    avg_response = statistics.fmean(response_times) if response_times else 0.0
    throughput = completed / total_time if total_time > 0 else 0.0

    print("Benchmark results")
    print(f"Host: {args.host}")
    print(f"Port: {args.port}")
    print(f"Requests attempted: {args.requests}")
    print(f"Requests completed: {completed}")
    print(f"Requests failed: {failures}")
    print(f"Concurrency: {args.concurrency}")
    print(f"Files: {', '.join(args.files)}")
    print(f"Total time: {total_time:.3f} s")
    print(f"Average response time: {avg_response:.2f} ms")
    print(f"Throughput: {throughput:.2f} requests/sec")
    print(f"Total bytes received: {total_bytes}")

    return 0 if failures == 0 else 2


if __name__ == "__main__":
    raise SystemExit(main())
