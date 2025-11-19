#include "trie.h"

TrieNode* create_trie_node() {
    TrieNode *node = malloc(sizeof(TrieNode));
    node->is_end_of_word = 0;
    node->file_meta = NULL;
    for (int i = 0; i < ALPHABET_SIZE; i++) {
        node->children[i] = NULL;
    }
    return node;
}

Trie* init_trie() {
    Trie *trie = malloc(sizeof(Trie));
    trie->root = create_trie_node();
    pthread_rwlock_init(&trie->lock, NULL);
    return trie;
}

void free_trie_node(TrieNode *node) {
    if (!node) return;
    
    for (int i = 0; i < ALPHABET_SIZE; i++) {
        if (node->children[i]) {
            free_trie_node(node->children[i]);
        }
    }
    
    if (node->file_meta) {
        free(node->file_meta);
    }
    
    free(node);
}

void free_trie(Trie *trie) {
    if (!trie) return;
    
    free_trie_node(trie->root);
    pthread_rwlock_destroy(&trie->lock);
    free(trie);
}

int trie_insert(Trie *trie, const char *filename, FileMetadata *meta) {
    pthread_rwlock_wrlock(&trie->lock);
    
    TrieNode *current = trie->root;
    
    for (int i = 0; filename[i] != '\0'; i++) {
        int index = (unsigned char)filename[i];
        
        if (index >= ALPHABET_SIZE) {
            pthread_rwlock_unlock(&trie->lock);
            return -1;
        }
        
        if (!current->children[index]) {
            current->children[index] = create_trie_node();
        }
        
        current = current->children[index];
    }
    
    current->is_end_of_word = 1;
    
    // Allocate and copy metadata
    if (!current->file_meta) {
        current->file_meta = malloc(sizeof(FileMetadata));
    }
    memcpy(current->file_meta, meta, sizeof(FileMetadata));
    
    pthread_rwlock_unlock(&trie->lock);
    return 0;
}

FileMetadata* trie_search(Trie *trie, const char *filename) {
    pthread_rwlock_rdlock(&trie->lock);
    
    TrieNode *current = trie->root;
    
    for (int i = 0; filename[i] != '\0'; i++) {
        int index = (unsigned char)filename[i];
        
        if (index >= ALPHABET_SIZE || !current->children[index]) {
            pthread_rwlock_unlock(&trie->lock);
            return NULL;
        }
        
        current = current->children[index];
    }
    
    FileMetadata *result = NULL;
    if (current && current->is_end_of_word && current->file_meta) {
        result = malloc(sizeof(FileMetadata));
        memcpy(result, current->file_meta, sizeof(FileMetadata));
    }
    
    pthread_rwlock_unlock(&trie->lock);
    return result;
}

int trie_delete_helper(TrieNode *node, const char *filename, int depth) {
    if (!node) return 0;
    
    // Base case: reached end of filename
    if (filename[depth] == '\0') {
        if (node->is_end_of_word) {
            node->is_end_of_word = 0;
            if (node->file_meta) {
                free(node->file_meta);
                node->file_meta = NULL;
            }
        }
        
        // Check if node has children
        for (int i = 0; i < ALPHABET_SIZE; i++) {
            if (node->children[i]) return 0;
        }
        
        return 1;  // Can delete this node
    }
    
    int index = (unsigned char)filename[depth];
    if (index >= ALPHABET_SIZE) return 0;
    
    int should_delete = trie_delete_helper(node->children[index], filename, depth + 1);
    
    if (should_delete) {
        free(node->children[index]);
        node->children[index] = NULL;
        
        // Check if current node can also be deleted
        if (!node->is_end_of_word) {
            for (int i = 0; i < ALPHABET_SIZE; i++) {
                if (node->children[i]) return 0;
            }
            return 1;
        }
    }
    
    return 0;
}

int trie_delete(Trie *trie, const char *filename) {
    pthread_rwlock_wrlock(&trie->lock);
    trie_delete_helper(trie->root, filename, 0);
    pthread_rwlock_unlock(&trie->lock);
    return 0;
}

int trie_update(Trie *trie, const char *filename, FileMetadata *meta) {
    pthread_rwlock_wrlock(&trie->lock);
    
    TrieNode *current = trie->root;
    
    for (int i = 0; filename[i] != '\0'; i++) {
        int index = (unsigned char)filename[i];
        
        if (index >= ALPHABET_SIZE || !current->children[index]) {
            pthread_rwlock_unlock(&trie->lock);
            return -1;
        }
        
        current = current->children[index];
    }
    
    if (current && current->is_end_of_word && current->file_meta) {
        memcpy(current->file_meta, meta, sizeof(FileMetadata));
        pthread_rwlock_unlock(&trie->lock);
        return 0;
    }
    
    pthread_rwlock_unlock(&trie->lock);
    return -1;
}

void trie_collect_files(TrieNode *node, FileMetadata **files, int *count, int max_files) {
    if (!node || *count >= max_files) return;
    
    if (node->is_end_of_word && node->file_meta) {
        files[*count] = malloc(sizeof(FileMetadata));
        memcpy(files[*count], node->file_meta, sizeof(FileMetadata));
        (*count)++;
    }
    
    for (int i = 0; i < ALPHABET_SIZE; i++) {
        if (node->children[i]) {
            trie_collect_files(node->children[i], files, count, max_files);
        }
    }
}

int trie_get_all_files(Trie *trie, FileMetadata **files, int max_files) {
    pthread_rwlock_rdlock(&trie->lock);
    
    int count = 0;
    trie_collect_files(trie->root, files, &count, max_files);
    
    pthread_rwlock_unlock(&trie->lock);
    return count;
}

FolderTrie* init_folder_trie() {
    FolderTrie *trie = malloc(sizeof(FolderTrie));
    trie->root = create_trie_node();
    pthread_rwlock_init(&trie->lock, NULL);
    return trie;
}

void free_folder_trie(FolderTrie *trie) {
    if (!trie) return;
    free_trie_node(trie->root);
    pthread_rwlock_destroy(&trie->lock);
    free(trie);
}

int folder_trie_insert(FolderTrie *trie, const char *path, FolderMetadata *meta) {
    pthread_rwlock_wrlock(&trie->lock);
    
    TrieNode *current = trie->root;
    for (int i = 0; path[i] != '\0'; i++) {
        int index = (unsigned char)path[i];
        if (index >= ALPHABET_SIZE) {
            pthread_rwlock_unlock(&trie->lock);
            return -1;
        }
        if (!current->children[index]) {
            current->children[index] = create_trie_node();
        }
        current = current->children[index];
    }
    
    current->is_end_of_word = 1;
    if (!current->file_meta) {
        current->file_meta = malloc(sizeof(FileMetadata));
    }
    // Store folder metadata in file_meta (we'll reuse the structure)
    memcpy(current->file_meta, meta, sizeof(FolderMetadata));
    
    pthread_rwlock_unlock(&trie->lock);
    return 0;
}

FolderMetadata* folder_trie_search(FolderTrie *trie, const char *path) {
    pthread_rwlock_rdlock(&trie->lock);
    
    TrieNode *current = trie->root;
    for (int i = 0; path[i] != '\0'; i++) {
        int index = (unsigned char)path[i];
        if (index >= ALPHABET_SIZE || !current->children[index]) {
            pthread_rwlock_unlock(&trie->lock);
            return NULL;
        }
        current = current->children[index];
    }
    
    FolderMetadata *result = NULL;
    if (current && current->is_end_of_word && current->file_meta) {
        result = malloc(sizeof(FolderMetadata));
        memcpy(result, current->file_meta, sizeof(FolderMetadata));
    }
    
    pthread_rwlock_unlock(&trie->lock);
    return result;
}

int folder_trie_delete(FolderTrie *trie, const char *path) {
    pthread_rwlock_wrlock(&trie->lock);
    trie_delete_helper(trie->root, path, 0);
    pthread_rwlock_unlock(&trie->lock);
    return 0;
}
