#include "common.h"
#include "logger.h"
#include "file_ops.h"
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <ifaddrs.h>
#include <sys/time.h>
#include <time.h>
#include <utime.h>

#define SS_STORAGE_DIR "./ss_storage"
#define HEARTBEAT_INTERVAL 5
#define SENTENCE_CAPACITY 10
#define SOCKET_TIMEOUT 10  

static pthread_mutex_t nm_comm_mutex = PTHREAD_MUTEX_INITIALIZER; // Newly added to deal with heartbeats - N

typedef struct {
    int id;
    char ip[INET_ADDRSTRLEN];
    int nm_port;
    int client_port;
    int nm_sock;                 // NEW - command socket to NM - N
    int nm_hb_sock;              // NEW - for heartbeats - N
    int client_sock;
    char storage_path[MAX_PATH];
    
    pthread_mutex_t locks_mutex;
    struct {
        char filename[MAX_FILENAME];
        SentenceLock *locks;
        int lock_count;
    } file_locks[MAX_FILES];
    int file_lock_count;
    
    volatile int running;
} StorageServer;

WriteSession write_sessions[MAX_CLIENTS*10];
int write_session_count = 0;
pthread_mutex_t write_sessions_mutex = PTHREAD_MUTEX_INITIALIZER;

#define MAX_FILE_QUEUES 100
FileCommitQueue commit_queues[MAX_FILE_QUEUES];
int commit_queue_count = 0;
pthread_mutex_t commit_queues_mutex = PTHREAD_MUTEX_INITIALIZER;

StorageServer ss;

void* handle_nm_communication(void* arg);
void* handle_client_request(void* arg);
void* client_listener(void* arg);
void* heartbeat_thread(void* arg);
void scan_and_register_files();
int create_file_ss(const char *filename);
int delete_file_ss(const char *filename);
int read_file_ss(const char *filename, char *buffer);
int write_file_ss(const char *filename, const char* username, int sent_idx, int word_idx, const char *content);
int stream_file_ss(int client_sock, const char *filename);
int get_file_info_ss(const char *filename, FileMetadata *meta);
SentenceLock* get_sentence_lock(const char *filename, int sentence_idx);
void init_file_locks(const char *filename, int sentence_count);
int lock_sentence_ss(const char *filename, int sent_idx, const char *username);
int unlock_sentence_ss(const char *filename, int sent_idx, const char *username);
WriteSession* get_write_session(const char* filename, const char* username, int sent_idx, int create);
void remove_write_session(const char* filename, const char* username, int sent_idx);
int commit_write_session_ss(const char* filename, const char* username, int sent_idx);
int cancel_write_session_ss(const char* filename, const char* username, int sent_idx);

FileCommitQueue* get_commit_queue(const char *filename) {
    pthread_mutex_lock(&commit_queues_mutex);
    
    // Find existing queue
    for (int i = 0; i < commit_queue_count; i++) {
        if (strcmp(commit_queues[i].filename, filename) == 0) {
            pthread_mutex_unlock(&commit_queues_mutex);
            return &commit_queues[i];
        }
    }
    
    // Create new queue
    if (commit_queue_count >= MAX_FILE_QUEUES) {
        pthread_mutex_unlock(&commit_queues_mutex);
        return NULL;
    }
    
    FileCommitQueue *queue = &commit_queues[commit_queue_count++];
    strcpy(queue->filename, filename);
    queue->head = NULL;
    queue->tail = NULL;
    pthread_mutex_init(&queue->mutex, NULL);
    
    pthread_mutex_unlock(&commit_queues_mutex);
    return queue;
}

// Add commit to queue (sorted by lock_time - FIFO order)
int enqueue_commit(const char *filename, const char *username, int sent_idx, 
                   int original_count, const char *temp_path, time_t lock_time) {
    FileCommitQueue *queue = get_commit_queue(filename);
    if (!queue) return -1;
    
    CommitQueueEntry *entry = malloc(sizeof(CommitQueueEntry));
    strcpy(entry->filename, filename);
    strcpy(entry->username, username);
    entry->sentence_idx = sent_idx;
    entry->original_sentence_count = original_count;
    strcpy(entry->temp_filepath, temp_path);
    entry->lock_time = lock_time;
    entry->next = NULL;
    
    pthread_mutex_lock(&queue->mutex);
    
    if (queue->tail == NULL) {
        queue->head = queue->tail = entry;
    } else {
        queue->tail->next = entry;
        queue->tail = entry;
    }
    
    pthread_mutex_unlock(&queue->mutex);
    
    log_formatted(LOG_INFO, "Enqueued commit for %s by %s (sentence %d, locked at %ld)", 
                  filename, username, sent_idx, lock_time);
    return 0;
}

// Process all pending commits for a file sequentially
int process_commit_queue(const char *filename) {
    FileCommitQueue *queue = get_commit_queue(filename);
    if (!queue) return 0;
    
    pthread_mutex_lock(&queue->mutex);
    
    int processed = 0;
    char filepath[MAX_PATH];
    snprintf(filepath, sizeof(filepath), "%s/%s", ss.storage_path, filename);
    
    // Create initial backup before processing queue
    if (queue->head != NULL) {
        if (create_undo_backup(filepath) != 0) {
            log_formatted(LOG_WARNING, "Could not create undo backup before commit queue processing");
        }
    }
    
    while (queue->head != NULL) {
        CommitQueueEntry *entry = queue->head;
        
        log_formatted(LOG_INFO, "Processing queued commit: %s by %s (sentence %d, original_count=%d)",
                      entry->filename, entry->username, entry->sentence_idx, entry->original_sentence_count);
        
        // Parse CURRENT main file state
        FileContent *main_fc = init_file_content();
        int main_parsed = (parse_file(filepath, main_fc) == 0);
        int current_sentence_count = main_parsed ? main_fc->sentence_count : 0;
        
        // Parse temp file (user's modifications)
        FileContent *temp_fc = init_file_content();
        int temp_parsed = (parse_file(entry->temp_filepath, temp_fc) == 0);
        
        if (!temp_parsed) {
            log_formatted(LOG_ERROR, "Failed to parse temp file, skipping commit");
            free_file_content(main_fc);
            free_file_content(temp_fc);
            
            // Remove from queue and continue
            queue->head = entry->next;
            if (queue->head == NULL) queue->tail = NULL;
            unlink(entry->temp_filepath);
            free(entry);
            continue;
        }
        
        // Calculate sentence mapping
        // When this user locked sentence X, the file had original_sentence_count sentences
        // Now the file has current_sentence_count sentences
        // We need to figure out where sentence X is NOW after previous commits
        
        int sentence_shift = current_sentence_count - entry->original_sentence_count;
        int adjusted_idx = entry->sentence_idx + sentence_shift;
        
        log_formatted(LOG_INFO, "Sentence mapping: original_idx=%d, shift=%d, adjusted_idx=%d (current_count=%d)",
                      entry->sentence_idx, sentence_shift, adjusted_idx, current_sentence_count);
        
        // Validate adjusted index
        // Special case: if both original and current are 0 (empty file), allow idx 0
        if (current_sentence_count == 0 && entry->original_sentence_count == 0 && adjusted_idx == 0) {
            // This is fine - writing to an empty file
            log_formatted(LOG_INFO, "Writing to empty file, adjusted_idx=0 is valid");
        } else if (adjusted_idx < 0 || adjusted_idx >= current_sentence_count) {
            log_formatted(LOG_ERROR, "Adjusted sentence index %d out of bounds (current file has %d sentences), skipping commit",
                          adjusted_idx, current_sentence_count);
            free_file_content(main_fc);
            free_file_content(temp_fc);
            
            queue->head = entry->next;
            if (queue->head == NULL) queue->tail = NULL;
            unlink(entry->temp_filepath);
            free(entry);
            continue;
        }
        
        // Extract modified sentence(s) from temp file
        // The modified sentence is at entry->sentence_idx in the temp file
        int temp_original_count = entry->original_sentence_count;
        int temp_current_count = temp_fc->sentence_count;
        int sentence_expansion = temp_current_count - temp_original_count;
        int modified_sentence_count = 1 + sentence_expansion;
        
        log_formatted(LOG_INFO, "Sentence expansion: temp had %d originally, now has %d, expansion=%d",
                      temp_original_count, temp_current_count, sentence_expansion);
        
        // Build merged content
        int new_total;
        Sentence *new_sentences;
        
        if (current_sentence_count == 0) {
            // Empty file case - just use temp file content directly
            log_formatted(LOG_INFO, "Empty file - using temp file content as-is");
            new_total = temp_fc->sentence_count;
            new_sentences = malloc(sizeof(Sentence) * (new_total > 0 ? new_total : 1));
            if (!new_sentences) {
                log_formatted(LOG_ERROR, "Out of memory during merge");
                free_file_content(main_fc);
                free_file_content(temp_fc);
                pthread_mutex_unlock(&queue->mutex);
                return -1;
            }
            
            // Deep copy all sentences from temp
            for (int i = 0; i < temp_fc->sentence_count; i++) {
                Sentence *src = &temp_fc->sentences[i];
                new_sentences[i].capacity = src->word_count > 10 ? src->word_count : 10;
                new_sentences[i].word_count = src->word_count;
                new_sentences[i].words = malloc(sizeof(char*) * new_sentences[i].capacity);
                
                for (int j = 0; j < src->word_count; j++) {
                    new_sentences[i].words[j] = strdup(src->words[j]);
                }
            }
            
            // Update main file content
            free(main_fc->sentences);
            main_fc->sentences = new_sentences;
            main_fc->sentence_count = new_total;
            main_fc->capacity = new_total;
        } else {
            // Non-empty file - do normal merge
            new_total = current_sentence_count + sentence_expansion;
            new_sentences = malloc(sizeof(Sentence) * (new_total > 0 ? new_total : 1));
            if (!new_sentences) {
                log_formatted(LOG_ERROR, "Out of memory during merge");
                free_file_content(main_fc);
                free_file_content(temp_fc);
                pthread_mutex_unlock(&queue->mutex);
                return -1;
            }
            
            int new_idx = 0;
            
            // Step 1: Copy sentences BEFORE adjusted_idx from current main
            for (int i = 0; i < adjusted_idx && i < current_sentence_count; i++) {
                new_sentences[new_idx++] = main_fc->sentences[i];
            }
            
            // Step 2: Insert modified sentence(s) from temp (deep copy)
            for (int i = 0; i < modified_sentence_count && (entry->sentence_idx + i) < temp_fc->sentence_count; i++) {
                Sentence *src = &temp_fc->sentences[entry->sentence_idx + i];
                
                new_sentences[new_idx].capacity = src->word_count > 10 ? src->word_count : 10;
                new_sentences[new_idx].word_count = src->word_count;
                new_sentences[new_idx].words = malloc(sizeof(char*) * new_sentences[new_idx].capacity);
                
                for (int j = 0; j < src->word_count; j++) {
                    new_sentences[new_idx].words[j] = strdup(src->words[j]);
                }
                new_idx++;
            }
            
            // Step 3: Copy sentences AFTER adjusted_idx from current main
            for (int i = adjusted_idx + 1; i < current_sentence_count; i++) {
                new_sentences[new_idx++] = main_fc->sentences[i];
            }
            
            log_formatted(LOG_INFO, "Merged content: %d sentences (expected %d)", new_idx, new_total);
            
            // Update main file content
            free(main_fc->sentences);
            main_fc->sentences = new_sentences;
            main_fc->sentence_count = new_idx;
            main_fc->capacity = new_total;
        }
        
        // Write back to disk
        if (write_file_content(filepath, main_fc) != 0) {
            log_formatted(LOG_ERROR, "Failed to write merged content");
            free_file_content(main_fc);
            free_file_content(temp_fc);
            pthread_mutex_unlock(&queue->mutex);
            return -1;
        }
        
        // Update timestamps
        struct stat st;
        if (stat(filepath, &st) == 0) {
            struct utimbuf times;
            times.actime = time(NULL);
            times.modtime = time(NULL);
            utime(filepath, &times);
        }
        
        free_file_content(main_fc);
        free_file_content(temp_fc);
        
        // Clean up
        unlink(entry->temp_filepath);
        
        // Remove from queue
        queue->head = entry->next;
        if (queue->head == NULL) queue->tail = NULL;
        free(entry);
        
        processed++;
        log_formatted(LOG_INFO, "Successfully processed commit %d for %s", processed, filename);
    }
    
    pthread_mutex_unlock(&queue->mutex);
    
    log_formatted(LOG_INFO, "Processed %d commits for %s", processed, filename);
    return processed;
}

WriteSession* get_write_session(const char *filename, const char *username, int sent_idx, int create) {
    pthread_mutex_lock(&write_sessions_mutex);
    
    // Search for existing session
    for (int i = 0; i < write_session_count; i++) {
        if (write_sessions[i].active &&
            strcmp(write_sessions[i].filename, filename) == 0 &&
            strcmp(write_sessions[i].username, username) == 0 &&
            write_sessions[i].sentence_idx == sent_idx) {
            pthread_mutex_unlock(&write_sessions_mutex);
            return &write_sessions[i];
        }
    }
    
    // Create new session if requested
    if (create && write_session_count < MAX_CLIENTS * 10) {
        WriteSession *session = &write_sessions[write_session_count];
        strcpy(session->filename, filename);
        strcpy(session->username, username);
        session->sentence_idx = sent_idx;
        
        // Create temp file path: filename.temp_username_sentidx
        snprintf(session->temp_filepath, sizeof(session->temp_filepath), 
                 "%s/%s.temp_%s_%d", ss.storage_path, filename, username, sent_idx);
        
        session->active = 1;
        write_session_count++;
        
        pthread_mutex_unlock(&write_sessions_mutex);
        return session;
    }
    
    pthread_mutex_unlock(&write_sessions_mutex);
    return NULL;
}

void remove_write_session(const char *filename, const char *username, int sent_idx) {
    pthread_mutex_lock(&write_sessions_mutex);
    
    for (int i = 0; i < write_session_count; i++) {
        if (write_sessions[i].active &&
            strcmp(write_sessions[i].filename, filename) == 0 &&
            strcmp(write_sessions[i].username, username) == 0 &&
            write_sessions[i].sentence_idx == sent_idx) {
            
            // Delete temp file
            unlink(write_sessions[i].temp_filepath);
            
            // Mark inactive and compact array
            for (int j = i; j < write_session_count - 1; j++) {
                write_sessions[j] = write_sessions[j + 1];
            }
            write_session_count--;
            break;
        }
    }
    
    pthread_mutex_unlock(&write_sessions_mutex);
}

// Updated start_write_session_ss function
int start_write_session_ss(const char *filename, const char *username, int sent_idx) {
    char filepath[MAX_PATH];
    snprintf(filepath, sizeof(filepath), "%s/%s", ss.storage_path, filename);
    
    // Parse current file to get sentence count
    FileContent *fc = init_file_content();
    int parsed = parse_file(filepath, fc);
    int original_sentence_count = parsed == 0 ? fc->sentence_count : 0;
    free_file_content(fc);
    
    // Create write session
    WriteSession *session = get_write_session(filename, username, sent_idx, 1);
    if (!session) {
        log_formatted(LOG_ERROR, "Failed to create write session");
        return ERR_SERVER_ERROR;
    }
    
    // Store original sentence count and lock time
    session->original_sentence_count = original_sentence_count;
    session->lock_time = time(NULL);
    
    // Copy current file content to temp file
    FILE *src = fopen(filepath, "r");
    if (!src) {
        FILE *temp = fopen(session->temp_filepath, "w");
        if (!temp) {
            remove_write_session(filename, username, sent_idx);
            return ERR_SERVER_ERROR;
        }
        fclose(temp);
        log_formatted(LOG_INFO, "Created empty temp file for write session");
        return SUCCESS;
    }
    
    FILE *temp = fopen(session->temp_filepath, "w");
    if (!temp) {
        fclose(src);
        remove_write_session(filename, username, sent_idx);
        return ERR_SERVER_ERROR;
    }
    
    char buffer[MAX_BUFFER];
    size_t bytes;
    while ((bytes = fread(buffer, 1, sizeof(buffer), src)) > 0) {
        fwrite(buffer, 1, bytes, temp);
    }
    
    fclose(src);
    fclose(temp);
    
    log_formatted(LOG_INFO, "Started write session: %s by %s on sentence %d (file had %d sentences, locked at %ld)", 
                  filename, username, sent_idx, original_sentence_count, session->lock_time);
    return SUCCESS;
}

int commit_write_session_ss(const char *filename, const char *username, int sent_idx) {
    WriteSession *session = get_write_session(filename, username, sent_idx, 0);
    if (!session) {
        log_formatted(LOG_WARNING, "No write session to commit");
        return SUCCESS;
    }
    
    // Enqueue this commit
    int result = enqueue_commit(filename, username, sent_idx, 
                                 session->original_sentence_count,
                                 session->temp_filepath, 
                                 session->lock_time);
    
    if (result != 0) {
        log_formatted(LOG_ERROR, "Failed to enqueue commit");
        remove_write_session(filename, username, sent_idx);
        return ERR_SERVER_ERROR;
    }
    
    // Remove write session (temp file will be cleaned up after queue processing)
    pthread_mutex_lock(&write_sessions_mutex);
    for (int i = 0; i < write_session_count; i++) {
        if (write_sessions[i].active &&
            strcmp(write_sessions[i].filename, filename) == 0 &&
            strcmp(write_sessions[i].username, username) == 0 &&
            write_sessions[i].sentence_idx == sent_idx) {
            
            // Don't delete temp file here - queue will handle it
            // Just mark inactive
            for (int j = i; j < write_session_count - 1; j++) {
                write_sessions[j] = write_sessions[j + 1];
            }
            write_session_count--;
            break;
        }
    }
    pthread_mutex_unlock(&write_sessions_mutex);
    
    // Process the entire commit queue for this file
    process_commit_queue(filename);
    
    log_formatted(LOG_INFO, "Commit queued and processed for %s by %s on sentence %d", 
                  filename, username, sent_idx);
    return SUCCESS;
}

int cancel_write_session_ss(const char *filename, const char *username, int sent_idx) {
    WriteSession *session = get_write_session(filename, username, sent_idx, 0);
    if (!session) {
        return SUCCESS; // Already cancelled
    }
    
    log_formatted(LOG_INFO, "Cancelled write session for %s by %s", filename, username);
    remove_write_session(filename, username, sent_idx);
    return SUCCESS;
}


int check_file_locks(const char *filename) {
    pthread_mutex_lock(&ss.locks_mutex);
    
    // Find the file's lock structure
    for (int i = 0; i < ss.file_lock_count; i++) {
        if (strcmp(ss.file_locks[i].filename, filename) == 0) {
            // Check if any sentence is locked
            for (int j = 0; j < ss.file_locks[i].lock_count; j++) {
                SentenceLock *lock = &ss.file_locks[i].locks[j];
                
                pthread_mutex_lock(&lock->mutex);
                int is_locked = lock->locked;
                pthread_mutex_unlock(&lock->mutex);
                
                if (is_locked) {
                    pthread_mutex_unlock(&ss.locks_mutex);
                    log_formatted(LOG_INFO, "File %s has locked sentence %d", filename, j);
                    return 1;  // File has locked sentences
                }
            }
            break;
        }
    }
    
    pthread_mutex_unlock(&ss.locks_mutex);
    return 0;  // No locks found
}

int get_system_ip(char *ip_buffer, size_t buffer_size) {
    struct ifaddrs *ifaddr, *ifa;
    int found = 0;
    
    if (getifaddrs(&ifaddr) == -1) {
        perror("getifaddrs");
        return -1;
    }
    
    // Iterate through the network interfaces
    for (ifa = ifaddr; ifa != NULL; ifa = ifa->ifa_next) {
        if (ifa->ifa_addr == NULL)
            continue;
        
        // Check for IPv4 address
        if (ifa->ifa_addr->sa_family == AF_INET) {
            struct sockaddr_in *addr = (struct sockaddr_in *)ifa->ifa_addr;
            char *ip = inet_ntoa(addr->sin_addr);
            
            // Skip loopback interface (127.0.0.1)
            if (strcmp(ip, "127.0.0.1") != 0) {
                strncpy(ip_buffer, ip, buffer_size - 1);
                ip_buffer[buffer_size - 1] = '\0';
                found = 1;
                break;
            }
        }
    }
    
    freeifaddrs(ifaddr);
    
    if (!found) {
        // Fallback to loopback if no other interface found
        strncpy(ip_buffer, "127.0.0.1", buffer_size - 1);
        ip_buffer[buffer_size - 1] = '\0';
        return -1;
    }
    
    return 0;
}

void init_storage_server(const char *nm_ip, int nm_port, int client_port, int ss_id) {
    
    if (get_system_ip(ss.ip, sizeof(ss.ip)) != 0) {
        fprintf(stderr, "[SS] Warning: Could not determine system IP, using loopback\n");
    }

    ss.nm_port = nm_port;
    ss.client_port = client_port;
    printf("[SS] System client port: %d\n", ss.client_port);
    ss.id = ss_id;
    ss.running = 1;
    
    snprintf(ss.storage_path, sizeof(ss.storage_path), "%s_%d", SS_STORAGE_DIR, ss.id);
    
    struct stat st = {0};
    if (stat(ss.storage_path, &st) == -1) {
        mkdir(ss.storage_path, 0777);
        printf("[SS %d] Created new storage directory: %s\n", ss.id, ss.storage_path);
    } else {
        printf("[SS %d] Using existing storage directory: %s\n", ss.id, ss.storage_path);        
    }
    
    pthread_mutex_init(&nm_comm_mutex, NULL);
    pthread_mutex_init(&ss.locks_mutex, NULL);
    ss.file_lock_count = 0;

    char instance_name[64];
    snprintf(instance_name, sizeof(instance_name), "SS_%d", ss.id);
    set_instance_name(instance_name);
    
    char log_file[128];
    snprintf(log_file, sizeof(log_file), "ss_%d.log", ss.id);
    init_logger(log_file);
    
    printf("[SS %d] Storage Server initialized\n", ss.id);
    printf("[SS %d] Connecting to Name Server at %s:%d\n", ss.id, nm_ip, nm_port);
    printf("[SS %d] Storage path: %s\n", ss.id, ss.storage_path);
    printf("[SS %d] Client port: %d\n", ss.id, ss.client_port);
}

// Added doe to setup socket options for NM communication, might be unnecessary - N
void setup_nm_socket_options() {
    // Enable TCP keepalive to detect dead connections
    int keepalive = 1;
    setsockopt(ss.nm_sock, SOL_SOCKET, SO_KEEPALIVE, &keepalive, sizeof(keepalive));
    
    // Set keepalive parameters (Linux-specific)
    #ifdef __linux__
    int keepidle = 10;   // Start probes after 10 seconds of idle
    int keepintvl = 5;   // Send probes every 5 seconds
    int keepcnt = 3;     // Close after 3 failed probes
    setsockopt(ss.nm_sock, IPPROTO_TCP, TCP_KEEPIDLE, &keepidle, sizeof(keepidle));
    setsockopt(ss.nm_sock, IPPROTO_TCP, TCP_KEEPINTVL, &keepintvl, sizeof(keepintvl));
    setsockopt(ss.nm_sock, IPPROTO_TCP, TCP_KEEPCNT, &keepcnt, sizeof(keepcnt));
    #endif
    
    log_formatted(LOG_INFO, "Socket keepalive configured");
}

void connect_to_nm(const char *nm_ip, int nm_port) {
    // Command socket
    ss.nm_sock = socket(AF_INET, SOCK_STREAM, 0);
    if (ss.nm_sock < 0) {
        perror("Socket creation failed");
        exit(1);
    }
    
    struct sockaddr_in nm_addr;
    nm_addr.sin_family = AF_INET;
    nm_addr.sin_port = htons(nm_port);
    inet_pton(AF_INET, nm_ip, &nm_addr.sin_addr);
    
    if (connect(ss.nm_sock, (struct sockaddr*)&nm_addr, sizeof(nm_addr)) < 0) {
        perror("Connection to NM failed");
        exit(1);
    }
    setup_nm_socket_options();
    
    // Heartbeat socket configs - N
    ss.nm_hb_sock = socket(AF_INET, SOCK_STREAM, 0);
    if (ss.nm_hb_sock < 0) {
        perror("Heartbeat socket creation failed");
        exit(1);
    }
    
    struct sockaddr_in hb_addr;
    hb_addr.sin_family = AF_INET;
    hb_addr.sin_port = htons(NM_SS_HB_PORT);
    inet_pton(AF_INET, nm_ip, &hb_addr.sin_addr);
    
    // Start collecting heartbeats from this - N
    if (connect(ss.nm_hb_sock, (struct sockaddr*)&hb_addr, sizeof(hb_addr)) < 0) {
        perror("Connection to NM heartbeat port failed");
        close(ss.nm_sock);
        exit(1);
    }
    
    printf("[SS %d] Connected to Name Server at %s:%d (cmd) and %s:%d (hb)\n", 
           ss.id, nm_ip, nm_port, nm_ip, NM_SS_HB_PORT);
    log_formatted(LOG_INFO, "Connected to NM at %s:%d (cmd) and %s:%d (hb)", 
                  nm_ip, nm_port, nm_ip, NM_SS_HB_PORT);
}

void scan_and_register_files() {
    DIR *dir = opendir(ss.storage_path);
    if (!dir) {
        log_formatted(LOG_ERROR, "Cannot open storage directory");
        return;
    }
    
    Message msg;
    init_message(&msg);
    msg.type = MSG_REG_SS;
    msg.ss_id = ss.id;
    strcpy(msg.sender, ss.ip);
    msg.client_port = ss.client_port;  // Use new fields - N
    printf("[SS %d] Registering with NM: IP=%s, Client Port=%d\n", 
           ss.id, ss.ip, ss.client_port);
    msg.nm_port = ss.nm_port;       // Use new fields - N
    
    struct dirent *entry;
    char file_list[MAX_BUFFER] = "";
    int file_count = 0;
    
    while ((entry = readdir(dir)) != NULL) {
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
            continue;
        }
        
        if (strstr(entry->d_name, ".undo") != NULL) continue;
        
        char filepath[MAX_PATH];
        snprintf(filepath, sizeof(filepath), "%s/%s", ss.storage_path, entry->d_name);
        
        struct stat st;
        if (stat(filepath, &st) == 0 && S_ISREG(st.st_mode)) {
            if (file_count > 0) strcat(file_list, ",");
            strcat(file_list, entry->d_name);
            file_count++;
        }
    }
    closedir(dir);
    
    strcpy(msg.data, file_list);
    
    send_message(ss.nm_sock, &msg);
    log_formatted(LOG_INFO, "Registered %d files with NM", file_count);
    printf("[SS %d] Registered %d files with NM\n", ss.id, file_count);
}

void init_file_locks(const char *filename, int sentence_count) {
    if (sentence_count == 0) sentence_count = 1;
    pthread_mutex_lock(&ss.locks_mutex);
    
    for (int i = 0; i < ss.file_lock_count; i++) {
        if (strcmp(ss.file_locks[i].filename, filename) == 0) {
            if (ss.file_locks[i].lock_count < sentence_count) {
                ss.file_locks[i].locks = realloc(ss.file_locks[i].locks, 
                                                  sizeof(SentenceLock) * sentence_count);
                for (int j = ss.file_locks[i].lock_count; j < sentence_count; j++) {
                    ss.file_locks[i].locks[j].locked = 0;
                    pthread_mutex_init(&ss.file_locks[i].locks[j].mutex, NULL);
                }
                ss.file_locks[i].lock_count = sentence_count;
            }
            pthread_mutex_unlock(&ss.locks_mutex);
            return;
        }
    }
    
    strcpy(ss.file_locks[ss.file_lock_count].filename, filename);
    ss.file_locks[ss.file_lock_count].locks = malloc(sizeof(SentenceLock) * sentence_count);
    ss.file_locks[ss.file_lock_count].lock_count = sentence_count;
    
    for (int i = 0; i < sentence_count; i++) {
        ss.file_locks[ss.file_lock_count].locks[i].locked = 0;
        ss.file_locks[ss.file_lock_count].locks[i].locked_by[0] = '\0';
        pthread_mutex_init(&ss.file_locks[ss.file_lock_count].locks[i].mutex, NULL);
    }
    
    ss.file_lock_count++;
    pthread_mutex_unlock(&ss.locks_mutex);
}

SentenceLock* get_sentence_lock(const char *filename, int sentence_idx) {
    pthread_mutex_lock(&ss.locks_mutex);
    for (int i = 0; i < ss.file_lock_count; i++) {
        if (strcmp(ss.file_locks[i].filename, filename) == 0) {
            if (sentence_idx >= 0 && sentence_idx < ss.file_locks[i].lock_count) {
                SentenceLock *lock = &ss.file_locks[i].locks[sentence_idx];
                pthread_mutex_unlock(&ss.locks_mutex);
                return lock;
            }
            break;
        }
    }
    
    pthread_mutex_unlock(&ss.locks_mutex);
    return NULL;
}

int create_file_ss(const char *filename) {
    char filepath[MAX_PATH];
    snprintf(filepath, sizeof(filepath), "%s/%s", ss.storage_path, filename);
    
    if (access(filepath, F_OK) == 0) {
        return ERR_FILE_EXISTS;
    }
    
    FILE *file = fopen(filepath, "w");
    if (!file) {
        return ERR_SERVER_ERROR;
    }
    fclose(file);
    
    init_file_locks(filename, 1);
    
    log_formatted(LOG_INFO, "Created file: %s", filename);
    return SUCCESS;
}

int delete_file_ss(const char *filename) {
    char filepath[MAX_PATH];
    snprintf(filepath, sizeof(filepath), "%s/%s", ss.storage_path, filename);
    
    if (unlink(filepath) != 0) {
        return ERR_FILE_NOT_FOUND;
    }
    
    char undo_path[MAX_PATH];
    snprintf(undo_path, sizeof(undo_path), "%s.undo", filepath);
    unlink(undo_path);
    
    log_formatted(LOG_INFO, "Deleted file: %s", filename);
    return SUCCESS;
}

int read_file_ss(const char *filename, char *buffer) {
    char filepath[MAX_PATH];
    snprintf(filepath, sizeof(filepath), "%s/%s", ss.storage_path, filename);
    
    FILE *file = fopen(filepath, "r");
    if (!file) {
        return ERR_FILE_NOT_FOUND;
    }
    
    size_t bytes_read = fread(buffer, 1, MAX_BUFFER - 1, file);
    buffer[bytes_read] = '\0';
    
    fclose(file);

    struct stat st;
    if (stat(filepath, &st) == 0) {
        struct utimbuf times;
        times.actime = time(NULL);   // Update access time
        times.modtime = st.st_mtime; // Keep modification time unchanged
        utime(filepath, &times);
    }

    return SUCCESS;
}

int write_file_ss(const char *filename, const char* username, int sent_idx, int word_idx, const char *content) {
     // Get the active write session
    WriteSession *session = get_write_session(filename, username, sent_idx, 0);
    if (!session) {
        log_formatted(LOG_ERROR, "No active write session for %s by %s", filename, username);
        return ERR_INVALID_OPERATION;
    }

    log_formatted(LOG_DEBUG, "Write to temp: file=%s, sent=%d, word=%d, content='%s'", 
                 filename, sent_idx, word_idx, content);
    
    // we probabaly will need this for writes within folders - S
    // char filepath[MAX_PATH];
    // snprintf(filepath, sizeof(filepath), "%s/%s", ss.storage_path, filename);
    
    log_formatted(LOG_DEBUG, "Write to temp: file=%s, sent=%d, word=%d, content='%s'", 
                 filename, sent_idx, word_idx, content);
    
    // if (create_undo_backup(filepath) != 0) {
    //     log_formatted(LOG_WARNING, "Could not create undo backup (file might be empty)");
    // }
    
    FileContent *fc = init_file_content();
    if (parse_file(session->temp_filepath, fc) != 0) {
        log_formatted(LOG_WARNING, "Could not parse temp file, treating as empty");
        fc->sentence_count = 1;
        fc->sentences[0].capacity = SENTENCE_CAPACITY;
        fc->sentences[0].word_count = 0;
        fc->sentences[0].words = malloc(sizeof(char*) * fc->sentences[0].capacity);
    }

    // printf("File has %d sentences before insertion\n", fc->sentence_count);
    if (fc->sentence_count == 0) {
        log_formatted(LOG_DEBUG, "Temp file is empty, initializing with one sentence");
        fc->sentence_count = 1;
        fc->sentences[0].capacity = SENTENCE_CAPACITY;
        fc->sentences[0].word_count = 0;
        fc->sentences[0].words = malloc(sizeof(char*) * fc->sentences[0].capacity);
    }
    
    //log_formatted(LOG_DEBUG, "File has %d sentences before insertion", fc->sentence_count);
    
    if (sent_idx < 0 || sent_idx > fc->sentence_count) {
        log_formatted(LOG_ERROR, "Invalid sentence index: %d (file has %d sentences)", 
                     sent_idx, fc->sentence_count);
        free_file_content(fc);
        return ERR_INVALID_INDEX;
    }
    
    int words_in_sentence = fc->sentences[sent_idx].word_count;
    log_formatted(LOG_DEBUG, "Sentence %d has %d words, inserting at position %d", 
                 sent_idx, words_in_sentence, word_idx);
    
    int new_sentences = insert_word_in_sentence(fc, sent_idx, word_idx, content);
    if (new_sentences < 0) {
        log_formatted(LOG_ERROR, "Failed to insert word '%s' at sentence %d, word index %d in temp file"
                     "(sentence had %d words, valid range: 1-%d)", 
                     content, sent_idx, word_idx, words_in_sentence, words_in_sentence + 1);
        free_file_content(fc);
        return ERR_INVALID_INDEX;
    }
    
    // log_formatted(LOG_DEBUG, "Insertion successful, %d new sentences created, file now has %d sentences", 
    //              new_sentences, fc->sentence_count);
    
    //we probabbly will need this for expanding locks - S
    // if (new_sentences > 0) {
    //     log_formatted(LOG_DEBUG, "Expanding locks to %d sentences", fc->sentence_count);
    //     init_file_locks(filename, fc->sentence_count);
    // }
    
    if (write_file_content(session->temp_filepath, fc) != 0) {
        log_formatted(LOG_ERROR, "Failed to write temp file content back to disk");
        free_file_content(fc);
        return ERR_SERVER_ERROR;
    }

    //will probably need this to update timestamps (but for tempfile??) -- taken care in commit_write_session_ss - S
    // struct stat st;

    // if (stat(filepath, &st) == 0) {
    //     struct utimbuf times;
    //     times.actime = time(NULL);   // Update access time
    //     times.modtime = time(NULL); // Keep modification time unchanged
    //     utime(filepath, &times);
    // }
    
    free_file_content(fc);
    log_formatted(LOG_INFO, "Successfully wrote to temp file: %s at sentence %d, word %d", 
                 session->temp_filepath, sent_idx, word_idx);
    return SUCCESS;
}

int lock_sentence_ss(const char *filename, int sent_idx, const char *username) {
    init_file_locks(filename, sent_idx + 1); // Ensure locks are initialized (so that when ss initialises again, it has locks).
    //the init_file_locks properly handles the case when locks already exist. - S
    SentenceLock *lock = get_sentence_lock(filename, sent_idx);
    if (!lock) {
        return ERR_INVALID_INDEX;
    }
    
    pthread_mutex_lock(&lock->mutex);
    
    if (lock->locked && strcmp(lock->locked_by, username) != 0) {
        pthread_mutex_unlock(&lock->mutex);
        return ERR_SENTENCE_LOCKED;
    }
    
    lock->locked = 1;
    strncpy(lock->locked_by, username, MAX_USERNAME - 1);
    lock->lock_time = time(NULL);
    
    pthread_mutex_unlock(&lock->mutex);
    log_formatted(LOG_INFO, "Locked sentence %d in %s by %s", sent_idx, filename, username);
    return SUCCESS;
}

int unlock_sentence_ss(const char *filename, int sent_idx, const char *username) {
    SentenceLock *lock = get_sentence_lock(filename, sent_idx);
    if (!lock) {
        return ERR_INVALID_INDEX;
    }
    
    pthread_mutex_lock(&lock->mutex);
    
    if (!lock->locked || strcmp(lock->locked_by, username) != 0) {
        pthread_mutex_unlock(&lock->mutex);
        return ERR_ACCESS_DENIED;
    }
    
    lock->locked = 0;
    lock->locked_by[0] = '\0';
    
    pthread_mutex_unlock(&lock->mutex);
    log_formatted(LOG_INFO, "Unlocked sentence %d in %s by %s", sent_idx, filename, username);
    return SUCCESS;
}

int create_folder_ss(const char *folder_path) {
    char full_path[MAX_PATH];
    snprintf(full_path, sizeof(full_path), "%s%s", ss.storage_path, folder_path);
    
    struct stat st = {0};
    if (stat(full_path, &st) == 0) {
        return ERR_FILE_EXISTS;
    }
    
    // Create folder recursively
    char tmp[MAX_PATH];
    char *p = NULL;
    size_t len;
    
    snprintf(tmp, sizeof(tmp), "%s", full_path);
    len = strlen(tmp);
    if (tmp[len - 1] == '/')
        tmp[len - 1] = 0;
        
    for (p = tmp + 1; *p; p++) {
        if (*p == '/') {
            *p = 0;
            mkdir(tmp, 0777);
            *p = '/';
        }
    }
    mkdir(tmp, 0777);
    
    log_formatted(LOG_INFO, "Created folder: %s", folder_path);
    return SUCCESS;
}

int move_file_ss(const char *filename, const char *old_path, const char *new_path) {
    char old_full[MAX_PATH], new_full[MAX_PATH];
    
    // Handle old path (empty means root)
    if (strlen(old_path) > 0 && strcmp(old_path, "/") != 0) {
        snprintf(old_full, sizeof(old_full), "%s%s/%s", ss.storage_path, old_path, filename);
    } else {
        snprintf(old_full, sizeof(old_full), "%s/%s", ss.storage_path, filename);
    }
    
    // Handle new path (empty means root)
    if (strlen(new_path) > 0 && strcmp(new_path, "/") != 0) {
        snprintf(new_full, sizeof(new_full), "%s%s/%s", ss.storage_path, new_path, filename);
    } else {
        snprintf(new_full, sizeof(new_full), "%s/%s", ss.storage_path, filename);
    }
    
    if (rename(old_full, new_full) != 0) {
        log_formatted(LOG_ERROR, "Failed to move %s to %s: %s", 
                     old_full, new_full, strerror(errno));
        return ERR_SERVER_ERROR;
    }
    
    // Move undo file too if it exists
    char old_undo[MAX_PATH], new_undo[MAX_PATH];
    snprintf(old_undo, sizeof(old_undo), "%s.undo", old_full);
    snprintf(new_undo, sizeof(new_undo), "%s.undo", new_full);
    rename(old_undo, new_undo);  // Ignore error if doesn't exist
    
    log_formatted(LOG_INFO, "Moved file %s from %s to %s", filename, old_full, new_full);
    return SUCCESS;
}

int stream_file_ss(int client_sock, const char *filename) {
    char filepath[MAX_PATH];
    snprintf(filepath, sizeof(filepath), "%s/%s", ss.storage_path, filename);
    
    FileContent *fc = init_file_content();
    if (parse_file(filepath, fc) != 0) {
        free_file_content(fc);
        return ERR_FILE_NOT_FOUND;
    }
    
    Message msg;
    init_message(&msg);
    msg.type = MSG_DATA;
    msg.status = SUCCESS;
    
    for (int i = 0; i < fc->sentence_count; i++) {
        int wc = fc->sentences[i].word_count;
        for (int j = 0; j < wc; j++) {
            const char *curr = fc->sentences[i].words[j];
            const char *next = (j + 1 < wc) ? fc->sentences[i].words[j + 1] : NULL;

            /* Determine whether the server wants the client to print a trailing space.
               Rules:
                - If there is a next token in the same sentence, print a space only when
                  both current and next tokens are non-delimiters (normal word-word spacing).
                - If this is the last token of the sentence and there are more sentences,
                  request a trailing space (space between sentences).
                - Otherwise (delimiter before next word in same sentence, or last token of last sentence),
                  do not request a trailing space. - S */
            int needs_space = 0;
            if (next) {
                if (!is_delimiter(curr[0]) && !is_delimiter(next[0])) needs_space = 1;
            } else {
                if (i < fc->sentence_count - 1) needs_space = 1;
            }

            strncpy(msg.data, curr, MAX_BUFFER - 1);
            msg.data[MAX_BUFFER - 1] = '\0';
            msg.status = needs_space ? 1 : 0;
            msg.sentence_index = i;
            msg.word_index = j;
            
            if (send_message(client_sock, &msg) < 0) {
                free_file_content(fc);
                return ERR_SERVER_ERROR;
            }
            
            usleep(STREAM_DELAY);
        }
    }
    
    msg.type = MSG_STOP;
    msg.status = SUCCESS;
    send_message(client_sock, &msg);
    
    free_file_content(fc);
    return SUCCESS;
}

int get_file_info_ss(const char *filename, FileMetadata *meta) {
    char filepath[MAX_PATH];
    snprintf(filepath, sizeof(filepath), "%s/%s", ss.storage_path, filename);
    
    log_formatted(LOG_DEBUG, "Getting file info for: %s", filepath);
    
    struct stat st;
    if (stat(filepath, &st) != 0) {
        log_formatted(LOG_ERROR, "File not found: %s (errno: %d)", filepath, errno);
        return ERR_FILE_NOT_FOUND;
    }
    
    int word_count = 0, char_count = 0;
    get_file_stats(filepath, &word_count, &char_count);
    
    meta->size = st.st_size;
    meta->word_count = word_count;
    meta->char_count = char_count;
    meta->modified = st.st_mtime;
    meta->accessed = st.st_atime;
    
    log_formatted(LOG_INFO, "File info for %s: size=%zu, words=%d, chars=%d", 
                 filename, meta->size, meta->word_count, meta->char_count);
    
    return SUCCESS;
}

void* handle_client_request(void* arg) {
    int client_sock = *((int*)arg);
    free(arg);
    
    // Set timeouts for client socket
    set_socket_timeouts(client_sock, SOCKET_TIMEOUT, SOCKET_TIMEOUT);
    
    Message msg;
    while (ss.running) {
        if (recv_message(client_sock, &msg) < 0) {
             //log_formatted(LOG_INFO, "Client disconnected (socket=%d)", client_sock);

            //break;
            continue; // Was break before - N
        }

        
        
        Message response;
        init_message(&response);
        response.type = MSG_ACK;
        
        log_formatted(LOG_REQUEST, "Client request: %d for file %s", msg.type, msg.filename);
        
        switch (msg.type) {
            case MSG_READ: {
                char buffer[MAX_BUFFER];
                 response.status = read_file_ss(msg.filename, buffer);
                if (response.status == SUCCESS) {
                    strncpy(response.data, buffer, MAX_BUFFER - 1);
                }
                send_message(client_sock, &response);
                break;
            }
            
            case MSG_LOCK_SENTENCE: {
                /* Validate sentence index against current file content before
                   attempting to create locks or start a write session. This
                   prevents creating new sentence slots implicitly when the
                   requested index is out of bounds. */
                {
                    char filepath[MAX_PATH];
                    snprintf(filepath, sizeof(filepath), "%s/%s", ss.storage_path, msg.filename);
                    FileContent *fc = init_file_content();
                    int parsed = parse_file(filepath, fc);
                    int scount = 0;
                    if (parsed == 0) {
                        scount = fc->sentence_count;
                    } else {
                        /* file missing or unreadable -> treat as empty */
                        scount = 0;
                    }

                    int invalid = 0;
                    if (scount == 0) {
                        if (msg.sentence_index != 0) invalid = 1;
                    } else {
                        /* Allow indices 0..scount, but if the client requests
                           exactly scount (append), ensure the last sentence
                           ends with a delimiter or explicit newline token; if
                           not, appending a sentence isn't allowed until the
                           user adds a delimiter. */
                        if (msg.sentence_index < 0 || msg.sentence_index > scount) {
                            invalid = 1;
                        } else if (msg.sentence_index == scount) {
                            /* check last token of last sentence */
                            if (scount > 0) {
                                Sentence *last_sent = &fc->sentences[scount - 1];
                                if (last_sent->word_count == 0) {
                                    invalid = 1;
                                } else {
                                    char *last_word = last_sent->words[last_sent->word_count - 1];
                                    int is_nl = (last_word[0] == '\n' && last_word[1] == '\0');
                                    if (!(is_delimiter(last_word[0]) || is_nl)) {
                                        invalid = 1;
                                    }
                                }
                            } else {
                                invalid = 1;
                            }
                        }
                    }

                    free_file_content(fc);

                    if (invalid) {
                        response.status = ERR_INVALID_INDEX;
                        send_message(client_sock, &response);
                        break;
                    }
                }

                response.status = lock_sentence_ss(msg.filename, msg.sentence_index, msg.sender);

                if (response.status == SUCCESS) {
                    int session_status = start_write_session_ss(msg.filename, msg.sender, msg.sentence_index);
                    if (session_status != SUCCESS) {
                        // Failed to create session, unlock
                        unlock_sentence_ss(msg.filename, msg.sentence_index, msg.sender);
                        response.status = session_status;
                        log_formatted(LOG_ERROR, "Failed to start write session, unlocking");
                    } else {
                        log_formatted(LOG_INFO, "Lock acquired and write session started");
                    }
                }
                send_message(client_sock, &response);
                break;
            }
            
            case MSG_WRITE: {
                response.status = write_file_ss(msg.filename, msg.sender, msg.sentence_index, 
                                               msg.word_index, msg.data);
                send_message(client_sock, &response);
                break;
            }
            
            case MSG_UNLOCK_SENTENCE: {
                int commit_status = commit_write_session_ss(msg.filename, msg.sender, msg.sentence_index);

                if(commit_status == SUCCESS) {
                    response.status = unlock_sentence_ss(msg.filename, msg.sentence_index, msg.sender);
                    log_formatted(LOG_INFO, "Write commited and sentence unlocked");
                }
                else {
                    response.status = commit_status;
                    unlock_sentence_ss(msg.filename, msg.sentence_index, msg.sender);
                    log_formatted(LOG_ERROR, "Commit failed but sentence unlocked");
                }
                //response.status = unlock_sentence_ss(msg.filename, msg.sentence_index, msg.sender);
                send_message(client_sock, &response);
                break;
            }

            case MSG_CANCEL_WRITE: {
                cancel_write_session_ss(msg.filename, msg.sender, msg.sentence_index);
                response.status = unlock_sentence_ss(msg.filename, msg.sentence_index, msg.sender);
                send_message(client_sock, &response);
                break;
            }
            
            
            case MSG_STREAM: {
                response.status = SUCCESS;
                send_message(client_sock, &response);
                stream_file_ss(client_sock, msg.filename);
                break;
            }
            
            case MSG_UNDO: {
                char filepath[MAX_PATH];
                snprintf(filepath, sizeof(filepath), "%s/%s", ss.storage_path, msg.filename);
                
                if (undo_backup_exists(filepath)) {
                    //printf("[SS DEBUG] Undo backup exists for %s\n", filepath); // Debug line - N
                    response.status = restore_from_undo(filepath);
                } else {
                    //printf("[SS DEBUG] No undo backup for %s\n", filepath); // Debug line - N
                    response.status = ERR_INVALID_OPERATION;
                }
                send_message(client_sock, &response);
                break;
            }
            
            default:
                response.status = ERR_INVALID_OPERATION;
                send_message(client_sock, &response);
                break;
        }
        
        log_formatted(LOG_RESPONSE, "Response status: %d", response.status);
    }
    
    close(client_sock);
    return NULL;
}

void* client_listener(void* arg) {
    (void)arg;

    ss.client_sock = socket(AF_INET, SOCK_STREAM, 0);
    if (ss.client_sock < 0) {
        perror("Client socket creation failed");
        return NULL;
    }
    
    int opt = 1;
    setsockopt(ss.client_sock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    
    struct sockaddr_in addr;
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(ss.client_port);
    
    if (bind(ss.client_sock, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        perror("Client socket bind failed");
        return NULL;
    }
    
    if (listen(ss.client_sock, MAX_CLIENTS) < 0) {
        perror("Client socket listen failed");
        return NULL;
    }
    
    printf("[SS %d] Listening for clients on port %d\n", ss.id, ss.client_port);
    log_formatted(LOG_INFO, "Client listener started on port %d", ss.client_port);
    
    while (ss.running) {
        struct sockaddr_in client_addr;
        socklen_t addr_len = sizeof(client_addr);
        
        int *client_sock = malloc(sizeof(int));
        *client_sock = accept(ss.client_sock, (struct sockaddr*)&client_addr, &addr_len);
        
        if (*client_sock < 0) {
            free(client_sock);
            continue;
        }
        
        log_formatted(LOG_INFO, "Client connected from %s", inet_ntoa(client_addr.sin_addr));
        
        pthread_t tid;
        pthread_create(&tid, NULL, handle_client_request, client_sock);
        pthread_detach(tid);
    }
    
    return NULL;
}

// Heavily edited - N
void* handle_nm_communication(void* arg) {
    (void)arg;
    Message msg;
    
    // Set a reasonable timeout so we don't block forever
    struct timeval tv;
    tv.tv_sec = 30;  // 1 second timeout for receives
    tv.tv_usec = 0;
    setsockopt(ss.nm_sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    
    log_formatted(LOG_INFO, "NM communication thread started");
    
    while (ss.running) {
        int recv_result = recv_message(ss.nm_sock, &msg);
        
        if (recv_result < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                // Timeout - this is normal, just continue
                continue;
            }
            if (errno == EINTR) {
                // Interrupted system call - just retry
                continue;
            }
            
            log_formatted(LOG_ERROR, "Lost connection to NM (errno: %d)", errno);
            ss.running = 0;
            break;
        }
        
        // Process actual commands from NM
        Message response;
        init_message(&response);
        response.type = MSG_ACK;
        response.ss_id = ss.id;
        
        log_formatted(LOG_REQUEST, "NM request: type=%d, file=%s", msg.type, msg.filename);
        
        switch (msg.type) {
            case MSG_CHECK_LOCKS: {
                int has_locks = check_file_locks(msg.filename);
                response.status = has_locks ? ERR_FILE_LOCKED : SUCCESS;
                log_formatted(LOG_INFO, "CHECK_LOCKS %s: has_locks=%d", 
                            msg.filename, has_locks);
                break;
            }

            case MSG_CHECKPOINT: {
                char filepath[MAX_PATH];
                snprintf(filepath, sizeof(filepath), "%s/%s", ss.storage_path, msg.filename);
                response.status = create_checkpoint(filepath, msg.checkpoint_tag);
                log_formatted(LOG_INFO, "CHECKPOINT %s tag=%s: status=%d", 
                             msg.filename, msg.checkpoint_tag, response.status);
                break;
            }
            
            case MSG_LISTCHECKPOINTS: {
                char filepath[MAX_PATH];
                snprintf(filepath, sizeof(filepath), "%s/%s", ss.storage_path, msg.filename);
                response.status = list_checkpoints(filepath, response.data, MAX_BUFFER);
                log_formatted(LOG_INFO, "LISTCHECKPOINTS %s: status=%d", 
                             msg.filename, response.status);
                break;
            }
            
            case MSG_VIEWCHECKPOINT: {
                char filepath[MAX_PATH];
                snprintf(filepath, sizeof(filepath), "%s/%s", ss.storage_path, msg.filename);
                response.status = view_checkpoint(filepath, msg.checkpoint_tag, 
                                                   response.data, MAX_BUFFER);
                log_formatted(LOG_INFO, "VIEWCHECKPOINT %s tag=%s: status=%d", 
                             msg.filename, msg.checkpoint_tag, response.status);
                break;
            }
            
            case MSG_REVERT: {
                char filepath[MAX_PATH];
                snprintf(filepath, sizeof(filepath), "%s/%s", ss.storage_path, msg.filename);
                response.status = revert_to_checkpoint(filepath, msg.checkpoint_tag);
                log_formatted(LOG_INFO, "REVERT %s to tag=%s: status=%d", 
                             msg.filename, msg.checkpoint_tag, response.status);
                break;
            }
            
            case MSG_CREATEFOLDER:
                response.status = create_folder_ss(msg.target_path);
                log_formatted(LOG_INFO, "CREATEFOLDER %s: status=%d", msg.target_path, response.status);
                break;
                
            case MSG_MOVE:
                response.status = move_file_ss(msg.filename, msg.data, msg.target_path);
                log_formatted(LOG_INFO, "MOVE %s to %s: status=%d", 
                             msg.filename, msg.target_path, response.status);
                break;

            case MSG_CREATE:
                response.status = create_file_ss(msg.filename);
                log_formatted(LOG_INFO, "CREATE %s: status=%d", msg.filename, response.status);
                break;
                
            case MSG_DELETE:
                response.status = delete_file_ss(msg.filename);
                log_formatted(LOG_INFO, "DELETE %s: status=%d", msg.filename, response.status);
                break;
                
            case MSG_SS_INFO: {
                FileMetadata meta;
                memset(&meta, 0, sizeof(FileMetadata));
                
                if (strcmp(msg.data, "READ_CONTENT") == 0) {
                    char buffer[MAX_BUFFER];
                    response.status = read_file_ss(msg.filename, buffer);
                    if (response.status == SUCCESS) {
                        strncpy(response.data, buffer, MAX_BUFFER - 1);
                        response.data[MAX_BUFFER - 1] = '\0';
                        log_formatted(LOG_DEBUG, "Returning file content (%zu bytes)", strlen(buffer));
                    } else {
                        log_formatted(LOG_ERROR, "Failed to read file %s: status=%d", 
                                     msg.filename, response.status);
                    }
                } else {
                    response.status = get_file_info_ss(msg.filename, &meta);
                    if (response.status == SUCCESS) {
                        snprintf(response.data, MAX_BUFFER, "%zu|%d|%d|%ld|%ld", 
                            meta.size, meta.word_count, meta.char_count, 
                            meta.modified, meta.accessed);
                        
                        log_formatted(LOG_INFO, "Sending metadata for %s: size=%zu, words=%d, chars=%d", 
                                     msg.filename, meta.size, meta.word_count, meta.char_count);
                    } else {
                        log_formatted(LOG_ERROR, "Failed to get file info for %s: status=%d", 
                                     msg.filename, response.status);
                    }
                }
                break;
            }
            
            default:
                log_formatted(LOG_WARNING, "Unknown message type from NM: %d", msg.type);
                response.status = ERR_INVALID_OPERATION;
                break;
        }
        
        // NEW: No mutex needed, dedicated socket
        int send_result = send_message(ss.nm_sock, &response);
        
        if (send_result < 0) {
            log_formatted(LOG_ERROR, "Failed to send response to NM (errno: %d)", errno);
            if (errno == EPIPE || errno == ECONNRESET || errno == ENOTCONN) {
                log_formatted(LOG_ERROR, "Connection to NM broken, shutting down");
                ss.running = 0;
                break;
            }
        } else {
            log_formatted(LOG_RESPONSE, "Sent response to NM: status=%d", response.status);
        }
    }
    
    log_formatted(LOG_INFO, "NM communication thread exiting");
    return NULL;
}

// Edited to handle heartbeat socket, checking alive connections and closing if they are not - N
void* heartbeat_thread(void* arg) {
    (void)arg;
    
    log_formatted(LOG_INFO, "Heartbeat thread started");
    
    // Send initial identification on heartbeat socket - N
    Message ident;
    init_message(&ident);
    ident.type = MSG_ACK;
    ident.ss_id = ss.id;
    strcpy(ident.data, "HB_INIT");
    send_message(ss.nm_hb_sock, &ident);

    Message msg;
    init_message(&msg);
    msg.type = MSG_ACK;
    msg.ss_id = ss.id;
    strcpy(msg.data, "HEARTBEAT");
    send_message(ss.nm_hb_sock, &msg);
    
    while (ss.running) {
        sleep(HEARTBEAT_INTERVAL);
        
        Message msg;
        init_message(&msg);
        msg.type = MSG_ACK;
        msg.ss_id = ss.id;
        strcpy(msg.data, "HEARTBEAT");
        
        log_formatted(LOG_DEBUG, "Sending heartbeat to NM");
        
        // Use heartbeat socket, no mutex needed - N
        int result = send_message(ss.nm_hb_sock, &msg);
        
        // Kill off the connection if we get errors indicating it's dead - N
        if (result < 0) {
            if (errno == EPIPE || errno == ECONNRESET || errno == ENOTCONN) {
                log_formatted(LOG_ERROR, "Connection lost to NM (errno: %d)", errno);
                ss.running = 0;
                break;
            }
            log_formatted(LOG_WARNING, "Heartbeat send failed (errno: %d), will retry", errno);
        } else {
            log_formatted(LOG_DEBUG, "Heartbeat sent successfully");
        }
    }
    
    log_formatted(LOG_INFO, "Heartbeat thread exiting");
    return NULL;
}

int main(int argc, char *argv[]) {
    if (argc != 5) {
        printf("Usage: %s <nm_ip> <nm_port> <client_port> <dir_name>\n", argv[0]);
        return 1;
    }
    
    char *nm_ip = argv[1];
    int nm_port = atoi(argv[2]);
    int client_port = atoi(argv[3]);
    int ss_id = atoi(argv[4]);

    // ADD: Validate ports
    if (nm_port <= 0 || nm_port > 65535) {
        printf("Error: Invalid NM port number. Must be between 1 and 65535.\n");
        return 1;
    }
    
    if (client_port <= 0 || client_port > 65535) {
        printf("Error: Invalid client port number. Must be between 1 and 65535.\n");
        return 1;
    }
    
    if (client_port == nm_port) {
        printf("Error: Client port and NM port cannot be the same.\n");
        return 1;
    }
    
    // Warn if not using standard ports (helpful for debugging)
    if (nm_port != NM_SS_PORT) {
        printf("[SS] Warning: Connecting to NM on non-standard port %d (expected %d)\n", 
               nm_port, NM_SS_PORT);
    }
    
    init_storage_server(nm_ip, nm_port, client_port, ss_id);
    connect_to_nm(nm_ip, nm_port);
    scan_and_register_files();
    
    pthread_t nm_thread, client_thread, hb_thread;
    
    // Robust checking - N
    if (pthread_create(&nm_thread, NULL, handle_nm_communication, NULL) != 0) {
        log_formatted(LOG_ERROR, "Failed to create NM thread");
        return 1;
    }
    
    if (pthread_create(&client_thread, NULL, client_listener, NULL) != 0) {
        log_formatted(LOG_ERROR, "Failed to create client thread");
        return 1;
    }

    sleep(1); // Ensure NM thread is up before heartbeat thread starts
    // This was causing certain issues so i commented it out - N
    
    if (pthread_create(&hb_thread, NULL, heartbeat_thread, NULL) != 0) {
        log_formatted(LOG_ERROR, "Failed to create heartbeat thread");
        return 1;
    }
    
    printf("[SS %d] Storage Server running. Press Ctrl+C to stop.\n", ss.id);
    log_formatted(LOG_INFO, "All threads started successfully");
    
    pthread_join(nm_thread, NULL);
    pthread_join(client_thread, NULL);
    pthread_join(hb_thread, NULL);
    
    close(ss.nm_sock);
    close(ss.nm_hb_sock);
    close(ss.client_sock);
    close_logger();
    
    return 0;
}