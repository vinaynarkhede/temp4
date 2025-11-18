# ProxyMax - Full-Featured Multithreaded Proxy Server

A comprehensive, high-performance proxy server written in C with support for multiple protocols, threading models, and advanced features.

## Features

### Core Functionality
- ✅ **Multiple Proxy Modes**
  - HTTP proxy (forward proxy)
  - HTTPS proxy with CONNECT tunneling
  - SOCKS5 proxy with authentication
  - TCP relay proxy
  - Reverse proxy with load balancing
  - Transparent proxy

### Threading Models
- ✅ **Thread-per-connection**: Dedicated thread for each client
- ✅ **Thread Pool**: Fixed pool of worker threads with work queue
- ✅ **Event-Driven**: epoll-based event loop for high scalability
- ✅ **Hybrid**: Combines epoll with thread pool for optimal performance

### Advanced Features
- ✅ **Caching System**
  - In-memory caching with LRU eviction
  - Configurable cache size and TTL
  - Cache statistics and hit/miss tracking

- ✅ **Authentication**
  - HTTP Basic authentication
  - SOCKS5 username/password authentication
  - File-based user management

- ✅ **Access Control**
  - IP whitelist/blacklist
  - URL filtering with wildcard patterns
  - Content filtering and inspection

- ✅ **Monitoring & Logging**
  - Detailed request logging
  - Connection statistics
  - Bandwidth tracking
  - Uptime monitoring
  - Real-time statistics

- ✅ **Performance Features**
  - Non-blocking I/O
  - Connection pooling
  - Zero-copy transfers (splice/sendfile ready)
  - Efficient buffer management

## Building

### Prerequisites
- GCC compiler
- POSIX threads (pthread)
- Linux (for epoll support)

### Compile
```bash
make
```

### Debug Build
```bash
make debug
```

### Clean
```bash
make clean
```

### Install
```bash
make install
```

## Usage

### Basic Usage

**HTTP Proxy on port 8080:**
```bash
./proxymax -p 8080 -m http
```

**SOCKS5 Proxy with authentication:**
```bash
./proxymax -p 1080 -m socks5 --enable-auth
```

**HTTP Proxy with caching:**
```bash
./proxymax -p 8080 -m http --enable-cache --enable-logging
```

**Reverse Proxy:**
```bash
./proxymax -p 80 -m reverse --enable-cache
```

### Command Line Options

```
Options:
  -c, --config FILE      Load configuration from FILE
  -p, --port PORT        Listen port (default: 8080)
  -b, --bind ADDR        Bind address (default: 0.0.0.0)
  -m, --mode MODE        Proxy mode: http|https|socks5|tcp|reverse
  -t, --threads NUM      Number of worker threads (default: 10)
  -M, --thread-model M   Threading model: pool|event|hybrid|perconn
  --enable-cache         Enable caching
  --enable-ssl           Enable SSL/TLS support
  --enable-auth          Enable authentication
  --enable-logging       Enable logging
  --enable-filtering     Enable content filtering
  --enable-compression   Enable compression
  --enable-rate-limit    Enable rate limiting
  -h, --help             Show help message
```

### Configuration File

Use a configuration file for advanced settings:

```bash
./proxymax -c config/proxy.conf
```

**Example Configuration:**
```ini
# Network settings
port=8080
bind_address=0.0.0.0
mode=http

# Threading
thread_model=pool
num_threads=20

# Features
enable_caching=1
enable_auth=1
enable_logging=1
enable_filtering=1

# Cache settings
cache_size_mb=200
cache_ttl_seconds=3600

# Authentication
auth_file=config/users.txt

# Logging
log_file=proxy.log
log_level=1
```

## Configuration Examples

### 1. Corporate HTTP Proxy
```bash
./proxymax -c config/http-proxy.conf
```
- HTTP proxy on port 8080
- Caching enabled (200 MB)
- URL filtering for security
- Request logging

### 2. SOCKS5 Proxy Server
```bash
./proxymax -c config/socks5-proxy.conf
```
- SOCKS5 on port 1080
- User authentication required
- IP-based access control
- Connection logging

### 3. Reverse Proxy / Load Balancer
```bash
./proxymax -c config/reverse-proxy.conf
```
- Reverse proxy on port 80
- Backend server load balancing
- Static content caching (500 MB)
- Rate limiting protection
- High-performance hybrid threading

## Testing

### Test HTTP Proxy

**Using curl:**
```bash
# Set proxy
export http_proxy=http://localhost:8080
export https_proxy=http://localhost:8080

# Make requests
curl http://example.com
curl https://google.com

# With authentication
curl -x http://admin:admin@localhost:8080 http://example.com
```

**Using browser:**
1. Configure browser proxy settings to `localhost:8080`
2. Browse websites through the proxy

### Test SOCKS5 Proxy

**Using curl:**
```bash
# SOCKS5 without auth
curl --socks5 localhost:1080 http://example.com

# SOCKS5 with auth
curl --socks5 admin:admin@localhost:1080 http://example.com
```

**Using proxychains:**
```bash
# Edit /etc/proxychains.conf
# Add: socks5 127.0.0.1 1080 admin admin

proxychains curl http://example.com
```

## Architecture

### Directory Structure
```
proxy-server/
├── src/
│   ├── main.c              # Entry point and CLI parsing
│   ├── core/
│   │   ├── server.c        # Main server loop
│   │   ├── connection.c    # Connection handling
│   │   └── threading.c     # Thread pool implementation
│   ├── protocols/
│   │   ├── http.c          # HTTP/HTTPS protocol handler
│   │   └── socks5.c        # SOCKS5 protocol handler
│   ├── cache/
│   │   └── cache.c         # LRU cache implementation
│   ├── auth/
│   │   └── auth.c          # Authentication system
│   ├── filter/
│   │   └── filter.c        # URL/IP filtering
│   ├── monitor/
│   │   ├── logger.c        # Logging system
│   │   └── stats.c         # Statistics tracking
│   └── utils/
│       ├── config.c        # Configuration parser
│       └── network.c       # Network utilities
├── include/
│   └── proxy.h             # Header file
├── config/                 # Configuration files
├── Makefile
└── README.md
```

### Threading Models

**1. Thread Pool (Recommended)**
- Fixed number of worker threads
- Work queue for pending connections
- Good balance of performance and resource usage
- Best for: General purpose, moderate traffic

**2. Event-Driven**
- epoll-based event loop
- Highly scalable (10K+ concurrent connections)
- Low memory footprint
- Best for: High concurrency, low latency

**3. Hybrid**
- Combines epoll with thread pool
- Maximum performance
- Handles both I/O and CPU-bound tasks efficiently
- Best for: Production deployments, heavy traffic

**4. Thread-per-Connection**
- Simple model, one thread per client
- Limited scalability
- Best for: Development, low traffic

## Performance Tuning

### For High Traffic
```ini
thread_model=hybrid
num_threads=50
enable_caching=1
cache_size_mb=1000
```

### For Low Latency
```ini
thread_model=event
enable_caching=1
cache_ttl_seconds=300
```

### For Security
```ini
enable_auth=1
enable_filtering=1
enable_rate_limiting=1
rate_limit_requests=100
```

## Advanced Features

### Caching
- Automatic caching of HTTP GET requests
- LRU (Least Recently Used) eviction policy
- Configurable cache size and TTL
- Cache hit/miss statistics

### Authentication
- HTTP Basic authentication for HTTP proxy
- SOCKS5 username/password authentication
- File-based user database (`config/users.txt`)
- Format: `username:password` (one per line)

### Filtering
- URL pattern matching with wildcards
- IP-based access control
- Whitelist/blacklist support
- Default blocks: malware, phishing, tracking sites

### Monitoring
- Real-time connection statistics
- Bandwidth usage tracking
- Request logging with timestamps
- Uptime monitoring
- Performance metrics

## Security Considerations

1. **Authentication**: Always enable authentication for public-facing proxies
2. **Filtering**: Use IP whitelisting to restrict access
3. **Logging**: Enable logging to track usage and detect abuse
4. **Rate Limiting**: Protect against DoS attacks
5. **SSL/TLS**: Use encrypted connections when handling sensitive data

## Limitations & Future Enhancements

**Current Limitations:**
- SSL/TLS support is framework-ready but not fully implemented
- Compression support is framework-ready but not implemented
- Rate limiting is framework-ready but not actively enforced
- Load balancing supports single backend (multi-backend planned)

**Planned Features:**
- Full SSL/TLS termination with OpenSSL
- HTTP/2 support
- WebSocket proxying
- gzip/deflate compression
- Advanced load balancing (least connections, IP hash)
- Health checking for backends
- Admin web interface
- Real-time metrics API
- Configuration hot-reload

## Troubleshooting

**Port already in use:**
```bash
# Check what's using the port
sudo lsof -i :8080

# Use a different port
./proxymax -p 8888
```

**Permission denied on port 80:**
```bash
# Ports < 1024 require root
sudo ./proxymax -p 80 -m reverse
```

**Too many open files:**
```bash
# Increase file descriptor limit
ulimit -n 65536
```

## Contributing

Contributions are welcome! This is a comprehensive educational project demonstrating:
- Network programming in C
- Multithreading and synchronization
- Protocol implementation
- System design patterns
- Performance optimization

## License

This project is provided as-is for educational purposes.

## Author

Created as a comprehensive example of multithreaded proxy server implementation in C.

---

**Version:** 1.0
**Last Updated:** 2025
