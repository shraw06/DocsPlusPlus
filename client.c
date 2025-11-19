#include "common.h"
#include "logger.h"
#include <signal.h> // For signal handling - S
#include <unistd.h> // For signal handling - S
#include <fcntl.h>  // For pipe fcntl - S
#include <errno.h> // For errno - S
#include <sys/select.h> // For select - S

// Add struct elements to store nm server ip and port for connection (8081) - N
typedef struct {
    char username[MAX_USERNAME];
    int nm_sock;
    int connected;
    char nm_ip[INET_ADDRSTRLEN];
    int nm_port;
} Client;

Client client;

// Signal handling variables - S
volatile sig_atomic_t current_ss_sock = -1;
volatile sig_atomic_t current_locked_sentence = -1;
char current_locked_filename[MAX_FILENAME] = {0};
int sig_pipe[2] = {-1, -1}; // pipe for signal handling - S
volatile sig_atomic_t sigint_received = 0; // flag for signal handling - S

// Signal handler for SIGINT - S
void sigint_handler(int signo) {

    // send signal to main loop via pipe - S
    if(sig_pipe[1] != -1) {
        const char b = 'I';
        ssize_t r = write(sig_pipe[1], &b, 1);
        (void)r;
    }

    sigint_received = 1;

    // calling send/recv/exit in signal handler is unsafe, so using a pipe to notify main loop - S
    // if(current_ss_sock > 0 && current_locked_sentence >= 0) {
    //     Message msg;
    //     init_message(&msg);
    //     msg.type = MSG_UNLOCK_SENTENCE;
    //     strncpy(msg.filename, current_locked_filename, MAX_FILENAME - 1);
    //     msg.filename[MAX_FILENAME - 1] = '\0';
    //     strcpy(msg.sender, client.username);
    //     msg.sentence_index = current_locked_sentence;

    //     send_message((int)current_ss_sock, &msg);
    //     //close((int)current_ss_sock);
    // }

    // if(client.nm_sock > 0) {
    //     close(client.nm_sock);
    // }
    // close_logger();

    // _exit(1);
}

// Function to perform unlock if needed on ctrl c - S
void perform_pending_unlock() {
    if(current_ss_sock > 0 && current_locked_sentence >= 0) {
        Message msg;
        init_message(&msg);
        msg.type = MSG_CANCEL_WRITE;
        strncpy(msg.filename, current_locked_filename, MAX_FILENAME - 1);
        msg.filename[MAX_FILENAME - 1] = '\0';
        strcpy(msg.sender, client.username);
        msg.sentence_index = current_locked_sentence;

        if(send_message((int)current_ss_sock, &msg) < 0) {
            printf("Error sending cancel message\n");
        }
        else {
            Message resp;
            if(recv_message((int)current_ss_sock, &resp) >= 0) {
                if(resp.status ==SUCCESS)
                {
                    printf("\n[INFO] Write session cancelled and sentence %d in file %s unlocked due to interrupt signal.\n", current_locked_sentence, current_locked_filename);
                }
                else {
                    printf("\n[WARN] Cancel returned status %d\n", resp.status);
                }
            } else {
                printf("[WARN] No response received for cancel message\n");
            }
        }

        current_locked_sentence = -1;
        current_locked_filename[0] = '\0';
    }
}

// Function declarations
void init_client();
void connect_to_nm();
void command_loop();
void handle_view(char *args);
void handle_read(char *filename);
void handle_create(char *filename);
void handle_write(char *filename, char *sent_idx_str);
void handle_delete(char *filename);
void handle_info(char *filename);
void handle_stream(char *filename);
void handle_list();
void handle_addaccess(char *flag, char *filename, char *username);
void handle_remaccess(char *filename, char *username);
void handle_exec(char *filename);
void handle_undo(char *filename);
int connect_to_ss(const char *ss_info);
void print_error(int status);

void init_client() {
    printf("Enter username: ");
    fgets(client.username, MAX_USERNAME, stdin);
    trim_whitespace(client.username);
    
    client.connected = 0;

    // Initialize logger for client
    char instance_name[64];
    snprintf(instance_name, sizeof(instance_name), "Client_%s", client.username);
    set_instance_name(instance_name);  // Added for better logging - N
    
    char log_file[128];
    snprintf(log_file, sizeof(log_file), "client_%s.log", client.username);
    init_logger(log_file);  // Starting logs here - N
    
    printf("[Client] Username: %s\n", client.username);
}

void connect_to_nm() {
    client.nm_sock = socket(AF_INET, SOCK_STREAM, 0);
    if (client.nm_sock < 0) {
        perror("Socket creation failed");
        exit(1);
    }
    
    // Make client connect to NM server using stored ip and port - N
    struct sockaddr_in nm_addr;
    nm_addr.sin_family = AF_INET;
    nm_addr.sin_port = htons(client.nm_port);
    inet_pton(AF_INET, client.nm_ip, &nm_addr.sin_addr);
    
    if (connect(client.nm_sock, (struct sockaddr*)&nm_addr, sizeof(nm_addr)) < 0) {
        perror("Connection to NM failed");
        exit(1);
    }

    Message msg;
    init_message(&msg);
    
    struct sockaddr_in local_addr;
    socklen_t addr_len = sizeof(local_addr);
    if (getsockname(client.nm_sock, (struct sockaddr*)&local_addr, &addr_len) == 0) {
        inet_ntop(AF_INET, &local_addr.sin_addr, msg.data, INET_ADDRSTRLEN);
    } else {
        // Fallback: use loopback if we can't get the actual IP, continuously stuck here - N
        //printf("[DEBUG] getsockname failed, using loopback address\n");
        strcpy(msg.data, "127.0.0.1");
    }
    
    // Send registration
    msg.type = MSG_REG_CLIENT;
    strcpy(msg.sender, client.username);
    
    send_message(client.nm_sock, &msg);
    
    Message response;
    recv_message(client.nm_sock, &response);
    
    if (response.status == SUCCESS) {
        client.connected = 1;
        printf("[Client] Connected to Name Server at %s:%d\n", client.nm_ip, client.nm_port);
        log_formatted(LOG_INFO, "Connected to NM at %s:%d", client.nm_ip, client.nm_port);
    } else {
        printf("[Client] Registration failed\n");
        log_formatted(LOG_ERROR, "Registration failed: %s", response.data);
        exit(1);
    }
}

void print_error(int status) {
    switch (status) {
        case ERR_FILE_NOT_FOUND:
            printf("Error: File not found\n");
            break;
        case ERR_ACCESS_DENIED:
            printf("Error: Access denied\n");
            break;
        case ERR_SENTENCE_LOCKED:
            printf("Error: Sentence is locked by another user\n");
            break;
        case ERR_FILE_LOCKED:  
            printf("Error: Cannot delete file - one or more sentences are currently locked by other users\n");
            break;
        case ERR_INVALID_INDEX:
            printf("Error: Invalid sentence or word index\n");
            break;
        case ERR_FILE_EXISTS:
            printf("Error: File already exists\n");
            break;
        case ERR_SS_UNAVAILABLE:
            printf("Error: Storage server unavailable\n");
            break;
        case ERR_INVALID_OPERATION:
            printf("Error: Invalid operation\n");
            break;
        case ERR_NOT_OWNER:
            printf("Error: You are not the owner of this file\n");
            break;
        case ERR_USER_NOT_FOUND:
            printf("Error: User not found\n");
            break;
        default:
            printf("Error: Unknown error (code %d)\n", status);
            break;
    }
}

int connect_to_ss(const char *ss_info) {
    char ip[INET_ADDRSTRLEN];
    int port;
    
    //printf("[DEBUG] Connecting to SS with info: %s\n", ss_info); // Debug line
    sscanf(ss_info, "%[^:]:%d", ip, &port);
    
    int ss_sock = socket(AF_INET, SOCK_STREAM, 0);
    if (ss_sock < 0) {
        return -1;
    }
    
    struct sockaddr_in ss_addr;
    ss_addr.sin_family = AF_INET;
    ss_addr.sin_port = htons(port);
    inet_pton(AF_INET, ip, &ss_addr.sin_addr);
    
    if (connect(ss_sock, (struct sockaddr*)&ss_addr, sizeof(ss_addr)) < 0) {
        close(ss_sock);
        return -1;
    }

    log_formatted(LOG_INFO, "Connected to SS at %s:%d", ip, port);
    
    return ss_sock;
}

void handle_createfolder(char *foldername, char *parent_path) {
    Message msg;
    init_message(&msg);
    msg.type = MSG_CREATEFOLDER;
    strcpy(msg.sender, client.username);
    strcpy(msg.foldername, foldername);
    if (parent_path) {
        strcpy(msg.target_path, parent_path);
    } else {
        msg.target_path[0] = '\0';
    }
    
    send_message(client.nm_sock, &msg);
    
    Message response;
    recv_message(client.nm_sock, &response);
    
    if (response.status == SUCCESS) {
        printf("Folder created successfully!\n");
    } else {
        print_error(response.status);
    }
}

void handle_move_file(char *filename, char *foldername) {
    Message msg;
    init_message(&msg);
    msg.type = MSG_MOVE;
    strcpy(msg.sender, client.username);
    strcpy(msg.filename, filename);
    strcpy(msg.target_path, foldername);
    
    send_message(client.nm_sock, &msg);
    
    Message response;
    recv_message(client.nm_sock, &response);
    
    if (response.status == SUCCESS) {
        printf("File moved successfully!\n");
    } else {
        print_error(response.status);
    }
}

void handle_viewfolder(char *foldername) {
    Message msg;
    init_message(&msg);
    msg.type = MSG_VIEWFOLDER;
    strcpy(msg.sender, client.username);
    strcpy(msg.target_path, foldername);
    
    send_message(client.nm_sock, &msg);
    
    Message response;
    recv_message(client.nm_sock, &response);

    if (response.status == SUCCESS) {
        printf("%s", response.data);
    } else {
        print_error(response.status);
    }
}

void handle_view(char *args) {
    Message msg;
    init_message(&msg);
    msg.type = MSG_VIEW;
    strcpy(msg.sender, client.username);
    
    if (args) {
        strncpy(msg.data, args, MAX_BUFFER - 1);
    }
    
    //printf("[DEBUG] Sending VIEW request with args: %s\n", args ? args : "None");
    send_message(client.nm_sock, &msg);
    
    Message response;
    recv_message(client.nm_sock, &response);
    
    if (response.status == SUCCESS) {
        printf("%s", response.data);
    } else {
        print_error(response.status);
    }
}

void handle_read(char *filename) {
    Message msg;
    init_message(&msg);
    msg.type = MSG_READ;
    strcpy(msg.sender, client.username);
    strcpy(msg.filename, filename);
    
    send_message(client.nm_sock, &msg);
    
    Message response;
    recv_message(client.nm_sock, &response);
    
    if (response.status != SUCCESS) {
        print_error(response.status);
        return;
    }
    
    // Connect to SS
    int ss_sock = connect_to_ss(response.data);
    if (ss_sock < 0) {
        printf("Error: Could not connect to storage server\n");
        return;
    }
    
    // Send read request to SS
    init_message(&msg);
    msg.type = MSG_READ;
    strcpy(msg.filename, filename);
    strcpy(msg.sender, client.username);
    
    send_message(ss_sock, &msg);
    recv_message(ss_sock, &response);
    
    if (response.status == SUCCESS) {
        printf("%s\n", response.data);
    } else {
        print_error(response.status);
    }
    
    close(ss_sock);
}

void handle_checkpoint(char *filename, char *tag) {
    Message msg;
    init_message(&msg);
    msg.type = MSG_CHECKPOINT;
    strcpy(msg.sender, client.username);
    strcpy(msg.filename, filename);
    strcpy(msg.checkpoint_tag, tag);
    
    send_message(client.nm_sock, &msg);
    
    Message response;
    recv_message(client.nm_sock, &response);
    
    if (response.status == SUCCESS) {
        printf("Checkpoint '%s' created successfully!\n", tag);
    } else {
        print_error(response.status);
    }
}

void handle_viewcheckpoint(char *filename, char *tag) {
    Message msg;
    init_message(&msg);
    msg.type = MSG_VIEWCHECKPOINT;
    strcpy(msg.sender, client.username);
    strcpy(msg.filename, filename);
    strcpy(msg.checkpoint_tag, tag);
    
    send_message(client.nm_sock, &msg);
    
    Message response;
    recv_message(client.nm_sock, &response);
    
    if (response.status == SUCCESS) {
        printf("%s\n", response.data);
    } else {
        print_error(response.status);
    }
}

void handle_revert(char *filename, char *tag) {
    Message msg;
    init_message(&msg);
    msg.type = MSG_REVERT;
    strcpy(msg.sender, client.username);
    strcpy(msg.filename, filename);
    strcpy(msg.checkpoint_tag, tag);
    
    send_message(client.nm_sock, &msg);
    
    Message response;
    recv_message(client.nm_sock, &response);
    
    if (response.status == SUCCESS) {
        printf("File reverted to checkpoint '%s' successfully!\n", tag);
    } else {
        print_error(response.status);
    }
}

void handle_listcheckpoints(char *filename) {
    Message msg;
    init_message(&msg);
    msg.type = MSG_LISTCHECKPOINTS;
    strcpy(msg.sender, client.username);
    strcpy(msg.filename, filename);
    
    send_message(client.nm_sock, &msg);
    
    Message response;
    recv_message(client.nm_sock, &response);
    
    if (response.status == SUCCESS) {
        printf("Checkpoints for %s:\n%s", filename, response.data);
    } else {
        print_error(response.status);
    }
}

void handle_create(char *filename) {
    Message msg;
    init_message(&msg);
    msg.type = MSG_CREATE;
    strcpy(msg.sender, client.username);
    strcpy(msg.filename, filename);
    
    //printf("[DEBUG] Sending CREATE request for: %s\n", filename);
    
    if (send_message(client.nm_sock, &msg) < 0) {
        //printf("[DEBUG] Failed to send message\n");
        return;
    }

    //printf("[DEBUG] Waiting for response...\n");

    Message response;
    if (recv_message(client.nm_sock, &response) < 0) {
        //printf("[DEBUG] Failed to receive response\n");
        return;
    }

    //printf("[DEBUG] Received response: type=%d, status=%d\n", response.type, response.status);

    if (response.status == SUCCESS) {
        printf("File created successfully!\n");
    } else {
        print_error(response.status);
    }
}

void process_escape_sequences(char *str) {
    char *src = str;
    char *dst = str;
    
    while (*src) {
        if (*src == '\\' && *(src + 1)) {
            switch (*(src + 1)) {
                case 'n':
                    *dst++ = '\n';
                    src += 2;
                    break;
                case 't':
                    *dst++ = '\t';
                    src += 2;
                    break;
                case 'r':
                    *dst++ = '\r';
                    src += 2;
                    break;
                case '\\':
                    *dst++ = '\\';
                    src += 2;
                    break;
                case '\'' :
                    *dst++ = '\'';
                    src += 2;
                    break;
                case '"' :
                    *dst++ = '"';
                    src += 2;
                    break;
                case '0': {
                    /* simple NUL escape: treat as single char '\0' */
                    *dst++ = '\0';
                    src += 2;
                    break;
                }
                default:
                    /* Unknown escape: keep the char after backslash as-is. */
                    *dst++ = *(src + 1);
                    src += 2;
                    break;
                }
        } else {
            *dst++ = *src++;
        }
    }
    *dst = '\0';
}

void handle_write(char *filename, char *sent_idx_str) {
    int sent_idx = atoi(sent_idx_str);

    // reset signal handling variables
    current_locked_sentence = -1;   
    current_locked_filename[0] = '\0';
    
    // Get SS info from NM
    Message msg;
    init_message(&msg);
    msg.type = MSG_WRITE;
    strcpy(msg.sender, client.username);
    strcpy(msg.filename, filename);
    
    send_message(client.nm_sock, &msg);
    
    Message response;
    recv_message(client.nm_sock, &response);
    
    if (response.status != SUCCESS) {
        print_error(response.status);
        return;
    }
    
    // Connect to SS
    int ss_sock = connect_to_ss(response.data);
    if (ss_sock < 0) {
        printf("Error: Could not connect to storage server\n");
        return;
    }
    
    current_ss_sock = ss_sock;

    // Lock sentence
    init_message(&msg);
    msg.type = MSG_LOCK_SENTENCE;
    strcpy(msg.filename, filename);
    strcpy(msg.sender, client.username);
    msg.sentence_index = sent_idx;
    
    send_message(ss_sock, &msg);
    recv_message(ss_sock, &response);
    
    if (response.status != SUCCESS) {
        print_error(response.status);
        close(ss_sock);
        current_ss_sock = -1;
        return;
    }
    
    // For signal handling
    current_locked_sentence = sent_idx; 
    strncpy(current_locked_filename, filename, MAX_FILENAME - 1);
    current_locked_filename[MAX_FILENAME - 1] = '\0';

    printf("Sentence locked. Enter writes (word_index content), then type ETIRW:\n");
    
    // Read write commands
    char line[MAX_BUFFER];
    int write_count = 0;
    int status = 1;
    
    while (1) {
        printf("Client: ");
        if (!fgets(line, sizeof(line), stdin)) {
            if(sigint_received) {
                perform_pending_unlock();
                printf("\nInterrupted; exiting client.\n");
                exit(0);
            }
            break;
        }
        
        // Check for signal during input
        if(sigint_received) {
            perform_pending_unlock();
            printf("\nInterrupted; exiting client.\n");
            exit(0);
        }
        
        trim_whitespace(line);
        
        if (strcmp(line, "ETIRW") == 0) {
            break;
        }
        
        // Parse word_index and content
        int word_idx;
        char content[MAX_BUFFER];
        
        char *first_space = strchr(line, ' ');
    
        if (!first_space) {
            printf("Invalid format. Use: <word_index> <content>\n");
            continue;
        }
        
        // Parse word index
        if (sscanf(line, "%d", &word_idx) != 1) {
            printf("Invalid format. Use: <word_index> <content>\n");
            continue;
        }
        
        // Everything after first space is content (preserving all spaces)
        strcpy(content, first_space + 1);
        
        // Remove only the trailing newline if present (NOT leading spaces!)
        size_t content_len = strlen(content);
        if (content_len > 0 && content[content_len - 1] == '\n') {
            content[content_len - 1] = '\0';
        }

        process_escape_sequences(content);
        
        // Try to send write to SS with retry logic
        int retry_count = 0;
        int write_success = 0;
        
        while (retry_count < 5 && !write_success) {
            // Prepare write message
            init_message(&msg);
            msg.type = MSG_WRITE;
            strcpy(msg.filename, filename);
            strcpy(msg.sender, client.username);
            msg.sentence_index = sent_idx;
            msg.word_index = word_idx;
            strcpy(msg.data, content);
            
            // Try to send write
            if (send_message(ss_sock, &msg) < 0) {
                // Send failed - connection lost
                printf("Error: Storage server disconnected during write\n");
                printf("Attempting to reconnect (attempt %d/5)...\n", retry_count + 1);
                close(ss_sock);
                current_ss_sock = -1;
                retry_count++;
                
                // Try to get new SS info from NM
                init_message(&msg);
                msg.type = MSG_WRITE;
                strcpy(msg.sender, client.username);
                strcpy(msg.filename, filename);
                
                if (send_message(client.nm_sock, &msg) < 0) {
                    printf("Error: Could not contact Name Server for reconnection\n");
                    sleep(1);
                    continue;
                }
                
                if (recv_message(client.nm_sock, &response) < 0) {
                    printf("Error: Lost connection to Name Server\n");
                    sleep(1);
                    continue;
                }
                
                if (response.status != SUCCESS) {
                    printf("Error: File no longer available\n");
                    print_error(response.status);
                    sleep(1);
                    continue;
                }
                
                // Try to reconnect to SS
                ss_sock = connect_to_ss(response.data);
                if (ss_sock < 0) {
                    printf("Error: Could not reconnect to storage server\n");
                    sleep(1);
                    continue;
                }
                
                current_ss_sock = ss_sock;
                printf("Reconnected successfully. Attempting to re-acquire lock...\n");
                
                // Try to re-lock the sentence
                init_message(&msg);
                msg.type = MSG_LOCK_SENTENCE;
                strcpy(msg.filename, filename);
                strcpy(msg.sender, client.username);
                msg.sentence_index = sent_idx;
                
                if (send_message(ss_sock, &msg) < 0 || recv_message(ss_sock, &response) < 0) {
                    printf("Error: Could not re-acquire lock after reconnection\n");
                    close(ss_sock);
                    current_ss_sock = -1;
                    sleep(1);
                    continue;
                }
                
                if (response.status != SUCCESS) {
                    printf("Error: Could not re-acquire lock - ");
                    print_error(response.status);
                    close(ss_sock);
                    current_ss_sock = -1;
                    sleep(1);
                    continue;
                }
                
                printf("Lock re-acquired. Retrying write operation...\n");
                // Loop will retry the send_message at the top
                continue;
            }
            
            // Send succeeded, now try to receive response
            if (recv_message(ss_sock, &response) < 0) {
                // Receive failed - connection lost
                printf("Error: Storage server disconnected while waiting for response\n");
                printf("Attempting to reconnect (attempt %d/5)...\n", retry_count + 1);
                close(ss_sock);
                current_ss_sock = -1;
                retry_count++;
                
                // Same reconnection logic as above
                init_message(&msg);
                msg.type = MSG_WRITE;
                strcpy(msg.sender, client.username);
                strcpy(msg.filename, filename);
                
                if (send_message(client.nm_sock, &msg) < 0) {
                    printf("Error: Could not contact Name Server for reconnection\n");
                    sleep(1);
                    continue;
                }
                
                if (recv_message(client.nm_sock, &response) < 0) {
                    printf("Error: Lost connection to Name Server\n");
                    sleep(1);
                    continue;
                }
                
                if (response.status != SUCCESS) {
                    printf("Error: File no longer available\n");
                    print_error(response.status);
                    sleep(1);
                    continue;
                }
                
                ss_sock = connect_to_ss(response.data);
                if (ss_sock < 0) {
                    printf("Error: Could not reconnect to storage server\n");
                    sleep(1);
                    continue;
                }
                
                current_ss_sock = ss_sock;
                printf("Reconnected successfully. Attempting to re-acquire lock...\n");
                
                init_message(&msg);
                msg.type = MSG_LOCK_SENTENCE;
                strcpy(msg.filename, filename);
                strcpy(msg.sender, client.username);
                msg.sentence_index = sent_idx;
                
                if (send_message(ss_sock, &msg) < 0 || recv_message(ss_sock, &response) < 0) {
                    printf("Error: Could not re-acquire lock after reconnection\n");
                    close(ss_sock);
                    current_ss_sock = -1;
                    sleep(1);
                    continue;
                }
                
                if (response.status != SUCCESS) {
                    printf("Error: Could not re-acquire lock - ");
                    print_error(response.status);
                    close(ss_sock);
                    current_ss_sock = -1;
                    sleep(1);
                    continue;
                }
                
                printf("Lock re-acquired. Retrying write operation...\n");
                continue;
            }
            
            // Both send and receive succeeded
            if (response.status != SUCCESS) {
                printf("Write failed: ");
                print_error(response.status);
                status = 0;
                break; // Exit retry loop
            }
            
            // Write was successful!
            write_success = 1;
            write_count++;
            printf("Write successful!\n");
        }
        
        // Check if we exhausted all retries
        if (retry_count >= 5 && !write_success) {
            printf("Error: Could not complete write after 5 reconnection attempts\n");
            status = 0;
            break; // Exit main while loop
        }
        
        // If write failed for non-connection reasons, exit
        if (!write_success && retry_count == 0) {
            break;
        }
    }
    
    // Unlock sentence if we still have a connection
    if (current_ss_sock > 0) {
        init_message(&msg);
        msg.type = MSG_UNLOCK_SENTENCE;
        strcpy(msg.filename, filename);
        strcpy(msg.sender, client.username);
        msg.sentence_index = sent_idx;
        
        if (send_message(ss_sock, &msg) < 0) {
            printf("Error: Could not send unlock (connection lost)\n");
        } else if (recv_message(ss_sock, &response) < 0) {
            printf("Error: Could not receive unlock response (connection lost)\n");
        } else if (response.status != SUCCESS) {
            printf("Unlock failed: ");
            print_error(response.status);
        }
        
        close(ss_sock);
    }
    
    // Clear signal handling variables
    current_ss_sock = -1;
    current_locked_sentence = -1;
    current_locked_filename[0] = '\0';
    
    // Print final status
    if (write_count > 0 && status) {
        printf("All writes completed successfully! (%d write(s))\n", write_count);
    } else if (write_count == 0) {
        printf("No writes performed.\n");
    } else {
        printf("Write session ended with errors. Completed %d write(s) before failure.\n", write_count);
    }
}

void handle_delete(char *filename) {
    Message msg;
    init_message(&msg);
    msg.type = MSG_DELETE;
    strcpy(msg.sender, client.username);
    strcpy(msg.filename, filename);
    
    send_message(client.nm_sock, &msg);
    
    Message response;
    recv_message(client.nm_sock, &response);
    
    if (response.status == SUCCESS) {
        printf("File deleted successfully!\n");
    } else {
        print_error(response.status);
    }
}

void handle_info(char *filename) {
    Message msg;
    init_message(&msg);
    msg.type = MSG_INFO;
    strcpy(msg.sender, client.username);
    strcpy(msg.filename, filename);
    
    send_message(client.nm_sock, &msg);
    
    Message response;
    recv_message(client.nm_sock, &response);
    
    if (response.status == SUCCESS) {
        printf("%s", response.data);
    } else {
        print_error(response.status);
    }
}

// void handle_stream(char *filename) {
//     Message msg;
//     init_message(&msg);
//     msg.type = MSG_STREAM;
//     strcpy(msg.sender, client.username);
//     strcpy(msg.filename, filename);
    
//     send_message(client.nm_sock, &msg);
    
//     Message response;
//     recv_message(client.nm_sock, &response);
    
//     if (response.status != SUCCESS) {
//         print_error(response.status);
//         return;
//     }
    
//     // Connect to SS
//     int ss_sock = connect_to_ss(response.data);
//     if (ss_sock < 0) {
//         printf("Error: Could not connect to storage server\n");
//         return;
//     }
    
//     // Send stream request
//     init_message(&msg);
//     msg.type = MSG_STREAM;
//     strcpy(msg.filename, filename);
//     strcpy(msg.sender, client.username);
    
//     send_message(ss_sock, &msg);
//     recv_message(ss_sock, &response);
    
//     if (response.status != SUCCESS) {
//         print_error(response.status);
//         close(ss_sock);
//         return;
//     }
    
//     // Receive and display words
//     while (1) {
//         if (recv_message(ss_sock, &response) < 0) {
//             printf("\nError: Connection to storage server lost\n");
//             break;
//         }
        
//         if (response.type == MSG_STOP) {
//             printf("\n");
//             break;
//         }
        
//         printf("%s ", response.data);
//         fflush(stdout);
//     }
    
//     close(ss_sock);
// }

void handle_stream(char *filename) {
    Message msg;
    init_message(&msg);
    msg.type = MSG_READ;
    strcpy(msg.sender, client.username);
    strcpy(msg.filename, filename);
    
    send_message(client.nm_sock, &msg);
    
    Message response;
    recv_message(client.nm_sock, &response);
    
    if (response.status != SUCCESS) {
        print_error(response.status);
        return;
    }
    int ss_sock = connect_to_ss(response.data ); /* note: connect_to_ss expects "ip:port"; adjust call if you instead get ss_info elsewhere */
    if (ss_sock < 0) {
        printf("Failed to connect to storage server for streaming\n");
        return;
    }

    Message req;
    init_message(&req);
    req.type = MSG_STREAM;
    strcpy(req.sender, client.username);
    strncpy(req.filename, filename, MAX_FILENAME - 1);
    req.filename[MAX_FILENAME - 1] = '\0';

    if (send_message(ss_sock, &req) < 0) {
        printf("Failed to request stream\n");
        close(ss_sock);
        return;
    }

    /* Receive stream tokens until MSG_STOP */
    Message resp;
    while (1) {
        if (recv_message(ss_sock, &resp) < 0) {
            printf("Stream interrupted\n");
            break;
        }
        if (resp.type == MSG_STOP) {
            break;
        }
        if (resp.type != MSG_DATA) {
            continue;
        }

        /* Print token */
        printf("%s", resp.data);

        /* Print trailing space only when server asked for it (resp.status == 1) */
        if (resp.status == 1) {
            printf(" ");
        }
        fflush(stdout);
    }

    printf("\n");
    close(ss_sock);
}

void handle_list() {
    Message msg;
    init_message(&msg);
    msg.type = MSG_LIST;
    strcpy(msg.sender, client.username);
    
    send_message(client.nm_sock, &msg);
    
    Message response;
    recv_message(client.nm_sock, &response);
    
    if (response.status == SUCCESS) {
        printf("%s", response.data);
    } else {
        print_error(response.status);
    }
}

void handle_addaccess(char *flag, char *filename, char *username) {
    Message msg;
    init_message(&msg);
    msg.type = MSG_ADDACCESS;
    strcpy(msg.sender, client.username);
    strcpy(msg.filename, filename);
    strcpy(msg.target_user, username);
    
    if (strcmp(flag, "-R") == 0) {
        msg.access = ACCESS_READ;
    } else if (strcmp(flag, "-W") == 0) {
        msg.access = ACCESS_READWRITE;
    } else {
        printf("Invalid flag. Use -R for read or -W for write\n");
        return;
    }
    
    send_message(client.nm_sock, &msg);
    
    Message response;
    recv_message(client.nm_sock, &response);
    
    if (response.status == SUCCESS) {
        printf("Access granted successfully!\n");
    } else {
        print_error(response.status);
    }
}

void handle_remaccess(char *filename, char *username) {
    Message msg;
    init_message(&msg);
    msg.type = MSG_REMACCESS;
    strcpy(msg.sender, client.username);
    strcpy(msg.filename, filename);
    strcpy(msg.target_user, username);
    
    send_message(client.nm_sock, &msg);
    
    Message response;
    recv_message(client.nm_sock, &response);
    
    if (response.status == SUCCESS) {
        printf("Access removed successfully!\n");
    } else {
        print_error(response.status);
    }
}

void handle_exec(char *filename) {
    Message msg;
    init_message(&msg);
    msg.type = MSG_EXEC;
    strcpy(msg.sender, client.username);
    strcpy(msg.filename, filename);
    
    send_message(client.nm_sock, &msg);
    
    Message response;
    recv_message(client.nm_sock, &response);
    
    if (response.status == SUCCESS) {
        printf("%s", response.data);
    } else {
        print_error(response.status);
    }
}

void handle_undo(char *filename) {
    Message msg;
    init_message(&msg);
    msg.type = MSG_UNDO;
    strcpy(msg.sender, client.username);
    strcpy(msg.filename, filename);
    
    send_message(client.nm_sock, &msg);
    
    Message response;
    recv_message(client.nm_sock, &response);
    
    if (response.status != SUCCESS) {
        print_error(response.status);
        return;
    }
    
    // Connect to SS
    int ss_sock = connect_to_ss(response.data);
    if (ss_sock < 0) {
        printf("Error: Could not connect to storage server\n");
        return;
    }
    
    // Send undo request
    init_message(&msg);
    msg.type = MSG_UNDO;
    strcpy(msg.filename, filename);
    strcpy(msg.sender, client.username);
    
    send_message(ss_sock, &msg);
    recv_message(ss_sock, &response);
    
    if (response.status == SUCCESS) {
        printf("Undo successful!\n");
    } else {
        print_error(response.status);
    }
    
    close(ss_sock);
}

void handle_requestaccess(char *flag, char *filename) {
    Message msg;
    init_message(&msg);
    msg.type = MSG_REQUESTACCESS;
    strcpy(msg.sender, client.username);
    strcpy(msg.filename, filename);
    
    if (strcmp(flag, "-R") == 0) {
        msg.access = ACCESS_READ;
    } else if (strcmp(flag, "-W") == 0) {
        msg.access = ACCESS_READWRITE;
    } else {
        printf("Invalid flag. Use -R for read or -W for write\n");
        return;
    }
    
    send_message(client.nm_sock, &msg);
    
    Message response;
    recv_message(client.nm_sock, &response);
    
    if (response.status == SUCCESS) {
        printf("Access request sent successfully!\n");
        if (strlen(response.data) > 0) {
            printf("%s\n", response.data);
        }
    } else {
        print_error(response.status);
    }
}

void handle_viewrequests() {
    Message msg;
    init_message(&msg);
    msg.type = MSG_VIEWREQUESTS;
    strcpy(msg.sender, client.username);
    
    send_message(client.nm_sock, &msg);
    
    Message response;
    recv_message(client.nm_sock, &response);
    
    if (response.status == SUCCESS) {
        printf("Pending Access Requests:\n%s", response.data);
    } else {
        print_error(response.status);
    }
}

void handle_approverequest(char *request_id_str) {
    int request_id = atoi(request_id_str);
    
    Message msg;
    init_message(&msg);
    msg.type = MSG_APPROVEREQUEST;
    strcpy(msg.sender, client.username);
    msg.sentence_index = request_id;
    
    send_message(client.nm_sock, &msg);
    
    Message response;
    recv_message(client.nm_sock, &response);
    
    if (response.status == SUCCESS) {
        printf("Access request approved successfully!\n");
    } else {
        print_error(response.status);
    }
}

void handle_denyrequest(char *request_id_str) {
    int request_id = atoi(request_id_str);
    
    Message msg;
    init_message(&msg);
    msg.type = MSG_DENYREQUEST;
    strcpy(msg.sender, client.username);
    msg.sentence_index = request_id;
    
    send_message(client.nm_sock, &msg);
    
    Message response;
    recv_message(client.nm_sock, &response);
    
    if (response.status == SUCCESS) {
        printf("Access request denied successfully!\n");
    } else {
        print_error(response.status);
    }
}

void command_loop() {
    char line[MAX_BUFFER];
    
    printf("\nWelcome %s! Type commands (or 'help' for list, 'exit' to quit):\n", client.username);
    
    while (1) {
        printf("\n> ");
        // this flushes the buffer - S
        fflush(stdout);

        // set up the file descriptor set - S
        fd_set rfds;
        FD_ZERO(&rfds);
        FD_SET(STDIN_FILENO, &rfds);

        if(sig_pipe[0] != -1) {
            FD_SET(sig_pipe[0], &rfds);
        }
        int maxfd = (sig_pipe[0] > STDIN_FILENO) ? sig_pipe[0] : STDIN_FILENO;

        // set up select - S
        int sel = select(maxfd + 1, &rfds, NULL, NULL, NULL);
        if(sel<0)
        {
            if(errno == EINTR) continue;
            perror("select");
            break;
        }

        // if we received an interrupt, break out of the loop
        if(sig_pipe[0] != -1 && FD_ISSET(sig_pipe[0], &rfds)) {
            char buf[64];
            while(read(sig_pipe[0], buf, sizeof(buf)) > 0) {}
            perform_pending_unlock();
            printf("\nReceived interrupt, exiting client.\n");
            break;
        }

        // else if no signal, check for stdin input - S
        if(FD_ISSET(STDIN_FILENO, &rfds)){

        
        if (!fgets(line, sizeof(line), stdin)) {
            break;
        }
        
        trim_whitespace(line);
        
        if (strlen(line) == 0) {
            continue;
        }

        char* buf = line;
        char** argv = NULL;
        int argc_local = 0;
        int cap = 0;
        char* saveptr = NULL;
        char* tok = strtok_r(buf, " \t", &saveptr);
        while(tok)
        {
            if(argc_local + 1 >= cap)
            {
                int newcap = cap? cap * 2 : 8;
                char** tmp = realloc(argv, newcap * sizeof(char*));
                if(!tmp) { free(argv); argv=NULL; cap=0; break; }
                argv = tmp;
                cap = newcap;
            }
            argv[argc_local++] = tok;
            tok = strtok_r(NULL, " \t", &saveptr);
        }

        if(!argv) { continue; }
        argv[argc_local] = NULL;
        if(argc_local < 1) { free(argv); continue; }

        char tail[MAX_BUFFER] = "";
        if(argc_local > 1)
        {
            size_t off = 0;
            for(int i = 1; i < argc_local; i++)
            {
                int n = snprintf(tail + off, sizeof(tail) - off, "%s%s", (i == 1) ? " " : "", argv[i]);
                if(n<0) break;
                off += (size_t)n;
                if(off >= sizeof(tail)-1) break;
            }
        }

        char* cmd = argv[0];
        
        // Parse command
        // char cmd[64], arg1[MAX_FILENAME], arg2[MAX_FILENAME], arg3[MAX_USERNAME];
        // int argc = sscanf(line, "%s %s %s %s", cmd, arg1, arg2, arg3);
        
        // if (argc < 1) {
        //     continue;
        // }
        
        if (strcmp(cmd, "exit") == 0 || strcmp(cmd, "quit") == 0) {
            printf("Goodbye!\n");
            free(argv);
            break;
        } else if (strcmp(cmd, "help") == 0) {
            printf("Available commands:\n");
            printf("  VIEW [-a] [-l] [-al]  - List files\n");
            printf("  READ <filename>       - Read file content\n");
            printf("  CREATE <filename>     - Create new file\n");
            printf("  WRITE <filename> <sent_idx> - Write to file\n");
            printf("  DELETE <filename>     - Delete file\n");
            printf("  INFO <filename>       - Get file information\n");
            printf("  STREAM <filename>     - Stream file content\n");
            printf("  LIST                  - List all users\n");
            printf("  ADDACCESS -R|-W <filename> <username> - Add access\n");
            printf("  REMACCESS <filename> <username> - Remove access\n");
            printf("  EXEC <filename>       - Execute file as commands\n");
            printf("  UNDO <filename>       - Undo last change\n");
            printf("  CREATEFOLDER <foldername> [parent_path] - Create new folder\n");
            printf("  MOVE <filename> <foldername> - Move file to folder\n");
            printf("  VIEWFOLDER <foldername>  - View folder contents\n");
            printf("  CHECKPOINT <filename> <tag> - Create checkpoint\n");
            printf("  VIEWCHECKPOINT <filename> <tag> - View checkpoint content\n");
            printf("  REVERT <filename> <tag> - Revert to checkpoint\n");
            printf("  LISTCHECKPOINTS <filename> - List all checkpoints\n");
            printf("  REQUESTACCESS -R|-W <filename> - Request file access\n");
            printf("  VIEWREQUESTS          - View pending access requests\n");
            printf("  APPROVEREQUEST <id>   - Approve access request\n");
            printf("  DENYREQUEST <id>      - Deny access request\n");
            printf("  exit                  - Exit client\n");
        } else if (strcmp(cmd, "VIEW") == 0) {
            handle_view(argc_local > 1 ? tail : NULL);
        } else if (strcmp(cmd, "READ") == 0) {
            if (argc_local < 2) {
                printf("Usage: READ <filename>\n");
            } else {
                handle_read(argv[1]);
            }
        } else if (strcmp(cmd, "CREATE") == 0) {
            if (argc_local < 2) {
                printf("Usage: CREATE <filename>\n");
            } else {
                handle_create(argv[1]);
            }
        } else if (strcmp(cmd, "WRITE") == 0) {
            if (argc_local < 3) {
                printf("Usage: WRITE <filename> <sentence_index>\n");
            } else {
                handle_write(argv[1], argv[2]);
            }
        } else if (strcmp(cmd, "DELETE") == 0) {
            if (argc_local < 2) {
                printf("Usage: DELETE <filename>\n");
            } else {
                handle_delete(argv[1]);
            }
        } else if (strcmp(cmd, "INFO") == 0) {
            if (argc_local < 2) {
                printf("Usage: INFO <filename>\n");
            } else {
                handle_info(argv[1]);
            }
        } else if (strcmp(cmd, "STREAM") == 0) {
            if (argc_local < 2) {
                printf("Usage: STREAM <filename>\n");
            } else {
                handle_stream(argv[1]);
            }
        } else if (strcmp(cmd, "LIST") == 0) {
            handle_list();
        } else if (strcmp(cmd, "ADDACCESS") == 0) {
            if (argc_local < 4) {
                printf("Usage: ADDACCESS -R|-W <filename> <username>\n");
            } else {
                handle_addaccess(argv[1], argv[2], argv[3]);
            }
        } else if (strcmp(cmd, "REMACCESS") == 0) {
            if (argc_local < 3) {
                printf("Usage: REMACCESS <filename> <username>\n");
            } else {
                handle_remaccess(argv[1], argv[2]);
            }
        } else if (strcmp(cmd, "EXEC") == 0) {
            if (argc_local < 2) {
                printf("Usage: EXEC <filename>\n");
            } else {
                handle_exec(argv[1]);
            }
        } else if (strcmp(cmd, "UNDO") == 0) {
            if (argc_local < 2) {
                printf("Usage: UNDO <filename>\n");
            } else {
                handle_undo(argv[1]);
            }
        } else if (strcmp(cmd, "CREATEFOLDER") == 0) {
            if (argc_local < 2) {
                printf("Usage: CREATEFOLDER <foldername> [parent_path]\n");
            } else {
                handle_createfolder(argv[1], argc_local > 2 ? argv[2] : NULL);
            }
        } else if (strcmp(cmd, "MOVE") == 0) {
            if (argc_local < 3) {
                printf("Usage: MOVE <filename> <foldername>\n");
            } else {
                handle_move_file(argv[1], argv[2]);
            }
        } else if (strcmp(cmd, "VIEWFOLDER") == 0) {
            if (argc_local < 2) {
                printf("Usage: VIEWFOLDER <foldername>\n");
            } else {
                handle_viewfolder(argv[1]);
            }
        } else if (strcmp(cmd, "CHECKPOINT") == 0) {
            if (argc_local < 3) {
                printf("Usage: CHECKPOINT <filename> <tag>\n");
            } else {
                handle_checkpoint(argv[1], argv[2]);
            }
        } else if (strcmp(cmd, "VIEWCHECKPOINT") == 0) {
            if (argc_local < 3) {
                printf("Usage: VIEWCHECKPOINT <filename> <tag>\n");
            } else {
                handle_viewcheckpoint(argv[1], argv[2]);
            }
        } else if (strcmp(cmd, "REVERT") == 0) {
            if (argc_local < 3) {
                printf("Usage: REVERT <filename> <tag>\n");
            } else {
                handle_revert(argv[1], argv[2]);
            }
        } else if (strcmp(cmd, "LISTCHECKPOINTS") == 0) {
            if (argc_local < 2) {
                printf("Usage: LISTCHECKPOINTS <filename>\n");
            } else {
                handle_listcheckpoints(argv[1]);
            }
        } else if (strcmp(cmd, "REQUESTACCESS") == 0) {
            if (argc_local < 3) {
                printf("Usage: REQUESTACCESS -R|-W <filename>\n");
            } else {
                handle_requestaccess(argv[1], argv[2]);
            }
        } else if (strcmp(cmd, "VIEWREQUESTS") == 0) {
            handle_viewrequests();
        } else if (strcmp(cmd, "APPROVEREQUEST") == 0) {
            if (argc_local < 2) {
                printf("Usage: APPROVEREQUEST <request_id>\n");
            } else {
                handle_approverequest(argv[1]);
            }
        } else if (strcmp(cmd, "DENYREQUEST") == 0) {
            if (argc_local < 2) {
                printf("Usage: DENYREQUEST <request_id>\n");
            } else {
                handle_denyrequest(argv[1]);
            }
        } else {
            printf("Unknown command: %s\n", cmd);
            printf("Type 'help' for list of commands\n");
        }
        // Free memory - S 
        free(argv);
    }

    }
}

// Allow the user client to set ip and port of the nm server to connect to at registration, specified to be known in the docs - N
int main(int argc, char *argv[]) {

    // set up signal handling pipe - S
    if(pipe(sig_pipe)<0) {
        perror("pipe");
        return 1;
    }

    // set pipe to non-blocking to avoid potential deadlocks - S
    int flags = fcntl(sig_pipe[0], F_GETFL, 0);
    if (flags>=0) fcntl(sig_pipe[0], F_SETFL, flags | O_NONBLOCK);
    flags = fcntl(sig_pipe[1], F_GETFL, 0);
    if (flags>=0) fcntl(sig_pipe[1], F_SETFL, flags | O_NONBLOCK);
    
    // Set up signal handler - S
    struct sigaction sa;
    sa.sa_handler = sigint_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    sigaction(SIGINT, &sa, NULL);

    signal(SIGPIPE, SIG_IGN); // Ignore SIGPIPE to handle broken pipes manually (resolves crashes on SS failure) - S

    if (argc != 3) {
        printf("Usage: %s <nm_ip> <nm_port>\n", argv[0]);
        printf("Example: %s 127.0.0.1 8081\n", argv[0]);
        return 1;
    }
    
    // Parse command-line arguments
    strncpy(client.nm_ip, argv[1], INET_ADDRSTRLEN - 1);
    client.nm_ip[INET_ADDRSTRLEN - 1] = '\0';
    client.nm_port = atoi(argv[2]);
    
    // Validate port
    if (client.nm_port <= 0 || client.nm_port > 65535) {
        printf("Error: Invalid port number. Must be between 1 and 65535.\n");
        return 1;
    }
    else if (client.nm_port != 8081) {
        printf("Error: To register as a user, you must register under port 8081.\n");
        return 1;
    }
    
    init_client();
    connect_to_nm();
    command_loop();
    
    close(client.nm_sock);
    close_logger();  

    if(sig_pipe[0] != -1) close(sig_pipe[0]);
    if(sig_pipe[1] != -1) close(sig_pipe[1]);
    return 0;
}
