# ProxyMax - New Features Guide

This document describes the newly implemented features in ProxyMax v2.0.

## Table of Contents
1. [Rate Limiting & Traffic Management](#rate-limiting--traffic-management)
2. [Multi-Backend Load Balancing](#multi-backend-load-balancing)
3. [HTTP Response Compression](#http-response-compression)
4. [SSL/TLS Support](#ssltls-support)

---

## Rate Limiting & Traffic Management

### Overview
ProxyMax now includes comprehensive rate limiting to protect your backend servers and prevent abuse.

### Features

**1. Request Rate Limiting**
- Token bucket algorithm for smooth rate limiting
- Per-IP tracking
- Configurable requests per minute
- Automatic token refill

**2. Bandwidth Throttling**
- Per-IP bandwidth tracking
- Configurable bandwidth limits in KB/s
- 60-second sliding window
- Upload and download tracking

### Configuration

```ini
# Enable rate limiting
enable_rate_limiting=1

# Allow 100 requests per minute per IP
rate_limit_requests=100

# Limit bandwidth to 10 Mbps (10240 KB/s)
rate_limit_bandwidth_kbps=10240
```

### Usage Examples

**Basic Rate Limiting:**
```bash
./proxymax -p 8080 -m http --enable-rate-limit
```

**Custom Limits:**
```ini
# config/rate-limited.conf
port=8080
mode=http
enable_rate_limiting=1
rate_limit_requests=50      # 50 requests/min
rate_limit_bandwidth_kbps=5120  # 5 Mbps
```

### How It Works

**Request Rate Limiting:**
1. Each IP gets a token bucket with N tokens (rate_limit_requests)
2. Each request consumes 1 token
3. Tokens refill at rate_limit_requests/60 per second
4. Requests blocked when no tokens available

**Bandwidth Limiting:**
1. Tracks bytes sent/received per IP in 60-second window
2. Checks if transfer would exceed limit before allowing
3. Window resets every 60 seconds
4. Returns 429 Too Many Requests if exceeded

### API

```c
// Check if IP can make request
int ratelimit_check_request(const char *ip);

// Check if bandwidth transfer allowed
int ratelimit_check_bandwidth(const char *ip, size_t bytes, int direction);

// Update bandwidth counters
void ratelimit_update_bandwidth(const char *ip, size_t bytes, int direction);
```

---

## Multi-Backend Load Balancing

### Overview
Support for multiple backend servers with intelligent load balancing algorithms.

### Supported Algorithms

**1. Round-Robin (Default)**
- Distributes requests evenly across backends
- Simple and fair distribution
- Best for: Homogeneous backend servers

**2. Least Connections**
- Routes to backend with fewest active connections
- Dynamic load balancing
- Best for: Long-lived connections

**3. IP Hash**
- Consistent hashing based on client IP
- Same client always goes to same backend
- Best for: Session persistence

**4. Weighted Round-Robin**
- Round-robin with backend weights
- More powerful servers get more requests
- Best for: Heterogeneous backends

### Configuration

**Single Backend (Default):**
```ini
mode=reverse
backend_host=192.168.1.100
backend_port=8080
```

**Multiple Backends:**
```ini
mode=reverse
lb_algorithm=round-robin  # or: least-connections, ip-hash, weighted-rr
num_backends=3

# Backend configuration
backend_1=192.168.1.100:8080:1  # host:port:weight
backend_2=192.168.1.101:8080:2  # weight=2 (double traffic)
backend_3=192.168.1.102:8080:1
```

### Health Checking

ProxyMax automatically monitors backend health:
- Health checks every 30 seconds
- Unhealthy backends removed from rotation
- Automatic recovery when backend comes back
- Failed request tracking

### Usage Examples

**Round-Robin Load Balancer:**
```bash
./proxymax -p 80 -m reverse --enable-cache
```

**Weighted Load Balancer:**
```ini
# config/load-balanced.conf
port=80
mode=reverse
lb_algorithm=weighted-rr
num_backends=2
backend_1=server1.local:8080:2  # Gets 66% of traffic
backend_2=server2.local:8080:1  # Gets 33% of traffic
enable_caching=1
```

### Monitoring

```c
// Get backend statistics
void loadbalancer_print_stats(void);

// Example output:
// [LOAD BALANCER] Backend Statistics:
//   [0] 192.168.1.100:8080 - Active: 5, Total: 1234, Failed: 2, Status: Healthy
//   [1] 192.168.1.101:8080 - Active: 3, Total: 1156, Failed: 0, Status: Healthy
```

---

## HTTP Response Compression

### Overview
Automatic compression of HTTP responses using gzip or deflate to reduce bandwidth.

### Features

- **Automatic Detection**: Checks Accept-Encoding header
- **Smart Compression**: Only compresses text-based content
- **Size Thresholds**: Only compresses responses > 1KB
- **Format Support**: gzip and deflate
- **Content-Type Aware**: Compresses HTML, JSON, JavaScript, CSS, XML

### Configuration

```ini
# Enable compression
enable_compression=1
```

### Automatic Behavior

**Compression Happens When:**
1. Client sends `Accept-Encoding: gzip` header
2. Response Content-Type is text-based
3. Response size > 1KB and < 10MB
4. Not already compressed

**Supported Content Types:**
- text/* (HTML, plain, CSS)
- application/json
- application/javascript
- application/xml
- application/xhtml+xml

### Usage

```bash
# Enable compression for HTTP proxy
./proxymax -p 8080 -m http --enable-compression

# Test it
curl -H "Accept-Encoding: gzip" -x http://localhost:8080 http://example.com
```

### Performance Impact

**Bandwidth Savings:**
- HTML: 70-90% reduction
- JSON: 60-80% reduction
- JavaScript: 50-70% reduction
- CSS: 60-80% reduction

**CPU Impact:**
- Minimal for text content
- ~5-10ms per request for typical web pages
- Automatic skip for already compressed content

### API

```c
// Check if client accepts compression
int compression_check_accept_encoding(const char *headers);

// Compress data with gzip
int compression_gzip_compress(const unsigned char *input, size_t input_len,
                               unsigned char **output, size_t *output_len);

// Compress with deflate
int compression_deflate_compress(const unsigned char *input, size_t input_len,
                                  unsigned char **output, size_t *output_len);
```

---

## SSL/TLS Support

### Overview
Full SSL/TLS termination with OpenSSL for secure HTTPS connections.

### Features

- **TLS 1.2/1.3**: Modern TLS versions only
- **Auto Certificate Generation**: Creates self-signed certs if missing
- **Custom Certificates**: Use your own cert/key files
- **Strong Ciphers**: Modern cipher suites
- **Client & Server**: Both client and server SSL contexts
- **Certificate Info**: View peer certificate details

### Configuration

```ini
# Enable SSL/TLS
enable_ssl=1

# Certificate files (auto-generated if missing)
ssl_cert=certs/server.crt
ssl_key=certs/server.key
```

### Usage Examples

**HTTPS Proxy (SSL Termination):**
```bash
./proxymax -p 443 -m http --enable-ssl
```

**Auto-Generate Certificate:**
```bash
# ProxyMax will create self-signed cert if files don't exist
./proxymax -p 443 -m https --enable-ssl

# Output:
# [WARN] SSL certificate not found, generating self-signed certificate...
# [INFO] Generated self-signed certificate:
# [INFO]   Certificate: certs/server.crt
# [INFO]   Private Key: certs/server.key
```

**Use Custom Certificate:**
```bash
# Place your certificate files
mkdir -p certs
cp my-cert.crt certs/server.crt
cp my-key.key certs/server.key

./proxymax -p 443 -m https --enable-ssl
```

### Certificate Generation

**Manual Generation:**
```bash
# Create certs directory
mkdir -p certs

# ProxyMax can generate self-signed certs
# Or use openssl manually:
openssl req -x509 -newkey rsa:2048 -nodes \
  -keyout certs/server.key \
  -out certs/server.crt \
  -days 365 \
  -subj "/C=US/O=ProxyMax/CN=localhost"
```

### TLS Configuration

**Supported TLS Versions:**
- TLS 1.2 (minimum)
- TLS 1.3

**Cipher Suites:**
- ECDHE-RSA-AES256-GCM-SHA384
- ECDHE-RSA-AES128-GCM-SHA256
- ECDHE-RSA-AES256-SHA384
- ECDHE-RSA-AES128-SHA256

### SSL API

```c
// Initialize SSL/TLS
int ssl_init(void);

// Create server context with cert/key
int ssl_create_server_context(const char *cert_file, const char *key_file);

// Accept SSL connection
ssl_context_t* ssl_accept_connection(int sockfd);

// SSL I/O
ssize_t ssl_read(ssl_context_t *ctx, void *buf, size_t len);
ssize_t ssl_write(ssl_context_t *ctx, const void *buf, size_t len);

// Get certificate info
int ssl_get_peer_cert_info(ssl_context_t *ctx, char *buf, size_t size);

// Get cipher info
const char* ssl_get_cipher_info(ssl_context_t *ctx);

// Cleanup
void ssl_shutdown(ssl_context_t *ctx);
```

### Security Notes

**Production Recommendations:**
1. Use valid CA-signed certificates (not self-signed)
2. Keep private keys secure (chmod 600)
3. Regular certificate rotation
4. Monitor SSL/TLS handshake failures
5. Use strong cipher suites only

**Testing with Self-Signed Certificates:**
```bash
# curl with self-signed cert
curl -k -x https://localhost:443 https://example.com

# Browser: Add security exception for localhost
```

---

## Combining Features

### Example: Production Reverse Proxy

```ini
# config/production.conf
# High-performance SSL reverse proxy with all features

port=443
mode=reverse
thread_model=hybrid
num_threads=50

# SSL/TLS
enable_ssl=1
ssl_cert=certs/production.crt
ssl_key=certs/production.key

# Load Balancing
lb_algorithm=least-connections
num_backends=3
backend_1=app1.internal:8080:1
backend_2=app2.internal:8080:1
backend_3=app3.internal:8080:1

# Performance
enable_caching=1
cache_size_mb=1000
cache_ttl_seconds=3600
enable_compression=1

# Security
enable_rate_limiting=1
rate_limit_requests=1000
rate_limit_bandwidth_kbps=102400
enable_auth=0  # Use backend authentication

# Monitoring
enable_logging=1
log_file=/var/log/proxymax/access.log
log_level=1
```

### Example: Secure Caching Proxy

```ini
# config/secure-cache.conf
# Authenticated caching proxy with rate limiting

port=8080
mode=http
thread_model=pool
num_threads=20

# Authentication
enable_auth=1
auth_file=config/users.txt

# Caching
enable_caching=1
cache_size_mb=500

# Compression
enable_compression=1

# Rate Limiting
enable_rate_limiting=1
rate_limit_requests=100
rate_limit_bandwidth_kbps=10240

# Content Filtering
enable_filtering=1

# Logging
enable_logging=1
log_file=secure-proxy.log
```

---

## Performance Impact

### Resource Usage by Feature

| Feature | CPU Impact | Memory Impact | Network Impact |
|---------|-----------|---------------|----------------|
| Rate Limiting | Very Low | Low (per-IP tracking) | None |
| Load Balancing | Low | Low (backend list) | None |
| Compression | Medium | Medium (buffers) | -70% bandwidth |
| SSL/TLS | High | Medium (SSL contexts) | +5% overhead |

### Recommended Configurations

**Low Resource (1 CPU, 1GB RAM):**
```ini
num_threads=10
enable_caching=1
cache_size_mb=100
enable_compression=1
enable_rate_limiting=1
```

**Medium Resource (4 CPU, 4GB RAM):**
```ini
thread_model=hybrid
num_threads=30
enable_caching=1
cache_size_mb=500
enable_compression=1
enable_ssl=1
enable_rate_limiting=1
```

**High Resource (16+ CPU, 16GB+ RAM):**
```ini
thread_model=hybrid
num_threads=100
enable_caching=1
cache_size_mb=4000
enable_compression=1
enable_ssl=1
enable_rate_limiting=1
lb_algorithm=least-connections
num_backends=10
```

---

## Migration Guide

### Upgrading from v1.0

**1. Update Configuration:**
```ini
# Old (v1.0)
port=8080
mode=reverse
backend_host=server.local
backend_port=8080

# New (v2.0) - same behavior
port=8080
mode=reverse
backend_host=server.local
backend_port=8080

# New (v2.0) - with new features
port=8080
mode=reverse
lb_algorithm=round-robin
num_backends=1
backend_1=server.local:8080:1
enable_compression=1
enable_rate_limiting=1
```

**2. No Breaking Changes:**
- All v1.0 configurations still work
- New features are opt-in
- Default behavior unchanged

**3. Recommended Additions:**
```ini
# Add to existing configs for improved performance
enable_compression=1
enable_rate_limiting=1
rate_limit_requests=1000
```

---

## Troubleshooting

### Rate Limiting

**Problem: Legitimate users being rate limited**
```ini
# Increase limits
rate_limit_requests=500  # Higher request limit
rate_limit_bandwidth_kbps=51200  # 50 Mbps
```

**Problem: Too many rate limit entries in memory**
```c
// Call periodically to clean up old entries
ratelimit_cleanup_old_entries();
```

### Load Balancing

**Problem: Backend marked unhealthy**
```bash
# Check backend is accessible
curl http://backend-host:port/

# View health check results
./proxymax -c config.conf
# Watch for health check messages in logs
```

**Problem: Uneven distribution**
```ini
# Use different algorithm
lb_algorithm=least-connections  # Better for varying request times
```

### Compression

**Problem: Compression not working**
```bash
# Verify client sends Accept-Encoding
curl -H "Accept-Encoding: gzip" -x http://localhost:8080 http://example.com

# Check response size is > 1KB
# Check Content-Type is compressible
```

### SSL/TLS

**Problem: Certificate errors**
```bash
# Regenerate self-signed certificate
rm certs/server.crt certs/server.key
./proxymax -p 443 -m https --enable-ssl

# Check certificate validity
openssl x509 -in certs/server.crt -text -noout
```

**Problem: SSL handshake failures**
```bash
# Check OpenSSL version
openssl version

# Test SSL connection
openssl s_client -connect localhost:443
```

---

## API Reference

See individual feature sections above for complete API documentation.

**Rate Limiting:** `src/monitor/ratelimit.c`
**Load Balancing:** `src/core/loadbalancer.c`
**Compression:** `src/protocols/compression.c`
**SSL/TLS:** `src/protocols/ssl.c`

---

## Version History

**v2.0 (Current)**
- Added rate limiting & traffic management
- Added multi-backend load balancing with 4 algorithms
- Added HTTP response compression (gzip/deflate)
- Added full SSL/TLS support with OpenSSL
- Added backend health checking
- Added automatic certificate generation

**v1.0**
- Initial release with basic proxy features
- HTTP/HTTPS/SOCKS5 support
- Basic caching
- Authentication
- Content filtering
