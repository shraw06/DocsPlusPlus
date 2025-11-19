#include "file_ops.h"
#include "logger.h"
#include <ctype.h>

FileContent* init_file_content() {
    FileContent *fc = malloc(sizeof(FileContent));
    fc->capacity = 10;
    fc->sentence_count = 0;
    fc->sentences = malloc(sizeof(Sentence) * fc->capacity);
    return fc;
}

static int is_space_token(const char *token) {
    if (!token || token[0] == '\0') return 0;
    for (int i = 0; token[i] != '\0'; i++) {
        if (token[i] != ' ' && token[i] != '\t' && token[i] != '\r') {
            return 0;
        }
    }
    return 1;
}

static int is_newline_token(const char *token) {
    return (token && token[0] == '\n' && token[1] == '\0');
}

static int should_skip_for_indexing(const char *token) {
    return is_space_token(token) || is_newline_token(token);
}

void free_file_content(FileContent *fc) {
    if (!fc) return;
    for (int i = 0; i < fc->sentence_count; i++) {
        for (int j = 0; j < fc->sentences[i].word_count; j++) {
            free(fc->sentences[i].words[j]);
        }
        free(fc->sentences[i].words);
    }
    free(fc->sentences);
    free(fc);
}

int is_delimiter(char c) {
    return (c == '.' || c == '!' || c == '?');
}

char** split_by_delimiters(const char *word, int *count) {
    int cap = 10;
    char **parts = malloc(sizeof(char*) * cap);
    *count = 0;
    
    char buffer[MAX_WORD];
    int buf_idx = 0;
    
    for (int i = 0; word[i] != '\0'; i++) {
        // SPLIT ON SPACES/TABS
        if (word[i] == ' ' || word[i] == '\t' || word[i] == '\r') {
            // Save current buffer if not empty
            if (buf_idx > 0) {
                buffer[buf_idx] = '\0';
                parts[*count] = strdup(buffer);
                (*count)++;
                buf_idx = 0;
                if (*count >= cap) {
                    cap *= 2;
                    parts = realloc(parts, sizeof(char*) * cap);
                }
            }
            
            // Collect consecutive spaces
            char space_buf[MAX_WORD];
            int space_idx = 0;
            space_buf[space_idx++] = word[i];
            
            // Look ahead for more spaces
            while (word[i+1] && (word[i+1] == ' ' || word[i+1] == '\t' || word[i+1] == '\r')) {
                i++;
                if (space_idx < MAX_WORD - 1) {
                    space_buf[space_idx++] = word[i];
                }
            }
            
            // Save space token
            space_buf[space_idx] = '\0';
            if (*count >= cap) {
                cap *= 2;
                parts = realloc(parts, sizeof(char*) * cap);
            }
            
            parts[*count] = strdup(space_buf);
            (*count)++;
        }
        else if (word[i] == '\n') {
            /* Save current buffer if not empty (trim whitespace) */
            if (buf_idx > 0) {
                buffer[buf_idx] = '\0';
                /* trim trailing and leading whitespace from buffer */
                // int start = 0; while (buffer[start] && (buffer[start] == ' ' || buffer[start] == '\t' || buffer[start] == '\r')) start++;
                // int end = buf_idx - 1; while (end >= start && (buffer[end] == ' ' || buffer[end] == '\t' || buffer[end] == '\r')) end--;
                // buffer[end+1] = '\0';
                // if (start == 0) {
                //     parts[*count] = strdup(buffer);
                // } else {
                //     parts[*count] = strdup(buffer + start);
                // }

                //not trimming spaces - S
                parts[*count] = strdup(buffer);
                (*count)++;
                buf_idx = 0;
                if (*count >= cap) {
                    cap *= 2;
                    parts = realloc(parts, sizeof(char*) * cap);
                }
            }

            /* Save newline as separate token */
            if (*count >= cap) {
                cap *= 2;
                parts = realloc(parts, sizeof(char*) * cap);
            }
            parts[*count] = strdup("\n");
            (*count)++;
        } else if (is_delimiter(word[i])) {
            // Save current buffer if not empty
            if (buf_idx > 0) {
                buffer[buf_idx] = '\0';
                /* trim buffer whitespace */
                // int start = 0; while (buffer[start] && (buffer[start] == ' ' || buffer[start] == '\t' || buffer[start] == '\r')) start++;
                // int end = buf_idx - 1; while (end >= start && (buffer[end] == ' ' || buffer[end] == '\t' || buffer[end] == '\r')) end--;
                // buffer[end+1] = '\0';
                // if (start == 0) parts[*count] = strdup(buffer); else parts[*count] = strdup(buffer + start);

                //not trimming spaces - S
                parts[*count] = strdup(buffer);
                (*count)++;
                buf_idx = 0;
                
                if (*count >= cap) {
                    cap *= 2;
                    parts = realloc(parts, sizeof(char*) * cap);
                }
            }
            
            // Save delimiter as separate word
            buffer[0] = word[i];
            buffer[1] = '\0';
            if (*count >= cap) {
                cap *= 2;
                parts = realloc(parts, sizeof(char*) * cap);
            }
            parts[*count] = strdup(buffer);
            (*count)++;
        } else {
            buffer[buf_idx++] = word[i];
        }
    }
    
    // Save remaining buffer
    if (buf_idx > 0) {
        buffer[buf_idx] = '\0';
        /* trim buffer whitespace */
        // int start = 0; while (buffer[start] && (buffer[start] == ' ' || buffer[start] == '\t' || buffer[start] == '\r')) start++;
        // int end = buf_idx - 1; while (end >= start && (buffer[end] == ' ' || buffer[end] == '\t' || buffer[end] == '\r')) end--;
        // buffer[end+1] = '\0';
        // if (start == 0) parts[*count] = strdup(buffer); else parts[*count] = strdup(buffer + start);

        //not trimming spaces - S
        if (*count >= cap) {
            cap *= 2;
            parts = realloc(parts, sizeof(char*) * cap);
        }
        parts[*count] = strdup(buffer);
        (*count)++;
    }

    for (int i = 0; i < *count; i++) {
        //printf("DEBUG split_by_delimiters part[%d] = '%s' (len=%zu, is_space=%d)\n", 
            //i, parts[i], strlen(parts[i]), is_space_token(parts[i]));
    }
    
    return parts;
}

int parse_file(const char *filepath, FileContent *fc) {
    FILE *file = fopen(filepath, "r");
    if (!file) return -1;
    
    fc->sentence_count = 0;
    
    // char buffer[MAX_BUFFER];
    char word[MAX_WORD];
    int word_idx = 0;
    
    // Initialize first sentence
    if (fc->sentence_count >= fc->capacity) {
        fc->capacity *= 2;
        fc->sentences = realloc(fc->sentences, sizeof(Sentence) * fc->capacity);
    }
    fc->sentences[fc->sentence_count].capacity = 10;
    fc->sentences[fc->sentence_count].word_count = 0;
    fc->sentences[fc->sentence_count].words = malloc(sizeof(char*) * fc->sentences[fc->sentence_count].capacity);
    
    int current_sent = 0;
    int c;
    
    while ((c = fgetc(file)) != EOF) {
        if (c == ' ' || c == '\t' || c == '\r') {
            if (word_idx > 0) {
                word[word_idx] = '\0';
                
                // Add word to current sentence
                Sentence *sent = &fc->sentences[current_sent];
                if (sent->word_count >= sent->capacity) {
                    sent->capacity *= 2;
                    sent->words = realloc(sent->words, sizeof(char*) * sent->capacity);
                }
                sent->words[sent->word_count++] = strdup(word);
                
                word_idx = 0;
            }

            // Collect consecutive spaces/tabs as a single token
            char space_buf[MAX_WORD];
            int space_idx = 0;
            space_buf[space_idx++] = c;
            
            // Peek ahead for more spaces/tabs (but not newlines)
            int next;
            while ((next = fgetc(file)) != EOF) {
                if (next == ' ' || next == '\t' || next == '\r') {
                    if (space_idx < MAX_WORD - 1) {
                        space_buf[space_idx++] = next;
                    }
                } else {
                    ungetc(next, file);  // Put it back
                    break;
                }
            }
            
            // Save the spaces as a token
            space_buf[space_idx] = '\0';
            Sentence *sent = &fc->sentences[current_sent];
            if (sent->word_count >= sent->capacity) {
                sent->capacity *= 2;
                sent->words = realloc(sent->words, sizeof(char*) * sent->capacity);
            }
            sent->words[sent->word_count++] = strdup(space_buf);

        } else if (c == '\n') {
            /* Treat newline as an explicit token so we can preserve it when
               writing files back. We do NOT start a new sentence for a
               newline; it's an in-sentence break. */
            if (word_idx > 0) {
                word[word_idx] = '\0';
                Sentence *sent = &fc->sentences[current_sent];
                if (sent->word_count >= sent->capacity) {
                    sent->capacity *= 2;
                    sent->words = realloc(sent->words, sizeof(char*) * sent->capacity);
                }
                sent->words[sent->word_count++] = strdup(word);
                word_idx = 0;
            }

            /* Add newline token (single '\n' char) */
            word[0] = '\n';
            word[1] = '\0';
            Sentence *sent_nl = &fc->sentences[current_sent];
            if (sent_nl->word_count >= sent_nl->capacity) {
                sent_nl->capacity *= 2;
                sent_nl->words = realloc(sent_nl->words, sizeof(char*) * sent_nl->capacity);
            }
            sent_nl->words[sent_nl->word_count++] = strdup(word);
        } else if (is_delimiter(c)) {
            // Save current word if exists
            if (word_idx > 0) {
                word[word_idx] = '\0';
                Sentence *sent = &fc->sentences[current_sent];
                if (sent->word_count >= sent->capacity) {
                    sent->capacity *= 2;
                    sent->words = realloc(sent->words, sizeof(char*) * sent->capacity);
                }
                sent->words[sent->word_count++] = strdup(word);
                word_idx = 0;
            }
            
            // Add delimiter to current sentence
            word[0] = c;
            word[1] = '\0';
            Sentence *sent = &fc->sentences[current_sent];
            if (sent->word_count >= sent->capacity) {
                sent->capacity *= 2;
                sent->words = realloc(sent->words, sizeof(char*) * sent->capacity);
            }
            sent->words[sent->word_count++] = strdup(word);
            
            // Start new sentence
            current_sent++;
            fc->sentence_count = current_sent + 1;
            
            if (fc->sentence_count >= fc->capacity) {
                fc->capacity *= 2;
                fc->sentences = realloc(fc->sentences, sizeof(Sentence) * fc->capacity);
            }
            fc->sentences[current_sent].capacity = 10;
            fc->sentences[current_sent].word_count = 0;
            fc->sentences[current_sent].words = malloc(sizeof(char*) * fc->sentences[current_sent].capacity);
        } else {
            word[word_idx++] = c;
            if (word_idx >= MAX_WORD - 1) {
                word[word_idx] = '\0';
                Sentence *sent = &fc->sentences[current_sent];
                if (sent->word_count >= sent->capacity) {
                    sent->capacity *= 2;
                    sent->words = realloc(sent->words, sizeof(char*) * sent->capacity);
                }
                sent->words[sent->word_count++] = strdup(word);
                word_idx = 0;
            }
        }
    }
    
    // Handle remaining word
    if (word_idx > 0) {
        word[word_idx] = '\0';
        Sentence *sent = &fc->sentences[current_sent];
        if (sent->word_count >= sent->capacity) {
            sent->capacity *= 2;
            sent->words = realloc(sent->words, sizeof(char*) * sent->capacity);
        }
        sent->words[sent->word_count++] = strdup(word);
    }
    
    // Remove empty last sentence if exists
    if (fc->sentence_count > 0 && fc->sentences[fc->sentence_count - 1].word_count == 0) {
        free(fc->sentences[fc->sentence_count - 1].words);
        fc->sentence_count--;
    }

    if(fc->sentences[current_sent].word_count > 0) {
        fc->sentence_count = current_sent + 1;
    } //FOR WHEN FILE HAS NO DELIMITERS BUT WORDS, WE STILL COUNT IT AS ONE SENTENCE -S
    
    fclose(file);
    return 0;
}

int write_word_to_file(FILE *fp, const char *word) {
    const char *p = word;
    while (*p) {
        fputc(*p++, fp);
    }
    return 0;
}

int write_file_content(const char *filepath, FileContent *fc) {
    FILE *file = fopen(filepath, "w");
    if (!file) return -1;
    
    for (int i = 0; i < fc->sentence_count; i++) {
        for (int j = 0; j < fc->sentences[i].word_count; j++) {
            write_word_to_file(file, fc->sentences[i].words[j]);

            if (j < fc->sentences[i].word_count - 1) {
                const char* cur = fc->sentences[i].words[j];
                const char *next = fc->sentences[i].words[j + 1];

                int cur_is_delim = is_delimiter(cur[0]);
                int next_is_delim = is_delimiter(next[0]);
                int cur_is_newline = (cur[0] == '\n' && cur[1] == '\0');
                int next_is_newline = (next[0] == '\n' && next[1] == '\0');
                int cur_is_space = is_space_token(cur);
                int next_is_space = is_space_token(next);

                // NEVER add space if current or next is already a space/newline token
                if (cur_is_space || next_is_space || cur_is_newline || next_is_newline) {
                    continue;  // Space token handles its own spacing
                }
                
                // Don't add space between word and delimiter or delimiter and word
                if (cur_is_delim || next_is_delim) {
                    continue;
                }
                
                // Add space between two regular words
                fprintf(file, " ");
            }

            //printf("DEBUG write token[%d][%d] = '%s' (is_space=%d)\n", i, j, fc->sentences[i].words[j], is_space_token(fc->sentences[i].words[j]));
        }
        
        // Handle space between sentences - FIXED SECTION
        if (i < fc->sentence_count - 1) {
            if (fc->sentences[i].word_count > 0) {
                char *last = fc->sentences[i].words[fc->sentences[i].word_count - 1];
                
                // Don't add space if sentence ends with newline or space token
                if (!is_newline_token(last) && !is_space_token(last)) {
                    // Check if next sentence starts with space/newline token
                    if (fc->sentences[i + 1].word_count > 0) {
                        char *first_next = fc->sentences[i + 1].words[0];
                        // Only add space if next sentence doesn't start with space/newline
                        if (!is_space_token(first_next) && !is_newline_token(first_next)) {
                            fprintf(file, " ");
                        }
                    } else {
                        // Next sentence is empty, add space
                        fprintf(file, " ");
                    }
                }
            }
        }
    }
    
    fclose(file);
    return 0;
}

char* file_content_to_string(FileContent *fc) {
    char *result = malloc(MAX_BUFFER);
    result[0] = '\0';
    int pos = 0;
    
    for (int i = 0; i < fc->sentence_count; i++) {
        for (int j = 0; j < fc->sentences[i].word_count; j++) {
            int len = strlen(fc->sentences[i].words[j]);
            if (pos + len + 2 < MAX_BUFFER) {
                strcpy(result + pos, fc->sentences[i].words[j]);
                pos += len;

                if (j < fc->sentences[i].word_count - 1) {
                    const char *cur = fc->sentences[i].words[j];
                    const char *next = fc->sentences[i].words[j + 1];
                    int cur_is_delim = is_delimiter(cur[0]);
                    int next_is_delim = is_delimiter(next[0]);
                    int cur_is_newline = (cur[0] == '\n' && cur[1] == '\0');
                    int next_is_newline = (next[0] == '\n' && next[1] == '\0');
                    int cur_is_space = is_space_token(cur);
                    int next_is_space = is_space_token(next);

                    if (cur_is_space || next_is_space || cur_is_newline || next_is_newline) {
                        continue;
                    }
                    
                    if (cur_is_delim || next_is_delim) {
                        continue;
                    }
                    
                    result[pos++] = ' ';
                }
            }
        }
        
        if (i < fc->sentence_count - 1) {
            if (fc->sentences[i].word_count > 0) {
                char *last = fc->sentences[i].words[fc->sentences[i].word_count - 1];
                if (!is_newline_token(last) && !is_space_token(last)) {
                    // Check if next sentence starts with space/newline token
                    if (fc->sentences[i + 1].word_count > 0) {
                        char *first_next = fc->sentences[i + 1].words[0];
                        if (!is_space_token(first_next) && !is_newline_token(first_next)) {
                            result[pos++] = ' ';
                        }
                    } else {
                        result[pos++] = ' ';
                    }
                }
            }
        }
    }
    
    result[pos] = '\0';
    return result;
}

// Heavily edited - N
int insert_word_in_sentence(FileContent *fc, int sent_idx, int word_idx, const char *word) {
    // Checking validity, might immediately return - N
    if (sent_idx < 0 || sent_idx > fc->sentence_count) { // Changed >= to > - S
        log_formatted(LOG_ERROR, "Invalid sentence index: %d (file has %d sentences)", 
                     sent_idx, fc->sentence_count);
        return -1;
    }
    
    /* If caller wants to insert at the end (append a new sentence), make sure
       the target sentence slot is allocated and counted. This prevents accessing
       uninitialized memory when sent_idx == fc->sentence_count.  - S */

        if (sent_idx == fc->sentence_count) {
        /* grow sentences array if needed */
        if (fc->sentence_count >= fc->capacity) {
            int newcap = fc->capacity ? fc->capacity * 2 : 10;
            while (newcap <= fc->sentence_count) newcap *= 2;
            Sentence *tmp = realloc(fc->sentences, sizeof(Sentence) * newcap);
            if (!tmp) {
                log_formatted(LOG_ERROR, "Out of memory expanding sentences");
                return -1;
            }
            fc->sentences = tmp;
            fc->capacity = newcap;
        }
        /* initialize the new (empty) sentence slot */
        fc->sentences[fc->sentence_count].capacity = 10;
        fc->sentences[fc->sentence_count].word_count = 0;
        fc->sentences[fc->sentence_count].words = malloc(sizeof(char*) * fc->sentences[fc->sentence_count].capacity);
        if (!fc->sentences[fc->sentence_count].words) {
            log_formatted(LOG_ERROR, "Out of memory allocating words for new sentence");
            return -1;
        }
        /* actually add the sentence to the count so subsequent code can use it */
        if(!(fc->sentence_count==1 && fc->sentences[0].word_count==0)) // Special case: if file was empty with one empty sentence, don't count it - S
        fc->sentence_count++;
    }
    

    
    Sentence *sent = &fc->sentences[sent_idx];

    int real_word_count = 0;
    for (int i = 0; i < sent->word_count; i++) {
        if (!should_skip_for_indexing(sent->words[i])) {
            real_word_count++;
        }
    }
    
    // word_idx is 1-based, validate directly - N
    if (word_idx < 1 || word_idx > real_word_count + 1) {
        log_formatted(LOG_ERROR, "Invalid word index: %d (sentence has %d words, valid range: 1-%d)", 
                     word_idx, real_word_count, real_word_count + 1);
        return -1;
    }
    
    int actual_idx = 0;
    int real_words_seen = 0;
    
    if (word_idx == 1) {
        // Insert at beginning
        actual_idx = 0;
    } else {
        // Find position after (word_idx - 1) real words
        for (int i = 0; i < sent->word_count; i++) {
            if (!should_skip_for_indexing(sent->words[i])) {
                real_words_seen++;
                if (real_words_seen == word_idx - 1) {
                    actual_idx = i + 1;
                    break;
                }
            }
        }
        // If we want to append (word_idx > real_word_count)
        if (real_words_seen == real_word_count && word_idx == real_word_count + 1) {
            actual_idx = sent->word_count;
        }
    }
    
    // Split word by delimiters - N
    int part_count;
    char **parts = split_by_delimiters(word, &part_count);
    
    if (part_count == 0) {
        free(parts);
        log_formatted(LOG_WARNING, "Empty word, skipping insertion");
        return 0;
    }
    
    // Count how many delimiters (new sentences) we're creating - N
    int new_sentences = 0;
    
    int last_delimiter_idx = -1;
    for (int i = 0; i < part_count; i++) {
        if (is_delimiter(parts[i][0])) {
            last_delimiter_idx = i;
        }
    }

    if (last_delimiter_idx >= 0 && last_delimiter_idx < part_count - 1) {
        new_sentences = 1;
    }

    for (int i = 0; i < part_count; i++) {
        if (is_delimiter(parts[i][0])) {
            new_sentences++;
        }
    }
    
    log_formatted(LOG_DEBUG, "Inserting %d parts, %d will create new sentences", 
                 part_count, new_sentences);
    
    // Make room for new sentences if needed - N
    if (new_sentences > 0) {
        int new_total = fc->sentence_count + new_sentences;
        if (new_total > fc->capacity) {
            while (fc->capacity < new_total) fc->capacity *= 2;
            fc->sentences = realloc(fc->sentences, sizeof(Sentence) * fc->capacity);
        }
        
        // Shift existing sentences down to make room
        for (int i = fc->sentence_count - 1; i > sent_idx; i--) {
            fc->sentences[i + new_sentences] = fc->sentences[i];
        }
        
        // Initialize new sentence slots - N
        for (int i = 1; i <= new_sentences; i++) {
            fc->sentences[sent_idx + i].capacity = 10;
            fc->sentences[sent_idx + i].word_count = 0;
            fc->sentences[sent_idx + i].words = malloc(sizeof(char*) * 10);
        }
    }
    
    // Insert parts
    Sentence *cur_sent = &fc->sentences[sent_idx];
    int cur_sent_idx = sent_idx;
    int in_new_sentence = 0;
    
    for (int i = 0; i < part_count; i++) {
        // Check if this is the last delimiter that should split sentences
        int should_split = (i == last_delimiter_idx && last_delimiter_idx < part_count - 1);
        
        if (!in_new_sentence) {
            // Still in original sentence - expand if needed
            if (cur_sent->word_count >= cur_sent->capacity) {
                cur_sent->capacity *= 2;
                cur_sent->words = realloc(cur_sent->words, sizeof(char*) * cur_sent->capacity);
            }
            
            // Shift words to make room
            for (int j = cur_sent->word_count; j > actual_idx; j--) {
                cur_sent->words[j] = cur_sent->words[j - 1];
            }
            
            // Insert word/delimiter
            cur_sent->words[actual_idx] = strdup(parts[i]);
            cur_sent->word_count++;
            actual_idx++;
            
            if (should_split) {
                // Move remaining words to new sentence
                cur_sent_idx++;
                Sentence *next_sent = &fc->sentences[cur_sent_idx];
                int words_to_move = cur_sent->word_count - actual_idx;
                
                // Ensure capacity in next sentence
                while (next_sent->capacity < words_to_move + (part_count - i - 1)) {
                    next_sent->capacity *= 2;
                    next_sent->words = realloc(next_sent->words, sizeof(char*) * next_sent->capacity);
                }
                
                for (int j = 0; j < words_to_move; j++) {
                    next_sent->words[next_sent->word_count++] = cur_sent->words[actual_idx + j];
                }
                cur_sent->word_count = actual_idx;
                
                // Switch to new sentence
                cur_sent = next_sent;
                actual_idx = 0;  // FIX: Start at beginning of new sentence content
                in_new_sentence = 1;
            }
        } else {
            // In new sentence - expand if needed before inserting
            if (cur_sent->word_count >= cur_sent->capacity) {
                cur_sent->capacity *= 2;
                cur_sent->words = realloc(cur_sent->words, sizeof(char*) * cur_sent->capacity);
            }
            
            // FIX: Insert at actual_idx, not append
            // Shift words after actual_idx to make room
            for (int j = cur_sent->word_count; j > actual_idx; j--) {
                cur_sent->words[j] = cur_sent->words[j - 1];
            }
            
            cur_sent->words[actual_idx] = strdup(parts[i]);
            cur_sent->word_count++;
            actual_idx++;  // Move forward for next insertion
        }
        
        free(parts[i]);
    }
    free(parts);
    
    fc->sentence_count += new_sentences;
    log_formatted(LOG_DEBUG, "After insertion, file has %d sentences", fc->sentence_count);
    
    return new_sentences;
}

void get_file_stats(const char *filepath, int *word_count, int *char_count) {
    *word_count = 0;
    *char_count = 0;
    
    FILE *file = fopen(filepath, "r");
    if (!file) {
        log_formatted(LOG_ERROR, "Cannot open file for stats: %s", filepath);
        return;
    }
    
    int in_word = 0;
    int c;
    
    while ((c = fgetc(file)) != EOF) {
        // Count non-whitespace, non-delimiter characters for char_count
        // if (c != ' ' && c != '\t' && c != '\n' && c != '\r' && !is_delimiter(c)) { - N, count everything as a character
            (*char_count)++;
        // }
        
        // Count words (sequences of non-whitespace characters)
        if (c == ' ' || c == '\t' || c == '\n' || c == '\r') {
            if (in_word) {
                (*word_count)++;
                in_word = 0;
            }
        } else if (is_delimiter(c)) {
            // Delimiters count as separate words
            if (in_word) {
                (*word_count)++;
                in_word = 0;
            }
            // (*word_count)++;  // The delimiter itself is a word - this is awkward, so not counting it here - N
        } else {
            in_word = 1;
        }
    }
    
    // Handle last word if file doesn't end with whitespace
    if (in_word) {
        (*word_count)++;
    }
    
    fclose(file);
    
    log_formatted(LOG_DEBUG, "File stats for %s: %d words, %d chars", 
                 filepath, *word_count, *char_count);
}

int create_undo_backup(const char *filepath) {
    char undo_path[MAX_PATH];
    snprintf(undo_path, sizeof(undo_path), "%s.undo", filepath);
    
    // Copy original to undo
    FILE *src = fopen(filepath, "r");
    if (!src) return -1;
    
    FILE *dst = fopen(undo_path, "w");
    if (!dst) {
        fclose(src);
        return -1;
    }
    
    char buffer[MAX_BUFFER];
    size_t bytes;
    
    while ((bytes = fread(buffer, 1, sizeof(buffer), src)) > 0) {
        fwrite(buffer, 1, bytes, dst);
    }
    
    fclose(src);
    fclose(dst);
    return 0;
}

int restore_from_undo(const char *filepath) {
    //printf("[SS DEBUG] Restoring from undo: %s\n", filepath); // Debug line - N
    char undo_path[MAX_PATH];
    snprintf(undo_path, sizeof(undo_path), "%s.undo", filepath);
    
    // Check if undo file exists
    if (access(undo_path, F_OK) != 0) {
        return -1;
    }

    //printf("[SS DEBUG] Undo file exists: %s\n", undo_path); // Debug line - N
    
    // Copy undo to original
    FILE *src = fopen(undo_path, "r");
    if (!src) return -1;
    
    FILE *dst = fopen(filepath, "w");
    if (!dst) {
        fclose(src);
        return -1;
    }
    
    char buffer[MAX_BUFFER];
    size_t bytes;

    //printf("[SS DEBUG] Starting file restoration...\n"); // Debug line - N
    
    while ((bytes = fread(buffer, 1, sizeof(buffer), src)) > 0) {
        fwrite(buffer, 1, bytes, dst);
    }

    //printf("[SS DEBUG] File restoration completed.\n"); // Debug line - N
     
    fclose(src);
    fclose(dst);
    
    // Remove undo file after restoration
    unlink(undo_path);

    //printf("[SS DEBUG] Undo file removed: %s\n", undo_path); // Debug line - N
    return 200; // Return 200 to indicate success as per previous convention - N
}

int undo_backup_exists(const char *filepath) {
    char undo_path[MAX_PATH];
    snprintf(undo_path, sizeof(undo_path), "%s.undo", filepath);
    return (access(undo_path, F_OK) == 0);
}

int create_checkpoint(const char *filepath, const char *tag) {
    char checkpoint_path[MAX_PATH];
    snprintf(checkpoint_path, sizeof(checkpoint_path), "%s.checkpoint_%s", filepath, tag);
    
    // Check if checkpoint already exists
    if (access(checkpoint_path, F_OK) == 0) {
        return ERR_FILE_EXISTS;
    }
    
    // Copy file to checkpoint
    FILE *src = fopen(filepath, "r");
    if (!src) return ERR_FILE_NOT_FOUND;
    
    FILE *dst = fopen(checkpoint_path, "w");
    if (!dst) {
        fclose(src);
        return ERR_SERVER_ERROR;
    }
    
    char buffer[MAX_BUFFER];
    size_t bytes;
    while ((bytes = fread(buffer, 1, sizeof(buffer), src)) > 0) {
        fwrite(buffer, 1, bytes, dst);
    }
    
    fclose(src);
    fclose(dst);
    
    log_formatted(LOG_INFO, "Created checkpoint '%s' for %s", tag, filepath);
    return SUCCESS;
}

int list_checkpoints(const char *filepath, char *buffer, int buffer_size) {
    char dir_path[MAX_PATH];
    char filename[MAX_FILENAME];
    
    // Extract directory and filename
    strncpy(dir_path, filepath, sizeof(dir_path));
    char *last_slash = strrchr(dir_path, '/');
    if (last_slash) {
        strcpy(filename, last_slash + 1);
        *last_slash = '\0';
    } else {
        strcpy(filename, filepath);
        strcpy(dir_path, ".");
    }
    
    DIR *dir = opendir(dir_path);
    if (!dir) return ERR_SERVER_ERROR;
    
    buffer[0] = '\0';
    int pos = 0;
    struct dirent *entry;
    char checkpoint_prefix[MAX_PATH];
    snprintf(checkpoint_prefix, sizeof(checkpoint_prefix), "%s.checkpoint_", filename);
    int prefix_len = strlen(checkpoint_prefix);
    
    while ((entry = readdir(dir)) != NULL) {
        if (strncmp(entry->d_name, checkpoint_prefix, prefix_len) == 0) {
            // Extract tag name
            const char *tag = entry->d_name + prefix_len;
            pos += snprintf(buffer + pos, buffer_size - pos, "%s\n", tag);
            if (pos >= buffer_size - 1) break;
        }
    }
    
    closedir(dir);
    
    if (pos == 0) {
        snprintf(buffer, buffer_size, "No checkpoints found.\n");
    }
    
    return SUCCESS;
}

int view_checkpoint(const char *filepath, const char *tag, char *buffer, int buffer_size) {
    char checkpoint_path[MAX_PATH];
    snprintf(checkpoint_path, sizeof(checkpoint_path), "%s.checkpoint_%s", filepath, tag);
    
    FILE *file = fopen(checkpoint_path, "r");
    if (!file) return ERR_FILE_NOT_FOUND;
    
    size_t bytes_read = fread(buffer, 1, buffer_size - 1, file);
    buffer[bytes_read] = '\0';
    
    fclose(file);
    return SUCCESS;
}

int revert_to_checkpoint(const char *filepath, const char *tag) {
    char checkpoint_path[MAX_PATH];
    snprintf(checkpoint_path, sizeof(checkpoint_path), "%s.checkpoint_%s", filepath, tag);
    
    // Check if checkpoint exists
    if (access(checkpoint_path, F_OK) != 0) {
        return ERR_FILE_NOT_FOUND;
    }
    
    // Create backup of current file before reverting
    if (create_undo_backup(filepath) != 0) {
        log_formatted(LOG_WARNING, "Could not create undo backup before checkpoint revert");
    }
    
    // Copy checkpoint to original file
    FILE *src = fopen(checkpoint_path, "r");
    if (!src) return ERR_FILE_NOT_FOUND;
    
    FILE *dst = fopen(filepath, "w");
    if (!dst) {
        fclose(src);
        return ERR_SERVER_ERROR;
    }
    
    char buffer[MAX_BUFFER];
    size_t bytes;
    while ((bytes = fread(buffer, 1, sizeof(buffer), src)) > 0) {
        fwrite(buffer, 1, bytes, dst);
    }
    
    fclose(src);
    fclose(dst);
    
    log_formatted(LOG_INFO, "Reverted %s to checkpoint '%s'", filepath, tag);
    return SUCCESS;
}