#ifndef FILE_OPS_H
#define FILE_OPS_H

#include "common.h"

// Sentence structure
typedef struct {
    char **words;
    int word_count;
    int capacity;
} Sentence;

// File content structure
typedef struct {
    Sentence *sentences;
    int sentence_count;
    int capacity;
} FileContent;

// Initialize file content
FileContent* init_file_content();

// Free file content
void free_file_content(FileContent *fc);

// Parse file into sentences and words
int parse_file(const char *filepath, FileContent *fc);

// Write file content back to disk
int write_file_content(const char *filepath, FileContent *fc);

// Get file content as string
char* file_content_to_string(FileContent *fc);

// Insert word at position in sentence
// Returns: number of new sentences created (due to delimiters)
int insert_word_in_sentence(FileContent *fc, int sent_idx, int word_idx, const char *word);

// Check if character is a sentence delimiter
int is_delimiter(char c);

// Split word if it contains delimiters
// Returns array of words and count
char** split_by_delimiters(const char *word, int *count);

// Get file statistics
void get_file_stats(const char *filepath, int *word_count, int *char_count);

// Create backup for undo
int create_undo_backup(const char *filepath);

// Restore from undo backup
int restore_from_undo(const char *filepath);

// Check if undo backup exists
int undo_backup_exists(const char *filepath);

// Checkpoint functions
int create_checkpoint(const char *filepath, const char *tag);
int list_checkpoints(const char *filepath, char *buffer, int buffer_size);
int view_checkpoint(const char *filepath, const char *tag, char *buffer, int buffer_size);
int revert_to_checkpoint(const char *filepath, const char *tag);

#endif // FILE_OPS_H
