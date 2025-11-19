# Project Docs++: Limitations, Assumptions, and Declarations

This document outlines the key design choices, inherent limitations, and operational assumptions of the Docs++ Distributed File System implementation.

## 1. System Limitations

- **Single Point of Failure (SPOF):** The Name Server (NM) is a SPOF. As per the project specification, if the NM fails, the entire system becomes inoperable and requires a manual restart.
- **Undo Functionality:** The system supports only a single level of undo per file. There is no history of changes; only the most recent write operation can be reverted.
- **Write Conflict Resolution:** Concurrent writes to the *same sentence* are prevented by a sentence-level lock. However, concurrent writes to *different sentences* are queued and processed sequentially (FIFO based on lock acquisition time). This can lead to a backlog and potential delays under high contention. The user who finishes their write first enters the commit queue first.
- **Data Replication:** The current implementation does not support fault tolerance through data replication. If a Storage Server (SS) fails, any files stored exclusively on that server become inaccessible until the server is manually recovered. The bonus fault-tolerance features (replication, failure detection) are not implemented.
- **Resource Limits:** The system operates under predefined static limits (e.g., `MAX_FILES`, `MAX_CLIENTS`, `MAX_SS`, `MAX_BUFFER`). It cannot dynamically scale beyond these compiled-in constants.

## 2. Caveats and User Experience Notes

- **Single Session Per User:** The system enforces a strict one-session-per-username policy. A user cannot have multiple concurrent client sessions with the same username.
- **Word Definition and Counting:** The system's tokenizer treats sentence delimiters (`.`, `!`, `?`) as separate "words" inside a one WRITE command. This affects word counts in file information and the indexing for write operations, which may be unintuitive.
The fact that there should be no space between the last word of a sentence and its adjacent right delimiter unless explicitly states by the user, is almost taken care of.
- **Write Operation Indexing:** When writing to a file, the word index is 1-based and specifies the position *before* which the new content is inserted. For example, in a sentence "word1 word2 word3", a command to insert "word4" at index `2` will result in "word1 word4 word2 word3".
- **Folder Path Syntax:** When referencing a folder in any command (e.g., `MOVE`, `VIEWFOLDER`), the folder name must be prefixed with a forward slash (e.g., `/myfolder`).
- **Storage Server Initialization:** Each Storage Server must be launched with a unique integer as a command-line argument (e.g., `./ss 1`). This integer is used as the name for its root storage directory.

## 3. Core Assumptions

- **Publicly Known Name Server:** All Clients and Storage Servers are assumed to know the IP address and port numbers of the Name Server beforehand.
- **Stateless Clients:** Clients are mostly stateless, relying on the Name Server and Storage Servers to maintain the system's state.
- **POSIX-Compliant Environment:** The system is built using POSIX C libraries and assumes a compatible environment (like Linux) for compilation and execution.
- **File and Sentence Structure:**
    - A file is a collection of sentences.
    - A sentence is strictly defined as a sequence of words ending with a period (`.`), exclamation mark (`!`), or question mark (`?`).
    - **Word and Delimiter Tokenization:** The system's tokenizer treats every component as a distinct "word":
        - Alphanumeric words.
        - Sentence delimiters (`.`, `!`, `?`). (sometimes)
        - Whitespace characters (spaces, newlines).
        - The INFO command doesn't consider delimiters as words.
    - **Input Format Assumptions:** The system assumes that words and delimiters in any file content are separated by at least one space. The only exception is the final word of a sentence, which is immediately followed by its delimiter without a space (e.g., `"Hello world."`). This structure is critical for correct parsing.
- **User Management:** The system assumes that usernames are unique. There is no explicit user registration system; a user is considered "registered" the first time they connect with a new username.
- **Indexing:** Sentences are 0-indexed and words are 1-indexed.

## 4. Declarations and Design Choices

- **Centralized Metadata Management:** The Name Server acts as the single source of truth for all file metadata, including file locations, ownership, and access control lists (ACLs).
- **Efficient Search:** A **Trie** data structure is used on the Name Server to store file metadata, allowing for efficient prefix-based searches and lookups with a time complexity faster than O(N).
- **Caching:** An **LRU (Least Recently Used) Cache** is implemented on the Name Server to store frequently accessed `FileMetadata`. This reduces lookup latency for popular files.
- **Communication Protocol:** A custom, fixed-size binary messaging protocol is used for all inter-component communication over TCP sockets. `MessageType` enums define the set of possible operations.
- **Concurrency Control:**
    - **Multi-threading:** The Name Server and Storage Servers are multi-threaded to handle concurrent connections from multiple clients and servers.
    - **Sentence-Level Locking:** To manage concurrent edits, the system uses a per-sentence locking mechanism. A user must acquire a lock on a sentence before writing to it, preventing simultaneous edits to the same sentence.
    - **Temporary Write Files:** During a write operation, changes are made to a temporary file specific to the user's session. These changes are merged back into the main file only after the user commits the write, ensuring atomicity of the multi-step write operation.
- **Hierarchical Folders:** The system supports a hierarchical folder structure (a bonus feature). File and folder metadata are managed in separate Tries on the Name Server.
- **Checkpoints:** The implementation includes a checkpointing mechanism (a bonus feature) that allows the state of a file to be saved with a tag and reverted to later. Checkpoints are stored as distinct copies of the file on the Storage Server.
- **Reconnection:** When a client disconnects and reconnects again, their information is preserved. (assuming nm doesn't go down)
SS reconnection is also handled.


## 0. System Setup and Execution

### Prerequisites
- POSIX-compliant environment (Linux/Unix)
- GCC compiler with pthread support
- Make utility

### Compilation
```bash
make clean
make all
```

This will generate three executables:
- `nm` - Name Server
- `ss` - Storage Server
- `client` - User Client

### Starting the System

#### 1. Start the Name Server
The Name Server must be started first as it acts as the central coordinator:
```bash
./nm
```

The Name Server will listen on:
- Port 8080: Storage Server connections
- Port 8081: Client connections
- Port 8082: Heartbeat monitoring


#### 2. Start Storage Servers
Each Storage Server requires a unique integer identifier as a directory name:
```bash
./ss <nm_ip> <nm_port> <client_port> <dir_name>
```

**Parameters:**
- `<nm_ip>`: Name Server IP address (e.g., `127.0.0.1` for local testing)
- `<nm_port>`: Name Server port (must be `8080`)
- `<client_port>`: Port for direct client connections (e.g., `9001`, `9002`, etc.)
- `<dir_name>`: **Strictly an integer** (e.g., `1`, `2`, `3`). This creates storage directory `ss_storage_<dir_name>`

**Examples:**
```bash
./ss 127.0.0.1 8080 9001 1  # Storage Server 1
./ss 127.0.0.1 8080 9002 2  # Storage Server 2
./ss 127.0.0.1 8080 9003 3  # Storage Server 3
```

**Important Notes:**
- Each Storage Server must use a unique integer for `<dir_name>`
- Each Storage Server must use a unique `<client_port>`
- The `<nm_port>` must always be `8080` (standard NM-SS communication port)
- Storage directories are created automatically as `ss_storage_1`, `ss_storage_2`, etc.

#### 3. Start Clients
Clients connect to the Name Server to interact with the file system:
```bash
./client <nm_ip> <nm_port>
```

**Parameters:**
- `<nm_ip>`: Name Server IP address (e.g., `127.0.0.1`)
- `<nm_port>`: Name Server client port (must be `8081`)

**Example:**
```bash
./client 127.0.0.1 8081
```

Upon starting, the client will prompt for a username. This username is used for authentication and access control throughout the session.

### Example Complete Setup
```bash
# Terminal 1: Start Name Server
./nm

# Terminal 2: Start Storage Server 1
./ss 127.0.0.1 8080 9001 1

# Terminal 3: Start Storage Server 2
./ss 127.0.0.1 8080 9002 2

# Terminal 4: Start Client (User: alice)
./client 127.0.0.1 8081
# Enter username: alice

# Terminal 5: Start Client (User: bob)
./client 127.0.0.1 8081
# Enter username: bob
```

### Logs
The system generates log files for debugging and monitoring:
- `nm.log` - Name Server logs
- `ss_<id>.log` - Storage Server logs (one per SS)
- `client_<username>.log` - Client logs (one per user)

The system also works across devices if their IPs are known, and they are on the same network.

