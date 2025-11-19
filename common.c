#include "common.h"
#include <sys/time.h> 
#include <errno.h> // For errno - S

// Set socket timeouts to prevent indefinite blocking - N
int set_socket_timeouts(int sock, int send_timeout_sec, int recv_timeout_sec) {
    struct timeval tv;
    
    // Set send timeout - N
    tv.tv_sec = send_timeout_sec;
    tv.tv_usec = 0;
    if (setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv)) < 0) {
        return -1;
    }
    
    // Set receive timeout - N
    tv.tv_sec = recv_timeout_sec;
    tv.tv_usec = 0;
    if (setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv)) < 0) {
        return -1;
    }
    
    return 0;
}

void init_message(Message *msg) {
    memset(msg, 0, sizeof(Message));
    msg->status = SUCCESS;
    msg->type = MSG_ACK;
    msg->sentence_index = -1;
    msg->word_index = -1;
    msg->ss_id = -1;
    msg->access = ACCESS_NONE;
}

// Account for the nm_port and client_port fields - N
// Both serialize and deserialize functions updated accordingly

void serialize_message(Message *msg, char *buffer) {
    // Serialize in this order: - N
    // 0: type
    // 1: status
    // 2: sender
    // 3: filename
    // 4: foldername
    // 5: target_path
    // 6: sentence_index
    // 7: word_index
    // 8: ss_id
    // 9: client_port (NEW)
    // 10: nm_port (NEW)
    // 11: access
    // 12: target_user
    // 14: checkpoint stuff
    // 13: data (LAST - can contain anything including |)
    
    sprintf(buffer, "%d|%d|%s|%s|%s|%s|%d|%d|%d|%d|%d|%d|%s|%s|%s",
            msg->type,           // 0
            msg->status,         // 1
            msg->sender,         // 2
            msg->filename,       // 3
            msg->foldername,     // 4
            msg->target_path,    // 5
            msg->sentence_index, // 6
            msg->word_index,     // 7
            msg->ss_id,          // 8
            msg->client_port,    // 9
            msg->nm_port,        // 10
            msg->access,         // 11
            msg->target_user,    // 12
            msg->checkpoint_tag, // 13 (NEW)
            msg->data);          // 14 (LAST)
}

void deserialize_message(char *buffer, Message *msg) {
    init_message(msg);
    char *p = buffer;
    int field = 0;

    while (field < 15 && p) {  // Changed from 10 to 12
        char *sep;
        size_t len;
        
        // CRITICAL: For the last field (data at field 11), take everything remaining
        if (field == 14) {
            sep = NULL;  // No more separators
            len = strlen(p);
        } else {
            sep = strchr(p, '|');
            len = sep ? (size_t)(sep - p) : strlen(p);
        }

        /* create a temporary null-terminated token (may be empty) */
        char token_buf[MAX_BUFFER];
        if (len > sizeof(token_buf) - 1) len = sizeof(token_buf) - 1;
        memcpy(token_buf, p, len);
        token_buf[len] = '\0';

        if (len > 0) {
            switch (field) {
                case 0: msg->type = atoi(token_buf); break;
                case 1: msg->status = atoi(token_buf); break;
                case 2: 
                    strncpy(msg->sender, token_buf, MAX_USERNAME-1); 
                    msg->sender[MAX_USERNAME-1] = '\0'; 
                    break;
                case 3: 
                    strncpy(msg->filename, token_buf, MAX_FILENAME-1); 
                    msg->filename[MAX_FILENAME-1] = '\0'; 
                    break;
                case 4:
                    strncpy(msg->foldername, token_buf, MAX_FILENAME-1); 
                    msg->filename[MAX_FILENAME-1] = '\0'; 
                    break; 
                case 5:
                    strncpy(msg->target_path, token_buf, MAX_FILENAME-1); 
                    msg->filename[MAX_FILENAME-1] = '\0'; 
                    break; 
                case 6: msg->sentence_index = atoi(token_buf); break;
                case 7: msg->word_index = atoi(token_buf); break;
                case 8: msg->ss_id = atoi(token_buf); break;
                case 9: msg->client_port = atoi(token_buf); break;  // NEW
                case 10: msg->nm_port = atoi(token_buf); break;      // NEW
                case 11: msg->access = atoi(token_buf); break;
                case 12: 
                    strncpy(msg->target_user, token_buf, MAX_USERNAME-1); 
                    msg->target_user[MAX_USERNAME-1] = '\0'; 
                    break;
                case 13:
                    strncpy(msg->checkpoint_tag, token_buf, MAX_USERNAME-1); 
                    msg->sender[MAX_USERNAME-1] = '\0'; 
                    break;
                case 14: 
                    strncpy(msg->data, token_buf, MAX_BUFFER-1); 
                    msg->data[MAX_BUFFER-1] = '\0'; 
                    break;
            }
        } else {
            /* empty token: keep defaults from init_message() for numeric fields,
               and leave strings as empty (already zeroed by init_message). */
            if (field == 2) msg->sender[0] = '\0';
            if (field == 3) msg->filename[0] = '\0';
            if (field == 10) msg->target_user[0] = '\0';
            if (field == 11) msg->data[0] = '\0';
        }

        field++;
        if (!sep) break;
        p = sep + 1;
    }
}

int send_message(int sock, Message *msg) {
    char buffer[MAX_BUFFER * 2];
    serialize_message(msg, buffer);
    
    int len = strlen(buffer);
    int total_sent = 0;
    
    // Send length first
    // if (send(sock, &len, sizeof(int), 0) < 0) {
    //     return -1;
    // }

    // Send length first, with MSG_NOSIGNAL to prevent SIGPIPE - S
    // if there is no MSG_NOSIGNAL, the program may terminate unexpectedly because of SIGPIPE
    // Reference:
    //Normally, if you try to send on a TCP socket that has been closed by the peer, the kernel detects that the connection is broken and triggers a SIGPIPE signal to your process.
    //By default, SIGPIPE kills your process.
    int sent = send(sock, &len, sizeof(int), MSG_NOSIGNAL);
    if (sent<0)
    {
        return -1;
    }
    
    // Send data
    // while (total_sent < len) {
    //     int sent = send(sock, buffer + total_sent, len - total_sent, 0);
    //     if (sent < 0) {
    //         return -1;
    //     }
    //     total_sent += sent;
    // }

    // Send data with MSG_NOSIGNAL to prevent SIGPIPE - S
    while (total_sent < len) {
        int s = send(sock, buffer + total_sent, len - total_sent, MSG_NOSIGNAL);
        if (s < 0) {
            if(errno == EINTR) continue;
            return -1;
        }
        total_sent += s;
    }
    
    return 0;
}

int recv_message(int sock, Message *msg) {
    int len;
    
    // Receive length first
    int received = recv(sock, &len, sizeof(int), MSG_WAITALL);
    if (received <= 0) {
        return -1;
    }
    
    if (len >= MAX_BUFFER * 2 || len <= 0) {
        return -1;
    }
    
    // Receive data
    char buffer[MAX_BUFFER * 2];
    int total_received = 0;
    
    while (total_received < len) {
        received = recv(sock, buffer + total_received, len - total_received, 0);
        if (received <= 0) {
            return -1;
        }
        total_received += received;
    }
    
    buffer[len] = '\0';
    deserialize_message(buffer, msg);
    
    return 0;
}

char* get_timestamp() {
    static char timestamp[64];
    time_t now = time(NULL);
    struct tm *tm_info = localtime(&now); //changed localtime_r to localtime - S
    strftime(timestamp, sizeof(timestamp), "%Y-%m-%d %H:%M:%S", tm_info);
    return timestamp;
}

void trim_whitespace(char *str) {
    char *end;
    
    // Trim leading space
    while (*str == ' ' || *str == '\t' || *str == '\n' || *str == '\r') {
        str++;
    }
    
    if (*str == 0) return;
    
    // Trim trailing space
    end = str + strlen(str) - 1;
    while (end > str && (*end == ' ' || *end == '\t' || *end == '\n' || *end == '\r')) {
        end--;
    }
    
    *(end + 1) = '\0';
}
