#include "cache.h"

unsigned int hash_string(const char *str, int capacity) {
    unsigned int hash = 5381;
    int c;
    while ((c = *str++)) {
        hash = ((hash << 5) + hash) + c;
    }
    return hash % capacity;
}

CacheNode* create_cache_node(const char *key, FileMetadata *value) {
    CacheNode *node = malloc(sizeof(CacheNode));
    strncpy(node->key, key, MAX_FILENAME - 1);
    node->key[MAX_FILENAME - 1] = '\0';
    node->value = malloc(sizeof(FileMetadata));
    memcpy(node->value, value, sizeof(FileMetadata));
    node->prev = NULL;
    node->next = NULL;
    return node;
}

LRUCache* init_cache(int capacity) {
    LRUCache *cache = malloc(sizeof(LRUCache));
    cache->capacity = capacity;
    cache->size = 0;
    
    cache->head = malloc(sizeof(CacheNode));
    cache->tail = malloc(sizeof(CacheNode));
    cache->head->next = cache->tail;
    cache->tail->prev = cache->head;
    cache->head->prev = NULL;
    cache->tail->next = NULL;
    
    cache->hash_table = calloc(capacity, sizeof(CacheNode*));
    pthread_mutex_init(&cache->lock, NULL);
    
    return cache;
}

void remove_node(CacheNode *node) {
    node->prev->next = node->next;
    node->next->prev = node->prev;
}

void add_to_head(LRUCache *cache, CacheNode *node) {
    node->next = cache->head->next;
    node->prev = cache->head;
    cache->head->next->prev = node;
    cache->head->next = node;
}

void move_to_head(LRUCache *cache, CacheNode *node) {
    remove_node(node);
    add_to_head(cache, node);
}

CacheNode* remove_tail(LRUCache *cache) {
    CacheNode *node = cache->tail->prev;
    remove_node(node);
    return node;
}

FileMetadata* cache_get(LRUCache *cache, const char *key) {
    pthread_mutex_lock(&cache->lock);
    
    unsigned int hash = hash_string(key, cache->capacity);
    CacheNode *node = cache->hash_table[hash];
    
    // Linear probing for collision resolution
    while (node != NULL) {
        if (strcmp(node->key, key) == 0) {
            move_to_head(cache, node);
            FileMetadata *result = malloc(sizeof(FileMetadata));
            memcpy(result, node->value, sizeof(FileMetadata));
            pthread_mutex_unlock(&cache->lock);
            return result;
        }
        
        // Check next slot
        hash = (hash + 1) % cache->capacity;
        node = cache->hash_table[hash];
    }
    
    pthread_mutex_unlock(&cache->lock);
    return NULL;
}

void cache_put(LRUCache *cache, const char *key, FileMetadata *value) {
    pthread_mutex_lock(&cache->lock);
    
    unsigned int hash = hash_string(key, cache->capacity);
    unsigned int original_hash = hash;
    
    // Check if key exists
    CacheNode *node = cache->hash_table[hash];
    while (node != NULL) {
        if (strcmp(node->key, key) == 0) {
            // Update existing
            memcpy(node->value, value, sizeof(FileMetadata));
            move_to_head(cache, node);
            pthread_mutex_unlock(&cache->lock);
            return;
        }
        hash = (hash + 1) % cache->capacity;
        if (hash == original_hash) break;  // Full circle
        node = cache->hash_table[hash];
    }
    
    // Key doesn't exist, create new node
    node = create_cache_node(key, value);
    
    // Find empty slot
    hash = hash_string(key, cache->capacity);
    while (cache->hash_table[hash] != NULL) {
        hash = (hash + 1) % cache->capacity;
    }
    
    cache->hash_table[hash] = node;
    add_to_head(cache, node);
    cache->size++;
    
    // Evict if over capacity
    if (cache->size > cache->capacity) {
        CacheNode *tail = remove_tail(cache);
        
        // Remove from hash table
        unsigned int tail_hash = hash_string(tail->key, cache->capacity);
        while (cache->hash_table[tail_hash] != tail) {
            tail_hash = (tail_hash + 1) % cache->capacity;
        }
        cache->hash_table[tail_hash] = NULL;
        
        free(tail->value);
        free(tail);
        cache->size--;
    }
    
    pthread_mutex_unlock(&cache->lock);
}

void cache_remove(LRUCache *cache, const char *key) {
    pthread_mutex_lock(&cache->lock);
    
    unsigned int hash = hash_string(key, cache->capacity);
    CacheNode *node = cache->hash_table[hash];
    
    while (node != NULL) {
        if (strcmp(node->key, key) == 0) {
            cache->hash_table[hash] = NULL;
            remove_node(node);
            free(node->value);
            free(node);
            cache->size--;
            pthread_mutex_unlock(&cache->lock);
            return;
        }
        hash = (hash + 1) % cache->capacity;
        node = cache->hash_table[hash];
    }
    
    pthread_mutex_unlock(&cache->lock);
}

void cache_clear(LRUCache *cache) {
    pthread_mutex_lock(&cache->lock);
    
    CacheNode *current = cache->head->next;
    while (current != cache->tail) {
        CacheNode *next = current->next;
        free(current->value);
        free(current);
        current = next;
    }
    
    cache->head->next = cache->tail;
    cache->tail->prev = cache->head;
    memset(cache->hash_table, 0, cache->capacity * sizeof(CacheNode*));
    cache->size = 0;
    
    pthread_mutex_unlock(&cache->lock);
}

void free_cache(LRUCache *cache) {
    if (!cache) return;
    
    cache_clear(cache);
    free(cache->head);
    free(cache->tail);
    free(cache->hash_table);
    pthread_mutex_destroy(&cache->lock);
    free(cache);
}
