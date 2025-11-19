#ifndef TRIE_H
#define TRIE_H

#include "common.h"

#define ALPHABET_SIZE 128  // ASCII

typedef struct TrieNode {
    struct TrieNode *children[ALPHABET_SIZE];
    int is_end_of_word;
    FileMetadata *file_meta;  // Only set if is_end_of_word is true
} TrieNode;

typedef struct {
    TrieNode *root;
    pthread_rwlock_t lock;
} Trie;

typedef struct {
    TrieNode *root;
    pthread_rwlock_t lock;
} FolderTrie;

// Add function declarations:
FolderTrie* init_folder_trie();
void free_folder_trie(FolderTrie *trie);
int folder_trie_insert(FolderTrie *trie, const char *path, FolderMetadata *meta);
FolderMetadata* folder_trie_search(FolderTrie *trie, const char *path);
int folder_trie_delete(FolderTrie *trie, const char *path);

// Initialize trie
Trie* init_trie();

// Free trie
void free_trie(Trie *trie);

// Insert file metadata into trie
int trie_insert(Trie *trie, const char *filename, FileMetadata *meta);

// Search for file in trie
FileMetadata* trie_search(Trie *trie, const char *filename);

// Delete file from trie
int trie_delete(Trie *trie, const char *filename);

// Update file metadata in trie
int trie_update(Trie *trie, const char *filename, FileMetadata *meta);

// Get all files (for listing)
int trie_get_all_files(Trie *trie, FileMetadata **files, int max_files);

#endif // TRIE_H
