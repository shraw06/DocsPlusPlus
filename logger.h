#ifndef LOGGER_H
#define LOGGER_H

#include "common.h"

typedef enum {
    LOG_INFO,
    LOG_WARNING,
    LOG_ERROR,
    LOG_DEBUG,
    LOG_REQUEST,
    LOG_RESPONSE
} LogLevel;

// Initialize logger with filename
int init_logger(const char *log_filename);

// Set instance name (NM, SS_xxxx, Client_xxxx)
void set_instance_name(const char *name);

// Close logger
void close_logger();

// Log a message
void log_message(LogLevel level, const char *ip, int port, 
                 const char *username, const char *operation, 
                 const char *status, const char *details);

// Log with format string
void log_formatted(LogLevel level, const char *format, ...);

// Display message to console and log
void display_and_log(const char *message);

#endif // LOGGER_H