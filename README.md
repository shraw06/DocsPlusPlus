DIRECTORY STRUCTURE:

```c
course-project-shellshocked/
├── cache.c
├── cache.h
├── client.c
├── common.c
├── common.h
├── file_ops.c
├── file_ops.h
├── logger.c
├── logger.h
├── Makefile
├── nm.c
├── README.md
├── ss.c
├── trie.c
└── trie.h

1 directory, 15 files
```
```
┌─────────────────────────────────────────────────────────────────────┐
│                        NETWORK FILE SYSTEM                          │
└─────────────────────────────────────────────────────────────────────┘

              ┌──────────────────────────────────┐
              │       Name Server (NM)           │
              │  IP: Known publicly              │
              │  Ports: 8080 (SS), 8081 (Client) │
              │        8082 (Heartbeat)          │
              └──────────────────────────────────┘
                         ▲  ▲  ▲
                         │  │  │
         ┌───────────────┘  │  └────────────────┐
         │                  │                   │
         │ Registration     │ Heartbeat         │ Registration
         │ Commands         │ (Port 8082)       │ Requests
         │                  │                   │
         ▼                  ▼                   ▼
┌─────────────────┐  ┌─────────────────┐  ┌──────────────┐
│ Storage Server 1│  │ Storage Server 2│  │   Client 1   │
│ Ports:          │  │ Ports:          │  │  Username:   │
│  NM: 8080       │  │  NM: 8080       │  │  alice       │
│  Client: 9001   │  │  Client: 9002   │  └──────────────┘
│  HB: 8082       │  │  HB: 8082       │
└─────────────────┘  └─────────────────┘  ┌──────────────┐
         │                   │             │   Client 2   │
         │                   │             │  Username:   │
         │ Direct Client     │             │  bob         │
         │ Connection        │             └──────────────┘
         │ (Read/Write/      │
         │  Stream)          │
         └───────────────────┘
```
```
┌─────────────────────────────────────────┐
│           Message Structure             │
├─────────────────────────────────────────┤
│ type: MessageType                       │
│ status: int                             │
│ sender: char[MAX_USERNAME]              │
│ filename: char[MAX_FILENAME]            │
│ foldername: char[MAX_FILENAME]          │
│ checkpoint_tag: char[MAX_USERNAME]      │
│ target_path: char[MAX_PATH]             │
│ data: char[MAX_BUFFER]                  │
│ sentence_index: int                     │
│ word_index: int                         │
│ ss_id: int                              │
│ client_port: int                        │
│ nm_port: int                            │
│ access: AccessType                      │
│ target_user: char[MAX_USERNAME]         │
└─────────────────────────────────────────┘
```
```
Trie Structure:
┌─────────────────────────────────────────────┐
│              Trie Root Node                 │
└─────────────────────────────────────────────┘
              │
              ├─── 'f' ───┐
              │           │
              │           ├─── 'i' ───┐
              │           │           │
              │           │           ├─── 'l' ───┐
              │           │           │           │
              │           │           │           ├─── 'e' ───┐
              │           │           │           │           │
              │           │           │           │      [FileMetadata*]
              │           │           │           │      is_end_of_word=1
              │
              ├─── 't' ───┐
                          │
                          ├─── 'e' ───┐
                          │           │
                          │           ├─── 's' ───┐
                          │           │           │
                          │           │           ├─── 't' ───┐
                          │           │           │           │
                          │           │           │      [FileMetadata*]
                          │           │           │      is_end_of_word=1

Each node contains:
┌─────────────────────────────────┐
│       TrieNode Structure        │
├─────────────────────────────────┤
│ children[ALPHABET_SIZE=128]     │
│ is_end_of_word: int             │
│ file_meta: FileMetadata*        │
└─────────────────────────────────┘
```
```
LRU Cache Structure:

┌────────────────────────────────────────────────────────────┐
│                      LRUCache                              │
├────────────────────────────────────────────────────────────┤
│ head ──→ [Dummy] ←─→ [Node1] ←─→ [Node2] ←─→ [Dummy] ←── tail
│                         ↑                                  │
│                    Most Recently Used                      │
│                                                            │
│ hash_table[capacity]:                                      │
│   [0] → NULL                                               │
│   [1] → Node2                                              │
│   [2] → NULL                                               │
│   ...                                                      │
│   [hash(key)] → Node1                                      │
│                                                            │
│ capacity: int                                              │
│ size: int                                                  │
│ lock: pthread_mutex_t                                      │
└────────────────────────────────────────────────────────────┘

CacheNode Structure:
┌─────────────────────────────────┐
│      CacheNode                  │
├─────────────────────────────────┤
│ key: char[MAX_FILENAME]         │
│ value: FileMetadata*            │
│ prev: CacheNode*                │
│ next: CacheNode*                │
└─────────────────────────────────┘

Operations:
- cache_get(): O(1) - Moves accessed node to head
- cache_put(): O(1) - Adds/updates and moves to head
- Eviction: Removes tail node when capacity exceeded
```
```
┌─────────────────────────────────────────┐
│       FileMetadata Structure            │
├─────────────────────────────────────────┤
│ filename: char[MAX_FILENAME]            │
│ folder_path: char[MAX_PATH]             │
│ owner: char[MAX_USERNAME]               │
│ ss_id: int                              │
│ size: size_t                            │
│ word_count: int                         │
│ char_count: int                         │
│ created: time_t                         │
│ modified: time_t                        │
│ accessed: time_t                        │
│ last_accessed_by: char[MAX_USERNAME]    │
│ acl[MAX_ACL_ENTRIES]:                   │
│   ┌─────────────────────────────┐       │
│   │ username: char[MAX_USERNAME]│       │
│   │ access: AccessType          │       │
│   └─────────────────────────────┘       │
│ acl_count: int                          │
└─────────────────────────────────────────┘
```
```
┌────────────────────────────────────────┐
│       FileContent Structure             │
├─────────────────────────────────────────┤
│ sentences: Sentence*                    │
│ sentence_count: int                     │
│ capacity: int                           │
└─────────────────────────────────────────┘
            │
            ├─── Sentence[0]
            │    ┌──────────────────────────┐
            │    │ words: char**            │
            │    │ word_count: int          │
            │    │ capacity: int            │
            │    └──────────────────────────┘
            │         │
            │         ├─── words[0]: "Hello"
            │         ├─── words[1]: " "
            │         ├─── words[2]: "world"
            │         └─── words[3]: "."
            │
            ├─── Sentence[1]
            │    ┌──────────────────────────┐
            │    │ words: char**            │
            │    │ word_count: int          │
            │    │ capacity: int            │
            │    └──────────────────────────┘
            │         │
            │         ├─── words[0]: "How"
            │         ├─── words[1]: " "
            │         ├─── words[2]: "are"
            │         ├─── words[3]: " "
            │         ├─── words[4]: "you"
            │         └─── words[5]: "?"
            └─── ...

Note: Words include delimiters (., !, ?) and whitespace (" ", "\n", "\t") as separate tokens
```
```
Write Session Flow:

┌──────────────────────────────────────────────────────────┐
│                  Write Session                           │
├──────────────────────────────────────────────────────────┤
│ filename: char[MAX_FILENAME]                             │
│ username: char[MAX_USERNAME]                             │
│ sentence_idx: int                                        │
│ temp_filepath: char[MAX_PATH]                            │
│ active: int                                              │
│ original_sentence_count: int                             │
│ lock_time: time_t                                        │
└──────────────────────────────────────────────────────────┘
                       ↓
              Creates temp file
                       ↓
┌──────────────────────────────────────────────────────────┐
│            file.txt.temp_user_sentidx                    │
│  (Isolated workspace for user's modifications)           │
└──────────────────────────────────────────────────────────┘
                       ↓
              User makes writes
                       ↓
              UNLOCK/COMMIT
                       ↓
┌──────────────────────────────────────────────────────────┐
│              Commit Queue Entry                          │
├──────────────────────────────────────────────────────────┤
│ Queued based on lock_time (FIFO order)                   │
│ - filename                                               │
│ - username                                               │
│ - sentence_idx                                           │
│ - original_sentence_count                                │
│ - temp_filepath                                          │
│ - lock_time                                              │
└──────────────────────────────────────────────────────────┘
                       ↓
              Sequential Processing
                       ↓
┌──────────────────────────────────────────────────────────┐
│              Merge Algorithm                             │
├──────────────────────────────────────────────────────────┤
│ 1. Calculate sentence shift                              │
│ 2. Adjust target sentence index                          │
│ 3. Merge temp content into main file                     │
│ 4. Write back to disk                                    │
│ 5. Clean up temp file                                    │
└──────────────────────────────────────────────────────────┘
```
```
┌─────────────────────────────────────────┐
│       File Lock Structure               │
├─────────────────────────────────────────┤
│ filename: char[MAX_FILENAME]            │
│ locks[lock_count]:                      │
│   ┌─────────────────────────────┐       │
│   │    SentenceLock             │       │
│   ├─────────────────────────────┤       │
│   │ locked: int                 │       │
│   │ locked_by: char[MAX_USERNAME]│      │
│   │ lock_time: time_t           │       │
│   │ mutex: pthread_mutex_t      │       │
│   └─────────────────────────────┘       │
│ lock_count: int                         │
└─────────────────────────────────────────┘

Per-Sentence Locking:
Sentence 0: [UNLOCKED] ───────────────────
Sentence 1: [LOCKED by user_A] ───────────
Sentence 2: [UNLOCKED] ───────────────────
Sentence 3: [LOCKED by user_B] ───────────
```
```
┌────────┐                  ┌────────┐                  ┌────────┐
│ Client │                  │   NM   │                  │   SS   │
└───┬────┘                  └───┬────┘                  └───┬────┘
    │                           │                           │
    │ CREATE <filename>         │                           │
    ├──────────────────────────>│                           │
    │                           │                           │
    │                           │ Check if file exists      │
    │                           │ in Trie                   │
    │                           │                           │
    │                           │ Select SS (round-robin)   │
    │                           │                           │
    │                           │ CREATE <filename>         │
    │                           ├──────────────────────────>│
    │                           │                           │
    │                           │                           │ Create empty file
    │                           │                           │ on disk
    │                           │                           │
    │                           │        ACK (SUCCESS)      │
    │                           │<──────────────────────────┤
    │                           │                           │
    │                           │ Add to Trie:              │
    │                           │  - Insert FileMetadata    │
    │                           │  - Set owner to sender    │
    │                           │  - Set ss_id              │
    │                           │  - Init timestamps        │
    │                           │                           │
    │        ACK (SUCCESS)      │                           │
    │<──────────────────────────┤                           │
    │                           │                           │
    │ "File created!"           │                           │
    │                           │                           │
```
```
┌────────┐                  ┌────────┐                  ┌────────┐
│ Client │                  │   NM   │                  │   SS   │
└───┬────┘                  └───┬────┘                  └───┬────┘
    │                           │                           │
    │ READ <filename>           │                           │
    ├──────────────────────────>│                           │
    │                           │                           │
    │                           │ Check access (READ)       │
    │                           │  - Search Trie            │
    │                           │  - Verify ACL             │
    │                           │                           │
    │                           │ Find SS from Trie         │
    │                           │                           │
    │  SS Info: "IP:PORT"       │                           │
    │<──────────────────────────┤                           │
    │                           │                           │
    │                           │                           │
    │  READ <filename>          │                           │
    ├───────────────────────────────────────────────────────>│
    │                           │                           │
    │                           │                           │ Read file from disk
    │                           │                           │ Parse content
    │                           │                           │
    │        File Content       │                           │
    │<───────────────────────────────────────────────────────┤
    │                           │                           │
    │ Display content           │                           │
    │                           │                           │
```
```
┌────────┐                  ┌────────┐                  ┌────────┐
│ Client │                  │   NM   │                  │   SS   │
└───┬────┘                  └───┬────┘                  └───┬────┘
    │                           │                           │
    │ WRITE <file> <sent_idx>   │                           │
    ├──────────────────────────>│                           │
    │                           │                           │
    │                           │ Check access (WRITE)      │
    │                           │  - Search Trie            │
    │                           │  - Verify ACL             │
    │                           │                           │
    │  SS Info: "IP:PORT"       │                           │
    │<──────────────────────────┤                           │
    │                           │                           │
    │  LOCK_SENTENCE            │                           │
    │  <file> <sent_idx>        │                           │
    ├───────────────────────────────────────────────────────>│
    │                           │                           │
    │                           │                           │ Validate sent_idx
    │                           │                           │ Check if locked
    │                           │                           │
    │                           │                           │ Lock acquired:
    │                           │                           │  - Set lock flag
    │                           │                           │  - Record username
    │                           │                           │  - Store lock_time
    │                           │                           │
    │                           │                           │ Start write session:
    │                           │                           │  - Copy file to temp
    │                           │                           │  - Store original_count
    │                           │                           │
    │        ACK (SUCCESS)      │                           │
    │<───────────────────────────────────────────────────────┤
    │                           │                           │
    │ "Sentence locked"         │                           │
    │                           │                           │
    │ WRITE <word_idx> <content>│                           │
    ├───────────────────────────────────────────────────────>│
    │                           │                           │
    │                           │                           │ Parse temp file
    │                           │                           │ Insert word at position
    │                           │                           │ Handle delimiters
    │                           │                           │ Write back to temp
    │                           │                           │
    │        ACK (SUCCESS)      │                           │
    │<───────────────────────────────────────────────────────┤
    │                           │                           │
    │ (User can repeat writes)  │                           │
    │                           │                           │
    │ ETIRW (UNLOCK)            │                           │
    ├───────────────────────────────────────────────────────>│
    │                           │                           │
    │                           │                           │ Commit write session:
    │                           │                           │  - Add to commit queue
    │                           │                           │  - Process queue (FIFO)
    │                           │                           │  - Merge temp → main
    │                           │                           │  - Adjust indices
    │                           │                           │
    │                           │                           │ Unlock sentence
    │                           │                           │
    │        ACK (SUCCESS)      │                           │
    │<───────────────────────────────────────────────────────┤
    │                           │                           │
    │ "Write successful!"       │                           │
    │                           │                           │
```
```
┌────────┐                  ┌────────┐                  ┌────────┐
│ Client │                  │   NM   │                  │   SS   │
└───┬────┘                  └───┬────┘                  └───┬────┘
    │                           │                           │
    │ STREAM <filename>         │                           │
    ├──────────────────────────>│                           │
    │                           │                           │
    │                           │ Check access (READ)       │
    │                           │                           │
    │  SS Info: "IP:PORT"       │                           │
    │<──────────────────────────┤                           │
    │                           │                           │
    │  STREAM <filename>        │                           │
    ├───────────────────────────────────────────────────────>│
    │                           │                           │
    │                           │                           │ Parse file
    │                           │                           │
    │        ACK                │                           │
    │<───────────────────────────────────────────────────────┤
    │                           │                           │
    │                           │                           │ For each word:
    │        Word 1             │                           │
    │<───────────────────────────────────────────────────────┤
    │ Display + space           │                           │
    │                           │                           │ usleep(100000)
    │        Word 2             │                           │ [0.1 sec delay]
    │<───────────────────────────────────────────────────────┤
    │ Display + space           │                           │
    │                           │                           │
    │         ...               │                           │
    │                           │                           │
    │        Word N             │                           │
    │<───────────────────────────────────────────────────────┤
    │ Display                   │                           │
    │                           │                           │
    │        STOP               │                           │
    │<───────────────────────────────────────────────────────┤
    │                           │                           │
    │ "\n"                      │                           │
    │                           │                           │

Note: If SS crashes mid-stream, client detects connection loss and displays error
```
```
┌────────┐                  ┌────────┐                  ┌────────┐
│ Client │                  │   NM   │                  │   SS   │
└───┬────┘                  └───┬────┘                  └───┬────┘
    │                           │                           │
    │ DELETE <filename>         │                           │
    ├──────────────────────────>│                           │
    │                           │                           │
    │                           │ Check ownership           │
    │                           │  - Search Trie            │
    │                           │  - Verify sender is owner │
    │                           │                           │
    │                           │ Find SS                   │
    │                           │                           │
    │                           │ CHECK_LOCKS               │
    │                           ├──────────────────────────>│
    │                           │                           │
    │                           │                           │ Check all sentence locks
    │                           │                           │ for this file
    │                           │                           │
    │                           │  ERR_FILE_LOCKED or SUCCESS
    │                           │<──────────────────────────┤
    │                           │                           │
    │                           │ If locked:                │
    │  ERR_FILE_LOCKED          │  - Return error           │
    │<──────────────────────────┤                           │
    │                           │                           │
    │                           │ If not locked:            │
    │                           │ DELETE <filename>         │
    │                           ├──────────────────────────>│
    │                           │                           │
    │                           │                           │ Delete file from disk
    │                           │                           │ Delete .undo file
    │                           │                           │
    │                           │        ACK (SUCCESS)      │
    │                           │<──────────────────────────┤
    │                           │                           │
    │                           │ Remove from Trie          │
    │                           │ Remove from Cache         │
    │                           │                           │
    │        ACK (SUCCESS)      │                           │
    │<──────────────────────────┤                           │
    │                           │                           │
    │ "File deleted!"           │                           │
    │                           │                           │
```
```
┌────────┐                  ┌────────┐
│ Client │                  │   NM   │
└───┬────┘                  └───┬────┘
    │                           │
    │ ADDACCESS -W file user    │
    ├──────────────────────────>│
    │                           │
    │                           │ Verify ownership
    │                           │  - Search Trie
    │                           │  - Check sender == owner
    │                           │
    │                           │ Check if user exists
    │                           │  - Search registered_users[]
    │                           │
    │                           │ Update FileMetadata:
    │                           │  - Add to ACL array
    │                           │  - Set access level
    │                           │
    │                           │ Update Trie
    │                           │ Update Cache
    │                           │
    │        ACK (SUCCESS)      │
    │<──────────────────────────┤
    │                           │
    │ "Access granted!"         │
    │                           │
```
```
┌────────┐                  ┌────────┐
│ Client │                  │   NM   │
└───┬────┘                  └───┬────┘
    │                           │
    │ REMACCESS file user       │
    ├──────────────────────────>│
    │                           │
    │                           │ Verify ownership
    │                           │  - Search Trie
    │                           │  - Check sender == owner
    │                           │
    │                           │ Check if user exists
    │                           │  - Search registered_users[]
    │                           │
    │                           │ Update FileMetadata:
    │                           │  - Add to ACL array
    │                           │  - Set access level
    │                           │
    │                           │ Update Trie
    │                           │ Update Cache
    │                           │
    │        ACK (SUCCESS)      │
    │<──────────────────────────┤
    │                           │
    │ "Access removed!"         │
    │                           │
```
```
┌────────┐                  ┌────────┐
│ Client │                  │   NM   │
└───┬────┘                  └───┬────┘
    │                           │
    │ REQUESTACCESS -R file     │
    ├──────────────────────────>│
    │                           │
    │                           │ Check if file exists
    │                           │
    │                           │ Add to access_requests[]:
    │                           │  - username
    │                           │  - filename
    │                           │  - requested_access
    │                           │  - request_time
    │                           │
    │        ACK (SUCCESS)      │
    │<──────────────────────────┤
    │                           │
    │ "Request sent!"           │
    │                           │
```
```
┌────────┐                  ┌────────┐
│ Owner  │                  │   NM   │
└───┬────┘                  └───┬────┘
    │                           │
    │ VIEWREQUESTS              │
    ├──────────────────────────>│
    │                           │
    │                           │ Filter access_requests[]
    │                           │  - Find requests where
    │                           │    file.owner == sender
    │                           │
    │  List of requests         │
    │  [ID] user, file, access  │
    │<──────────────────────────┤
    │                           │
    │ APPROVEREQUEST <id>       │
    ├──────────────────────────>│
    │                           │
    │                           │ Verify ownership
    │                           │ Grant access (update ACL)
    │                           │ Remove from queue
    │                           │
    │        ACK (SUCCESS)      │
    │<──────────────────────────┤
    │                           │
```
```
┌────────┐                  ┌────────┐
│ Owner  │                  │   NM   │
└───┬────┘                  └───┬────┘
    │                           │
    │ VIEWREQUESTS              │
    ├──────────────────────────>│
    │                           │
    │                           │ Filter access_requests[]
    │                           │  - Find requests where
    │                           │    file.owner == sender
    │                           │
    │  List of requests         │
    │  [ID] user, file, access  │
    │<──────────────────────────┤
    │                           │
    │ DENYREQUEST <id>          │
    ├──────────────────────────>│
    │                           │
    │                           │ Verify ownership
    │                           │ Deny Access (update ACL)
    │                           │ Remove from queue
    │                           │
    │        ACK                │
    │<──────────────────────────┤
    │                           │
```
```
┌────────┐                  ┌────────┐                  ┌────────┐
│ Client │                  │   NM   │                  │   SS   │
└───┬────┘                  └───┬────┘                  └───┬────┘
    │                           │                           │
    │ INFO <filename>           │                           │
    ├──────────────────────────>│                           │
    │                           │                           │
    │                           │ Search Trie               │
    │                           │  - Get FileMetadata       │
    │                           │                           │
    │                           │ SS_INFO request           │
    │                           ├──────────────────────────>│
    │                           │                           │
    │                           │                           │ stat() file
    │                           │                           │ get_file_stats()
    │                           │                           │  - word_count
    │                           │                           │  - char_count
    │                           │                           │
    │                           │  Updated metadata         │
    │                           │<──────────────────────────┤
    │                           │                           │
    │                           │ Update Trie               │
    │                           │ Update Cache              │
    │                           │                           │
    │                           │ Format response:          │
    │                           │  - Filename               │
    │                           │  - Owner                  │
    │                           │  - Created timestamp      │
    │                           │  - Modified timestamp     │
    │                           │  - Accessed timestamp     │
    │                           │  - Last accessed by       │
    │                           │  - Size (bytes)           │
    │                           │  - Word count             │
    │                           │  - Char count             │
    │                           │  - Storage Server ID      │
    │                           │  - ACL entries            │
    │                           │                           │
    │  Formatted info           │                           │
    │<──────────────────────────┤                           │
    │                           │                           │
    │ Display info              │                           │
    │                           │                           │

```
```
┌────────┐                  ┌────────┐                  ┌────────┐
│ Client │                  │   NM   │                  │   SS   │
└───┬────┘                  └───┬────┘                  └───┬────┘
    │                           │                           │
    │ VIEW [-a] [-l]            │                           │
    ├──────────────────────────>│                           │
    │                           │                           │
    │                           │ Parse flags:              │
    │                           │  -a: show all files       │
    │                           │  -l: show details         │
    │                           │                           │
    │                           │ Get all files from Trie   │
    │                           │  trie_get_all_files()     │
    │                           │                           │
    │                           │ If -l flag:               │
    │                           │   For each file:          │
    │                           │     SS_INFO <file>        │
    │                           │   ├─────────────────────>│
    │                           │   │                      │
    │                           │   │  Updated stats       │
    │                           │   │<─────────────────────┤
    │                           │   │                      │
    │                           │   Update Trie & Cache    │
    │                           │                           │
    │                           │ Filter files:             │
    │                           │  If -a: show all          │
    │                           │  Else: check access       │
    │                           │                           │
    │                           │ Format output:            │
    │                           │  Simple: filenames        │
    │                           │  Detailed: table format   │
    │                           │                           │
    │  File list                │                           │
    │<──────────────────────────┤                           │
    │                           │                           │
    │ Display list              │                           │
    │                           │                           │
```
```
┌────────┐                  ┌────────┐                  ┌────────┐
│ Client │                  │   NM   │                  │   SS   │
└───┬────┘                  └───┬────┘                  └───┬────┘
    │                           │                           │
    │ UNDO <filename>           │                           │
    ├──────────────────────────>│                           │
    │                           │                           │
    │                           │ Check access (WRITE)      │
    │                           │                           │
    │  SS Info: "IP:PORT"       │                           │
    │<──────────────────────────┤                           │
    │                           │                           │
    │  UNDO <filename>          │                           │
    ├───────────────────────────────────────────────────────>│
    │                           │                           │
    │                           │                           │ Check if .undo exists
    │                           │                           │  filepath + ".undo"
    │                           │                           │
    │                           │                           │ If exists:
    │                           │                           │  - Copy .undo → main
    │                           │                           │  - Delete .undo file
    │                           │                           │
    │                           │                           │ If not exists:
    │                           │                           │  - Return error
    │                           │                           │
    │        ACK (SUCCESS/ERR)  │                           │
    │<───────────────────────────────────────────────────────┤
    │                           │                           │
    │ Display result            │                           │
    │                           │                           │

Note: .undo file is created before any write operation
      Only one level of undo is supported
```
```
Client                          NM                          
  │                              │                          
  │────── MSG_LIST ──────────────>│                          
  │      (sender: username)       │                          
  │                              │                          
  │                              │ [Lock client_mutex]
  │                              │ Collect connected users
  │                              │ [Unlock client_mutex]
  │                              │
  │                              │ [Lock registered_users_mutex]
  │                              │ Collect all registered users
  │                              │ [Unlock registered_users_mutex]
  │                              │
  │                              │ Build response:
  │                              │ "=== Connected Users ==="
  │                              │ + online users list
  │                              │ "=== All Registered Users ==="
  │                              │ + all users list
  │                              │
  │<──── MSG_DATA (status=200) ──│
  │      (data: formatted list)  │
  │                              │
  │ Display user list            │
  │                              │

Response Format:
┌──────────────────────────────────┐
│ === Connected Users ===         │
│ alice (online)                   │
│ bob (online)                     │
│                                  │
│ === All Registered Users ===    │
│ alice                            │
│ bob                              │
│ charlie                          │
│ dave                             │
└──────────────────────────────────┘
```
```
Client                    NM                      SS
  │                        │                       │
  │─── MSG_EXEC ───────────>│                       │
  │   (filename, sender)    │                       │
  │                        │                       │
  │                        │ Check read access     │
  │                        │ for user             │
  │                        │                       │
  │                        │ Find SS for file      │
  │                        │ (via cache/trie)     │
  │                        │                       │
  │                        │─── MSG_SS_INFO ──────>│
  │                        │   (filename,          │
  │                        │    data="READ_CONTENT")│
  │                        │                       │
  │                        │                       │ Parse file
  │                        │                       │ Return content
  │                        │                       │
  │                        │<──── MSG_ACK ─────────│
  │                        │     (status=200,      │
  │                        │      data=file_content)│
  │                        │                       │
  │                        │ Execute commands:     │
  │                        │ FILE *fp = popen(     │
  │                        │    file_content, "r") │
  │                        │                       │
  │                        │ Capture output        │
  │                        │                       │
  │<──── MSG_DATA ─────────│                       │
  │     (status=200,       │                       │
  │      data=exec_output) │                       │
  │                        │                       │
  │ Display output         │                       │
  │                        │                       │

Error Cases:
┌──────────────────────────────────────────────────┐
│ ERR_ACCESS_DENIED (403) - No read access        │
│ ERR_FILE_NOT_FOUND (404) - File doesn't exist   │
│ ERR_SS_UNAVAILABLE (503) - SS not available     │
│ ERR_SERVER_ERROR (500) - Execution failed       │
└──────────────────────────────────────────────────┘
```
```
Client                    NM                      SS
  │                        │                       │
  │─ MSG_CREATEFOLDER ─────>│                       │
  │  (foldername, sender,   │                       │
  │   target_path)          │                       │
  │                        │                       │
  │                        │ Build full path:      │
  │                        │ if target_path empty: │
  │                        │   full_path = /foldername│
  │                        │ else:                 │
  │                        │   full_path = target_path│
  │                        │            /foldername│
  │                        │                       │
  │                        │ Search folder_trie    │
  │                        │ (ERR_FILE_EXISTS)    │
  │                        │                       │
  │                        │ Get SS (round-robin)  │
  │                        │                       │
  │                        │── MSG_CREATEFOLDER ───>│
  │                        │  (foldername,         │
  │                        │   target_path)        │
  │                        │                       │
  │                        │                       │ Build full path:
  │                        │                       │ ss_storage/target_path
  │                        │                       │
  │                        │                       │ Check if exists
  │                        │                       │
  │                        │                       │ Create recursively:
  │                        │                       │ mkdir for each segment
  │                        │                       │
  │                        │<──── MSG_ACK ─────────│
  │                        │     (status=200)      │
  │                        │                       │
  │                        │ Insert into folder_trie:│
  │                        │ FolderMetadata {      │
  │                        │   foldername,         │
  │                        │   parent_path,        │
  │                        │   owner, created,     │
  │                        │   ss_id, acl[]        │
  │                        │ }                     │
  │                        │                       │
  │<──── MSG_ACK ──────────│                       │
  │     (status=200)       │                       │
  │                        │                       │
  │ Display: "Folder       │                       │
  │  created successfully!"│                       │
  │                        │                       │

Folder Structure Example:
┌────────────────────────────────────────┐
│ Root (/)                               │
│ ├── project/                           │
│ │   ├── src/                           │
│ │   │   └── file.txt                   │
│ │   └── docs/                          │
│ └── backup/                            │
│                                        │
│ NM folder_trie stores:                 │
│ - /project                             │
│ - /project/src                         │
│ - /project/docs                        │
│ - /backup                              │
└────────────────────────────────────────┘
```
```

Client                    NM                      SS
  │                        │                       │
  │──── MSG_MOVE ──────────>│                       │
  │    (filename, sender,   │                       │
  │     target_path)        │                       │
  │                        │                       │
  │                        │ Search file in trie   │
  │                        │ (ERR_FILE_NOT_FOUND) │
  │                        │                       │
  │                        │ Check permissions:    │
  │                        │ - Owner OR            │
  │                        │ - Has WRITE access   │
  │                        │                       │
  │                        │ Verify target folder  │
  │                        │ exists (if not root)  │
  │                        │                       │
  │                        │ Get SS for file       │
  │                        │                       │
  │                        │──── MSG_MOVE ─────────>│
  │                        │    (filename,         │
  │                        │     target_path,      │
  │                        │     data=old_path)    │
  │                        │                       │
  │                        │                       │ Build old/new paths:
  │                        │                       │ old_full = storage/
  │                        │                       │   old_path/filename
  │                        │                       │ new_full = storage/
  │                        │                       │   new_path/filename
  │                        │                       │
  │                        │                       │ rename(old_full, new_full)
  │                        │                       │
  │                        │                       │ Move .undo file too
  │                        │                       │ (if exists)
  │                        │                       │
  │                        │<──── MSG_ACK ─────────│
  │                        │     (status=200)      │
  │                        │                       │
  │                        │ Update file_meta:     │
  │                        │ strcpy(file_meta->    │
  │                        │   folder_path,        │
  │                        │   target_path)        │
  │                        │                       │
  │                        │ Update trie & cache   │
  │                        │                       │
  │<──── MSG_ACK ──────────│                       │
  │     (status=200)       │                       │
  │                        │                       │
  │ Display: "File moved   │                       │
  │  successfully!"        │                       │
  │                        │                       │

Move Example:
┌────────────────────────────────────────────────┐
│ Before MOVE file.txt to /project/src:         │
│   ss_storage_1/                               │
│   ├── file.txt                                │
│   └── project/src/                            │
│                                               │
│ After MOVE:                                   │
│   ss_storage_1/                               │
│   └── project/src/                            │
│       └── file.txt                            │
│                                               │
│ FileMetadata updated:                         │
│   folder_path: "" → "/project/src"           │
└────────────────────────────────────────────────┘
```
```
Client                          NM
  │                              │
  │──── MSG_VIEWFOLDER ──────────>│
  │    (sender, target_path)      │
  │                              │
  │                              │ Check if viewing root:
  │                              │ viewing_root = (path empty
  │                              │                 or path == "/")
  │                              │
  │                              │ If not root:
  │                              │   Search folder_trie
  │                              │   (ERR_FILE_NOT_FOUND)
  │                              │
  │                              │ Get all files from file_trie
  │                              │ trie_get_all_files()
  │                              │
  │                              │ Filter files:
  │                              │ FOR each file:
  │                              │   IF viewing_root:
  │                              │     matches = (folder_path
  │                              │                is empty)
  │                              │   ELSE:
  │                              │     matches = (folder_path
  │                              │                == target_path)
  │                              │
  │                              │   IF matches AND has_access:
  │                              │     Add filename to buffer
  │                              │
  │                              │ Build response:
  │                              │ "file1.txt\nfile2.txt\n..."
  │                              │ OR "(empty folder)"
  │                              │
  │<──── MSG_DATA (status=200) ──│
  │     (data: file list)        │
  │                              │
  │ Display folder contents      │
  │                              │

Folder View Example:
┌──────────────────────────────────┐
│ VIEWFOLDER /project/src          │
│                                  │
│ Output:                          │
│ main.c                           │
│ utils.c                          │
│ header.h                         │
│                                  │
│ VIEWFOLDER /empty                │
│                                  │
│ Output:                          │
│ (empty folder)                   │
└──────────────────────────────────┘
```

```
Client                    NM                      SS
  │                        │                       │
  │─ MSG_VIEWCHECKPOINT ───>│                       │
  │  (filename, sender,     │                       │
  │   checkpoint_tag)       │                       │
  │                        │                       │
  │                        │ Check access          │
  │                        │                       │
  │                        │ Find SS for file      │
  │                        │                       │
  │                        │── MSG_VIEWCHECKPOINT ─>│
  │                        │  (filename,           │
  │                        │   checkpoint_tag)     │
  │                        │                       │
  │                        │                       │ Build checkpoint path:
  │                        │                       │ checkpoint_path = 
  │                        │                       │   filepath.checkpoint_tag
  │                        │                       │
  │                        │                       │ fopen(checkpoint_path, "r")
  │                        │                       │
  │                        │                       │ Read entire content:
  │                        │                       │ fread(buffer, 1, 
  │                        │                       │       MAX_BUFFER-1, file)
  │                        │                       │
  │                        │<──── MSG_ACK ─────────│
  │                        │     (status=200,      │
  │                        │      data=checkpoint_ │
  │                        │           content)    │
  │                        │                       │
  │<──── MSG_ACK ──────────│                       │
  │     (status=200,       │                       │
  │      data=content)     │                       │
  │                        │                       │
  │ Display checkpoint     │                       │
  │ content to user        │                       │
  │                        │                       │

Error Cases:
┌──────────────────────────────────────────────────┐
│ ERR_FILE_NOT_FOUND (404) - Checkpoint not found  │
│ ERR_ACCESS_DENIED (403) - No access              │
└──────────────────────────────────────────────────┘
```
```
Client                    NM                      SS
  │                        │                       │
  │─ MSG_CHECKPOINT ───────>│                       │
  │  (filename, sender,     │                       │
  │   checkpoint_tag)       │                       │
  │                        │                       │
  │                        │ Check write access    │
  │                        │ for user             │
  │                        │                       │
  │                        │ Find SS for file      │
  │                        │                       │
  │                        │─── MSG_CHECKPOINT ────>│
  │                        │   (filename,          │
  │                        │    checkpoint_tag)    │
  │                        │                       │
  │                        │                       │ Build checkpoint path:
  │                        │                       │ filepath.checkpoint_tag
  │                        │                       │
  │                        │                       │ Check if exists
  │                        │                       │ (ERR_FILE_EXISTS if yes)
  │                        │                       │
  │                        │                       │ Copy file to checkpoint:
  │                        │                       │ src = fopen(filepath, "r")
  │                        │                       │ dst = fopen(checkpoint, "w")
  │                        │                       │ Copy bytes
  │                        │                       │
  │                        │<──── MSG_ACK ─────────│
  │                        │     (status=200)      │
  │                        │                       │
  │<──── MSG_ACK ──────────│                       │
  │     (status=200)       │                       │
  │                        │                       │
  │ Display: "Checkpoint   │                       │
  │  'tag' created!"       │                       │
  │                        │                       │

Checkpoint File Structure:
┌──────────────────────────────────────────────┐
│ Storage Server Directory:                    │
│   ss_storage_1/                              │
│   ├── file.txt                               │
│   ├── file.txt.checkpoint_v1                 │
│   ├── file.txt.checkpoint_v2                 │
│   └── file.txt.checkpoint_stable             │
└──────────────────────────────────────────────┘
```

```
Client                    NM                      SS
  │                        │                       │
  │─ MSG_LISTCHECKPOINTS ──>│                       │
  │  (filename, sender)     │                       │
  │                        │                       │
  │                        │ Check access          │
  │                        │                       │
  │                        │ Find SS for file      │
  │                        │                       │
  │                        │── MSG_LISTCHECKPOINTS >│
  │                        │  (filename)           │
  │                        │                       │
  │                        │                       │ Extract dir & filename
  │                        │                       │ from filepath
  │                        │                       │
  │                        │                       │ opendir(dir_path)
  │                        │                       │
  │                        │                       │ Search for files matching:
  │                        │                       │ "filename.checkpoint_*"
  │                        │                       │
  │                        │                       │ Extract tag names:
  │                        │                       │ tag = entry->d_name + 
  │                        │                       │       prefix_len
  │                        │                       │
  │                        │                       │ Build list of tags
  │                        │                       │
  │                        │<──── MSG_ACK ─────────│
  │                        │     (status=200,      │
  │                        │      data="v1\nv2\n   │
  │                        │            stable\n") │
  │                        │                       │
  │<──── MSG_ACK ──────────│                       │
  │     (status=200,       │                       │
  │      data=checkpoint_list)│                    │
  │                        │                       │
  │ Display:               │                       │
  │ "Checkpoints for file.txt:"│                   │
  │ "v1"                   │                       │
  │ "v2"                   │                       │
  │ "stable"               │                       │
  │                        │                       │

Empty Case:
┌──────────────────────────────────┐
│ Display: "No checkpoints found." │
└──────────────────────────────────┘
```
```
Client                    NM                      SS
  │                        │                       │
  │─── MSG_REVERT ─────────>│                       │
  │   (filename, sender,    │                       │
  │    checkpoint_tag)      │                       │
  │                        │                       │
  │                        │ Check write access    │
  │                        │                       │
  │                        │ Find SS for file      │
  │                        │                       │
  │                        │──── MSG_REVERT ───────>│
  │                        │    (filename,         │
  │                        │     checkpoint_tag)   │
  │                        │                       │
  │                        │                       │ Build checkpoint path
  │                        │                       │
  │                        │                       │ Check checkpoint exists
  │                        │                       │ (ERR_FILE_NOT_FOUND)
  │                        │                       │
  │                        │                       │ Create undo backup:
  │                        │                       │ create_undo_backup(
  │                        │                       │   filepath)
  │                        │                       │
  │                        │                       │ Copy checkpoint to file:
  │                        │                       │ src = fopen(checkpoint, "r")
  │                        │                       │ dst = fopen(filepath, "w")
  │                        │                       │ Copy all bytes
  │                        │                       │
  │                        │<──── MSG_ACK ─────────│
  │                        │     (status=200)      │
  │                        │                       │
  │<──── MSG_ACK ──────────│                       │
  │     (status=200)       │                       │
  │                        │                       │
  │ Display: "File reverted│                       │
  │  to checkpoint 'tag'!" │                       │
  │                        │                       │

Revert Process:
┌────────────────────────────────────────────────────┐
│ Before Revert:                                     │
│   file.txt (current: "Hello World Modified")      │
│   file.txt.checkpoint_v1 ("Hello World")          │
│                                                    │
│ After REVERT v1:                                   │
│   file.txt ("Hello World")                        │
│   file.txt.undo ("Hello World Modified")          │
│   file.txt.checkpoint_v1 ("Hello World")          │
└────────────────────────────────────────────────────┘
```

This project was developed in collaboration with [@NehaP1706](https://github.com/NehaP1706).
