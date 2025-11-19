#ifndef CACHE_H
#define CACHE_H

#include "common.h"

typedef struct CacheNode {
    char key[MAX_FILENAME];
    FileMetadata *value;
    struct CacheNode *prev;
    struct CacheNode *next;
} CacheNode;

typedef struct {
    CacheNode *head;
    CacheNode *tail;
    CacheNode **hash_table;
    int capacity;
    int size;
    pthread_mutex_t lock;
} LRUCache;

// Initialize cache
LRUCache* init_cache(int capacity);

// Free cache
void free_cache(LRUCache *cache);

// Get from cache
FileMetadata* cache_get(LRUCache *cache, const char *key);

// Put into cache
void cache_put(LRUCache *cache, const char *key, FileMetadata *value);

// Remove from cache
void cache_remove(LRUCache *cache, const char *key);

// Clear cache
void cache_clear(LRUCache *cache);

#endif // CACHE_H
