CC = gcc
CFLAGS = -Wall -Wextra -Wno-format-truncation -pthread -g
LDFLAGS = -pthread

# Object files for common modules
COMMON_OBJS = common.o logger.o file_ops.o cache.o trie.o

# Targets
all: nm ss client

# Name Server
nm: nm.o $(COMMON_OBJS)
	$(CC) $(LDFLAGS) -o nm nm.o $(COMMON_OBJS)

# Storage Server
ss: ss.o $(COMMON_OBJS)
	$(CC) $(LDFLAGS) -o ss ss.o $(COMMON_OBJS)

# Client
client: client.o common.o logger.o
	$(CC) $(LDFLAGS) -o client client.o common.o logger.o

# Object files
nm.o: nm.c common.h logger.h trie.h cache.h
	$(CC) $(CFLAGS) -c nm.c

ss.o: ss.c common.h logger.h file_ops.h
	$(CC) $(CFLAGS) -c ss.c

client.o: client.c common.h
	$(CC) $(CFLAGS) -c client.c

common.o: common.c common.h
	$(CC) $(CFLAGS) -c common.c

logger.o: logger.c logger.h common.h
	$(CC) $(CFLAGS) -c logger.c

file_ops.o: file_ops.c file_ops.h common.h
	$(CC) $(CFLAGS) -c file_ops.c

cache.o: cache.c cache.h common.h
	$(CC) $(CFLAGS) -c cache.c

trie.o: trie.c trie.h common.h
	$(CC) $(CFLAGS) -c trie.c

# Clean
clean:
	rm -f *.o nm ss client *.txt 
	rm -f *.log
	rm -rf ss_storage_*

# Run targets
run-nm:
	./nm

run-ss:
	./ss 127.0.0.1 8080 $(PORT)

run-client:
	./client

.PHONY: all clean run-nm run-ss run-client
