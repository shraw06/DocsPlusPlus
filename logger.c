#include "logger.h"
#include <stdarg.h>

static FILE *log_file = NULL;
static FILE *common_log = NULL;  // Common logs.txt for all instances
static pthread_mutex_t log_mutex = PTHREAD_MUTEX_INITIALIZER;
static char instance_name[64] = "UNKNOWN";  // Store instance name (NM, SS_xxx, Client_xxx)

const char* log_level_str(LogLevel level) {
    switch(level) {
        case LOG_INFO: return "INFO";
        case LOG_WARNING: return "WARN";
        case LOG_ERROR: return "ERROR";
        case LOG_DEBUG: return "DEBUG";
        case LOG_REQUEST: return "REQ";
        case LOG_RESPONSE: return "RESP";
        default: return "UNKNOWN";
    }
}

int init_logger(const char *log_filename) {
    pthread_mutex_lock(&log_mutex);
    
    // Open instance-specific log file
    log_file = fopen(log_filename, "a");
    if (!log_file) {
        pthread_mutex_unlock(&log_mutex);
        return -1;
    }
    
    // Open common logs.txt (append mode, created if doesn't exist)
    common_log = fopen("logs.txt", "a");
    if (!common_log) {
        fclose(log_file);
        log_file = NULL;
        pthread_mutex_unlock(&log_mutex);
        return -1;
    }
    
    fprintf(log_file, "\n=== Log Started at %s ===\n", get_timestamp());
    fflush(log_file);
    
    fprintf(common_log, "\n=== %s Log Started at %s ===\n", instance_name, get_timestamp());
    fflush(common_log);
    
    pthread_mutex_unlock(&log_mutex);
    return 0;
}

void set_instance_name(const char *name) {
    pthread_mutex_lock(&log_mutex);
    strncpy(instance_name, name, sizeof(instance_name) - 1);
    instance_name[sizeof(instance_name) - 1] = '\0';
    pthread_mutex_unlock(&log_mutex);
}

void close_logger() {
    pthread_mutex_lock(&log_mutex);
    
    if (log_file) {
        fprintf(log_file, "=== Log Closed at %s ===\n\n", get_timestamp());
        fclose(log_file);
        log_file = NULL;
    }
    
    if (common_log) {
        fprintf(common_log, "=== %s Log Closed at %s ===\n\n", instance_name, get_timestamp());
        fclose(common_log);
        common_log = NULL;
    }
    
    pthread_mutex_unlock(&log_mutex);
}

void log_message(LogLevel level, const char *ip, int port, 
                 const char *username, const char *operation, 
                 const char *status, const char *details) {
    pthread_mutex_lock(&log_mutex);
    
    char log_entry[MAX_BUFFER * 2];
    snprintf(log_entry, sizeof(log_entry),
             "[%s] [%s] [%s] [%s:%d] [User: %s] [Op: %s] [Status: %s] %s",
             get_timestamp(),
             instance_name,
             log_level_str(level),
             ip ? ip : "N/A",
             port,
             username ? username : "N/A",
             operation ? operation : "N/A",
             status ? status : "N/A",
             details ? details : "");
    
    if (log_file) {
        fprintf(log_file, "%s\n", log_entry);
        fflush(log_file);
    }
    
    if (common_log) {
        fprintf(common_log, "%s\n", log_entry);
        fflush(common_log);
    }
    
    pthread_mutex_unlock(&log_mutex);
}

void log_formatted(LogLevel level, const char *format, ...) {
    pthread_mutex_lock(&log_mutex);
    
    char message[MAX_BUFFER];
    va_list args;
    va_start(args, format);
    vsnprintf(message, sizeof(message), format, args);
    va_end(args);
    
    char log_entry[MAX_BUFFER * 2];
    snprintf(log_entry, sizeof(log_entry), "[%s] [%s] [%s] %s",
             get_timestamp(), instance_name, log_level_str(level), message);
    
    if (log_file) {
        fprintf(log_file, "%s\n", log_entry);
        fflush(log_file);
    }
    
    if (common_log) {
        fprintf(common_log, "%s\n", log_entry);
        fflush(common_log);
    }
    
    pthread_mutex_unlock(&log_mutex);
}

void display_and_log(const char *message) {
    char formatted_msg[MAX_BUFFER * 2];
    snprintf(formatted_msg, sizeof(formatted_msg), "[%s] %s", instance_name, message);
    
    printf("%s\n", formatted_msg);
    log_formatted(LOG_INFO, "%s", message);
}
