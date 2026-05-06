CXX = g++
CXXFLAGS = -std=c++17 -Wall -Wextra -pedantic -pthread
SERVER_OBJS = server.o request_queue.o file_cache.o server_logger.o

all: server client

server: $(SERVER_OBJS)
	$(CXX) $(CXXFLAGS) $(SERVER_OBJS) -o server

server.o: src/server.cpp src/request_queue.h src/file_cache.h src/server_logger.h
	$(CXX) $(CXXFLAGS) -c src/server.cpp -o server.o

request_queue.o: src/request_queue.cpp src/request_queue.h
	$(CXX) $(CXXFLAGS) -c src/request_queue.cpp -o request_queue.o

file_cache.o: src/file_cache.cpp src/file_cache.h
	$(CXX) $(CXXFLAGS) -c src/file_cache.cpp -o file_cache.o

server_logger.o: src/server_logger.cpp src/server_logger.h src/request_queue.h
	$(CXX) $(CXXFLAGS) -c src/server_logger.cpp -o server_logger.o

client: src/client.cpp
	$(CXX) $(CXXFLAGS) src/client.cpp -o client

clean:
	rm -f server client *.o
