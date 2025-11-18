# ProxyMax Architecture

## Overview

ProxyMax is designed as a modular, high-performance proxy server with a clean separation of concerns. The architecture supports multiple proxy protocols, threading models, and advanced features.

## System Architecture

```
┌─────────────────────────────────────────────────────────────┐
│                        Client Layer                         │
│  (HTTP Clients, SOCKS5 Clients, Browsers, Applications)    │
└─────────────────────────────────────────────────────────────┘
                            ▼
┌─────────────────────────────────────────────────────────────┐
│                    Connection Acceptor                      │
│              (Server Socket + accept() loop)                │
└─────────────────────────────────────────────────────────────┘
                            ▼
┌─────────────────────────────────────────────────────────────┐
│                     Threading Layer                         │
│  ┌──────────────┐  ┌──────────────┐  ┌─────────────────┐  │
│  │ Thread Pool  │  │ Event Loop   │  │ Per-Connection  │  │
│  │  (Default)   │  │   (epoll)    │  │   Threading     │  │
│  └──────────────┘  └──────────────┘  └─────────────────┘  │
└─────────────────────────────────────────────────────────────┘
                            ▼
┌─────────────────────────────────────────────────────────────┐
│                   Connection Handler                        │
│  ┌──────────────────────────────────────────────────────┐  │
│  │  ACL Check → Auth Check → Protocol Detection         │  │
│  └──────────────────────────────────────────────────────┘  │
└─────────────────────────────────────────────────────────────┘
                            ▼
┌─────────────────────────────────────────────────────────────┐
│                    Protocol Layer                           │
│  ┌──────────┐  ┌──────────┐  ┌──────────┐  ┌──────────┐  │
│  │   HTTP   │  │  HTTPS   │  │ SOCKS5   │  │   TCP    │  │
│  │ Handler  │  │ CONNECT  │  │ Handler  │  │  Relay   │  │
│  └──────────┘  └──────────┘  └──────────┘  └──────────┘  │
└─────────────────────────────────────────────────────────────┘
                            ▼
┌─────────────────────────────────────────────────────────────┐
│                    Feature Layer                            │
│  ┌──────┐  ┌──────┐  ┌──────┐  ┌──────┐  ┌──────────┐    │
│  │Cache │  │Filter│  │ Rate │  │ Log  │  │ Monitor  │    │
│  │      │  │      │  │Limit │  │      │  │          │    │
│  └──────┘  └──────┘  └──────┘  └──────┘  └──────────┘    │
└─────────────────────────────────────────────────────────────┘
                            ▼
┌─────────────────────────────────────────────────────────────┐
│                    Network Layer                            │
│           (Socket I/O, DNS Resolution, SSL/TLS)             │
└─────────────────────────────────────────────────────────────┘
                            ▼
┌─────────────────────────────────────────────────────────────┐
│                      Remote Servers                         │
│      (Destination Servers, Backend Servers, Internet)       │
└─────────────────────────────────────────────────────────────┘
```

## Component Architecture

### 1. Core Components

#### Server (`src/core/server.c`)
- Main server initialization and lifecycle
- Socket creation and binding
- Accept loop management
- Threading model selection
- Graceful shutdown handling

**Key Functions:**
- `proxy_server_init()`: Initialize server socket and subsystems
- `proxy_server_start()`: Start appropriate server loop
- `server_loop_*()`: Different implementations for each threading model

#### Connection (`src/core/connection.c`)
- Connection lifecycle management
- Client/remote socket pairing
- Data relay between endpoints
- Connection statistics tracking

**Key Functions:**
- `connection_create()`: Create new connection context
- `connection_handle()`: Main connection handler
- `connection_destroy()`: Clean up connection resources

#### Threading (`src/core/threading.c`)
- Thread pool implementation
- Work queue management
- Thread-safe synchronization
- Worker thread lifecycle

**Key Data Structures:**
```c
struct thread_pool {
    pthread_t *threads;      // Worker threads
    work_queue_t queue;      // Work queue
    int num_threads;         // Thread count
    volatile int shutdown;   // Shutdown flag
};

struct work_queue {
    work_item_t *head;       // Queue head
    work_item_t *tail;       // Queue tail
    pthread_mutex_t mutex;   // Queue lock
    pthread_cond_t cond;     // Condition variable
};
```

### 2. Protocol Handlers

#### HTTP Handler (`src/protocols/http.c`)
Handles HTTP and HTTPS requests with the following flow:

1. **Request Parsing**
   - Extract method, URL, version
   - Parse headers (Host, Authorization, etc.)
   - Validate request format

2. **Authentication Check**
   - Parse Proxy-Authorization header
   - Verify credentials if auth enabled

3. **Filtering**
   - Check URL against blacklist/whitelist
   - Apply content filtering rules

4. **Cache Check** (for GET requests)
   - Look up URL in cache
   - Return cached response if valid
   - Track cache hits/misses

5. **Request Types**
   - **CONNECT**: Establish SSL tunnel for HTTPS
   - **GET/POST/etc**: Forward HTTP request to server

6. **Response Handling**
   - Relay response to client
   - Store in cache if applicable
   - Track bandwidth

#### SOCKS5 Handler (`src/protocols/socks5.c`)
Implements SOCKS5 protocol (RFC 1928):

1. **Handshake**
   ```
   Client → Proxy: [VER, NMETHODS, METHODS]
   Proxy → Client: [VER, METHOD]
   ```

2. **Authentication** (if required)
   ```
   Client → Proxy: [VER, ULEN, USERNAME, PLEN, PASSWORD]
   Proxy → Client: [VER, STATUS]
   ```

3. **Request**
   ```
   Client → Proxy: [VER, CMD, RSV, ATYP, DST.ADDR, DST.PORT]
   Proxy → Client: [VER, REP, RSV, ATYP, BND.ADDR, BND.PORT]
   ```

4. **Data Relay**
   - Bidirectional data transfer
   - Transparent forwarding

### 3. Feature Modules

#### Cache (`src/cache/cache.c`)
**Implementation**: Hash table + LRU doubly-linked list

```c
struct cache_entry {
    char *key;                   // URL
    void *data;                  // Response data
    size_t size;                 // Data size
    time_t expiry;               // Expiration time
    LIST_ENTRY hash_entries;     // Hash table linkage
    TAILQ_ENTRY lru_entries;     // LRU list linkage
};
```

**Operations:**
- `cache_get()`: O(1) lookup + LRU update
- `cache_put()`: O(1) insert + eviction if needed
- LRU eviction when cache is full

**Thread Safety**: Mutex-protected operations

#### Authentication (`src/auth/auth.c`)
**Supported Methods:**
- HTTP Basic Authentication (Base64 encoded)
- SOCKS5 Username/Password

**User Storage:**
- Linked list of credentials
- File-based user database
- In-memory for fast lookup

**Security Considerations:**
- Passwords stored in plain text (file should be protected)
- Future: Add hashed password support (bcrypt, scrypt)

#### Filtering (`src/filter/filter.c`)
**Pattern Matching:**
- Wildcard support (* and ?)
- URL-based filtering
- IP-based access control

**Lists:**
- URL blacklist/whitelist
- IP blacklist/whitelist
- Configurable patterns

**Default Filters:**
```c
*malware*
*phishing*
*tracking*
*.ads.*
```

#### Logging (`src/monitor/logger.c`)
**Log Levels:**
- 0: DEBUG
- 1: INFO
- 2: WARN
- 3: ERROR

**Features:**
- Thread-safe logging
- Timestamp formatting
- File or stderr output
- Configurable log level

#### Statistics (`src/monitor/stats.c`)
**Tracked Metrics:**
- Total connections
- Active connections
- Total requests
- Bytes transferred
- Uptime
- Cache statistics

**Thread Safety**: Mutex-protected counters

## Threading Models

### 1. Thread Pool (Default)
```
┌─────────────┐
│   Accept    │
│    Loop     │
└──────┬──────┘
       │
       ▼
┌─────────────┐
│ Work Queue  │
└──────┬──────┘
       │
       ▼
┌─────────────────────────┐
│  Worker Thread Pool     │
│ [T1][T2][T3]...[Tn]    │
└─────────────────────────┘
```

**Pros:**
- Controlled resource usage
- Good for moderate traffic
- Predictable performance

**Cons:**
- Queue can fill up under heavy load
- Context switching overhead

### 2. Event-Driven (epoll)
```
┌──────────────────────┐
│   epoll_wait()       │
│   Event Loop         │
└──────────────────────┘
         │
    ┌────┴────┐
    ▼         ▼
[Server]  [Clients]
 Socket    Sockets
```

**Pros:**
- Highly scalable (10K+ connections)
- Low memory footprint
- Efficient I/O multiplexing

**Cons:**
- More complex implementation
- Linux-specific (epoll)

### 3. Hybrid (epoll + Thread Pool)
```
┌──────────────────────┐
│   epoll_wait()       │
│   Event Loop         │
└──────────────────────┘
         │
         ▼
┌──────────────────────┐
│   Thread Pool        │
│  Process Events      │
└──────────────────────┘
```

**Pros:**
- Best of both worlds
- Scalable I/O + parallel processing
- Production-ready performance

**Cons:**
- Most complex implementation
- Higher memory usage

### 4. Thread-per-Connection
```
Accept → Create Thread → Handle → Exit
         └──────────────────────┘
```

**Pros:**
- Simple implementation
- Good for learning

**Cons:**
- Limited scalability
- High resource usage

## Data Flow

### HTTP Request Flow
```
1. Client connects
2. Server accepts connection
3. Create connection context
4. Submit to thread pool / event loop
5. Read HTTP request
6. Parse request (method, URL, headers)
7. Check authentication
8. Check filtering
9. Check cache
   ├─ Hit: Return cached response
   └─ Miss: Continue
10. Connect to remote server
11. Forward request
12. Receive response
13. Store in cache (if GET)
14. Forward to client
15. Close connections
16. Update statistics
```

### SOCKS5 Connection Flow
```
1. Client connects
2. SOCKS5 handshake
   ├─ Version negotiation
   └─ Method selection
3. Authentication (if required)
   ├─ Username/password
   └─ Verify credentials
4. SOCKS5 request
   ├─ Parse destination
   └─ Connect to target
5. Send success reply
6. Bidirectional relay
   └─ Forward data both ways
7. Close on EOF/error
```

## Memory Management

### Buffer Management
- Stack-allocated buffers for small operations
- Dynamic allocation for variable-size data
- Reuse buffers where possible

### Connection Context
- Allocated on connection creation
- Freed after connection closes
- Contains all connection-specific data

### Cache Memory
- Configurable max size
- LRU eviction when full
- Automatic cleanup on shutdown

## Error Handling

### Network Errors
- Connection refused: Log and send error response
- Timeout: Close connection gracefully
- DNS failure: Return 502 Bad Gateway

### Protocol Errors
- Malformed requests: Return 400 Bad Request
- Unsupported features: Return appropriate error code

### System Errors
- Out of memory: Log error, refuse new connections
- Too many open files: Increase ulimit, queue connections

## Performance Optimizations

### Current
- Non-blocking I/O with epoll
- Thread pooling to avoid creation overhead
- Connection keep-alive
- Efficient buffer sizes (64KB)
- LRU cache with O(1) operations

### Planned
- Zero-copy transfers (splice/sendfile)
- Connection pooling to backend
- HTTP pipelining
- Response compression
- Smart buffer resizing

## Configuration System

### Configuration Hierarchy
1. Default values (hardcoded)
2. Configuration file
3. Command-line arguments

### File Format
```ini
key=value
# Comments start with #
```

### Hot Reload
Future enhancement: Support SIGHUP for config reload

## Security Architecture

### Defense in Depth
1. **Network Layer**: Bind to specific interface
2. **Access Control**: IP whitelist/blacklist
3. **Authentication**: User/password verification
4. **Filtering**: URL/content blocking
5. **Rate Limiting**: Prevent abuse
6. **Logging**: Audit trail

### Future Security Enhancements
- SSL/TLS termination
- Certificate validation
- DDoS mitigation
- Rate limiting per IP
- Intrusion detection

## Extensibility

### Adding New Protocol
1. Create handler in `src/protocols/`
2. Implement protocol-specific logic
3. Add to `connection_handle()` switch
4. Update configuration

### Adding New Feature
1. Create module in appropriate directory
2. Add initialization in `main.c`
3. Add cleanup in shutdown sequence
4. Update configuration system

## Testing Strategy

### Unit Testing
- Individual function testing
- Protocol parsing validation
- Cache operations

### Integration Testing
- Full request/response cycle
- Multi-client scenarios
- Error handling paths

### Performance Testing
- Benchmark throughput
- Latency measurements
- Concurrent connection handling
- Memory usage profiling

## Deployment Considerations

### Production Checklist
- [ ] Enable authentication
- [ ] Configure IP filtering
- [ ] Set appropriate cache size
- [ ] Enable logging
- [ ] Monitor resource usage
- [ ] Set file descriptor limits
- [ ] Use systemd for management
- [ ] Configure firewall rules
- [ ] Regular log rotation
- [ ] Backup configuration

### Monitoring
- Watch active connections
- Track cache hit rate
- Monitor bandwidth usage
- Check error logs
- Review authentication failures

## Future Roadmap

### Phase 1: Core Improvements
- Full SSL/TLS implementation
- Rate limiting enforcement
- Multi-backend load balancing

### Phase 2: Advanced Features
- HTTP/2 support
- WebSocket proxying
- Response compression (gzip/brotli)
- Admin web interface

### Phase 3: Enterprise Features
- Configuration hot-reload
- Metrics API (Prometheus)
- Health checking
- Geographic routing
- HA/clustering support
