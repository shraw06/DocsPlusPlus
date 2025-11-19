#ifndef COMMON_H
#define COMMON_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <time.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <dirent.h>
#include <signal.h>

// Constants
#define MAX_FILENAME 256
#define MAX_USERNAME 64
#define MAX_PATH 512
#define MAX_BUFFER 8192
#define MAX_WORD 128
#define MAX_SENTENCE 4096
#define MAX_FILES 10000
#define MAX_CLIENTS 100
#define MAX_SS 50
#define MAX_ACL_ENTRIES 100
#define CACHE_SIZE 100
#define STREAM_DELAY 100000  // 0.1 seconds in microseconds

// File System Limits added, lets tune it! - N
#define MAX_WORDS_PER_SENTENCE 10
#define MAX_SENTENCES 1000
#define SENTENCE_CAPACITY 10

// Error Codes
#define SUCCESS 200
#define ERR_FILE_NOT_FOUND 404
#define ERR_ACCESS_DENIED 403
#define ERR_SENTENCE_LOCKED 423
#define ERR_INVALID_INDEX 400
#define ERR_FILE_EXISTS 409
#define ERR_SS_UNAVAILABLE 503
#define ERR_INVALID_OPERATION 405
#define ERR_SERVER_ERROR 500
#define ERR_NOT_OWNER 401
#define ERR_USER_NOT_FOUND 406
#define ERR_FILE_LOCKED 424 

// Ports
#define NM_SS_PORT 8080          // Existing - commands
#define NM_SS_HB_PORT 8082       // NEW - heartbeats only
#define NM_CLIENT_PORT 8081      // Existing

// Message Types
typedef enum {
    MSG_REG_SS,           // Storage Server Registration
    MSG_REG_CLIENT,       // Client Registration
    MSG_CREATE,           // Create file
    MSG_READ,             // Read file
    MSG_WRITE,            // Write to file
    MSG_DELETE,           // Delete file
    MSG_INFO,             // Get file info
    MSG_VIEW,             // View files
    MSG_LIST,             // List users
    MSG_ADDACCESS,        // Add access
    MSG_REMACCESS,        // Remove access
    MSG_STREAM,           // Stream file
    MSG_EXEC,             // Execute file
    MSG_UNDO,             // Undo changes
    MSG_LOCK_SENTENCE,    // Lock sentence for writing
    MSG_UNLOCK_SENTENCE,  // Unlock sentence
    MSG_ACK,              // Acknowledgment
    MSG_NACK,             // Negative acknowledgment
    MSG_DATA,             // Data transfer
    MSG_ERROR,            // Error message
    MSG_STOP,             // Stop signal
    MSG_CHECK_LOCKS, 
    MSG_CREATEFOLDER,     // Create folder
    MSG_MOVE,             // Move file to folder
    MSG_VIEWFOLDER,       // View folder contents
    MSG_CHECKPOINT,       // Create checkpoint
    MSG_VIEWCHECKPOINT,   // View checkpoint
    MSG_REVERT,           // Revert to checkpoint
    MSG_LISTCHECKPOINTS,  // List checkpoints
    MSG_REQUESTACCESS,    // Request file access
    MSG_VIEWREQUESTS,     // View pending access requests
    MSG_APPROVEREQUEST,   // Approve access request
    MSG_DENYREQUEST,      // Deny access request
    MSG_SS_INFO,           // SS requesting file info
    MSG_CANCEL_WRITE,      // Cancel write session without commiting
    MSG_COMMIT_WRITE      // Explicit commit
} MessageType;

// Access Types
typedef enum {
    ACCESS_NONE = 0,
    ACCESS_READ = 1,
    ACCESS_WRITE = 2,
    ACCESS_READWRITE = 3
} AccessType;

// File Access Control Entry
typedef struct {
    char username[MAX_USERNAME];
    AccessType access;
} ACLEntry;

// File Metadata
typedef struct {
    char filename[MAX_FILENAME];
    char folder_path[MAX_PATH];
    char owner[MAX_USERNAME];
    int ss_id;  // Storage Server ID
    size_t size;
    int word_count;
    int char_count;
    time_t created;
    time_t modified;
    time_t accessed;
    char last_accessed_by[MAX_USERNAME];
    ACLEntry acl[MAX_ACL_ENTRIES];
    int acl_count;
} FileMetadata;

typedef struct {
    char foldername[MAX_FILENAME];
    char parent_path[MAX_PATH];  // Full path to parent folder
    char owner[MAX_USERNAME];
    time_t created;
    int ss_id;
    ACLEntry acl[MAX_ACL_ENTRIES];
    int acl_count;
} FolderMetadata;

// Storage Server Info
typedef struct {
    int id;
    char ip[INET_ADDRSTRLEN];
    int nm_port;
    int client_port;
    int sock;        // Command socket (port 8080)
    int hb_sock;     // ADD THIS: Heartbeat socket (port 8082)
    int active;
    time_t last_heartbeat;
    char files[MAX_FILES][MAX_FILENAME];
    int file_count;
} StorageServerInfo;

// Client Info
typedef struct {
    char username[MAX_USERNAME];
    char ip[INET_ADDRSTRLEN];
    int sock;
    time_t connected;
} ClientInfo;

// Message Structure
typedef struct {
    MessageType type;
    int status;
    char sender[MAX_USERNAME];
    char filename[MAX_FILENAME];
    char foldername[MAX_FILENAME];
    char checkpoint_tag[MAX_USERNAME]; 
    char target_path[MAX_PATH];
    char data[MAX_BUFFER];
    int sentence_index;
    int word_index;
    int ss_id;
    int client_port;     // ADD THIS: For SS registration
    int nm_port;         // ADD THIS: For SS registration
    AccessType access;
    char target_user[MAX_USERNAME];
} Message;

// Sentence Lock
typedef struct {
    int locked;
    char locked_by[MAX_USERNAME];
    time_t lock_time;
    pthread_mutex_t mutex;
} SentenceLock;

// Access Request Entry
typedef struct {
    char username[MAX_USERNAME];
    char filename[MAX_FILENAME];
    AccessType requested_access;
    time_t request_time;
} AccessRequest;

typedef struct {
    char username[MAX_USERNAME];
    time_t first_registered;
    time_t last_seen;
    int active_session;      // NEW: 1 if user has active session, 0 otherwise
    int client_sock;         // NEW: Socket of active session (for validation)
} RegisteredUser;

typedef struct CommitQueueEntry {
    char filename[MAX_FILENAME];
    char username[MAX_FILENAME];
    int sentence_idx;
    int original_sentence_count;
    char temp_filepath[MAX_PATH];
    time_t lock_time;
    struct CommitQueueEntry *next;
} CommitQueueEntry;

typedef struct {
    char filename[MAX_FILENAME];
    CommitQueueEntry *head;
    CommitQueueEntry *tail;
    pthread_mutex_t mutex;
} FileCommitQueue;

typedef struct {
    char filename[MAX_FILENAME];
    char username[MAX_FILENAME];
    int sentence_idx;
    char temp_filepath[MAX_PATH];
    int active;
    int original_sentence_count;
    time_t lock_time;  // Track when lock was acquired
} WriteSession;

// Function declarations
void init_message(Message *msg);
int send_message(int sock, Message *msg);
int recv_message(int sock, Message *msg);
void serialize_message(Message *msg, char *buffer);
void deserialize_message(char *buffer, Message *msg);
char* get_timestamp();
void trim_whitespace(char *str);
int set_socket_timeouts(int sock, int send_timeout_sec, int recv_timeout_sec); // Added definition - N


#endif // COMMON_H
