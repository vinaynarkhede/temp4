# Quick Start Guide

Get ProxyMax up and running in 5 minutes!

## Step 1: Build

```bash
cd proxy-server
make
```

You should see:
```
Compiling src/main.c...
Compiling src/core/server.c...
...
Linking ./proxymax...
Build complete: ./proxymax
```

## Step 2: Run Basic HTTP Proxy

```bash
./proxymax -p 8080 -m http
```

You should see:
```
╔══════════════════════════════════════════════════════════╗
║         ProxyMax - Multithreaded Proxy Server           ║
║              Full-Featured Edition v1.0                  ║
╚══════════════════════════════════════════════════════════╝

[CONFIG] Proxy Configuration:
  ├─ Mode: HTTP
  ├─ Listen: 0.0.0.0:8080
  ├─ Threading: POOL (10 threads)
  └─ Status: Ready

[INFO] Proxy server starting on 0.0.0.0:8080...
[INFO] Server started successfully
```

## Step 3: Test the Proxy

**In another terminal:**

```bash
# Set proxy environment variable
export http_proxy=http://localhost:8080

# Test it
curl http://example.com
```

You should see the HTML content of example.com!

## Step 4: Try Advanced Features

### Enable Caching

```bash
./proxymax -p 8080 -m http --enable-cache
```

Test it:
```bash
# First request (cache miss)
time curl -x http://localhost:8080 http://example.com

# Second request (cache hit - should be faster!)
time curl -x http://localhost:8080 http://example.com
```

### Enable Authentication

```bash
./proxymax -p 8080 -m http --enable-auth
```

Test it:
```bash
# Without credentials (should fail with 407)
curl -x http://localhost:8080 http://example.com

# With credentials (default: admin/admin)
curl -x http://admin:admin@localhost:8080 http://example.com
```

### Try SOCKS5

```bash
./proxymax -p 1080 -m socks5
```

Test it:
```bash
curl --socks5 localhost:1080 http://example.com
```

## Step 5: Use Configuration File

Create a config file:
```bash
cat > my-proxy.conf << EOF
port=8080
mode=http
thread_model=pool
num_threads=20
enable_caching=1
enable_logging=1
enable_filtering=1
cache_size_mb=200
log_file=my-proxy.log
EOF
```

Run with config:
```bash
./proxymax -c my-proxy.conf
```

## Common Use Cases

### 1. Development Proxy
```bash
./proxymax -p 8080 -m http --enable-logging
```
Great for debugging HTTP traffic from your applications!

### 2. Caching Proxy
```bash
./proxymax -p 8080 -m http --enable-cache -t 20
```
Speed up repeated requests with in-memory caching!

### 3. Secure Proxy
```bash
./proxymax -p 8080 -m http --enable-auth --enable-filtering
```
Require authentication and block unwanted content!

### 4. High-Performance Proxy
```bash
./proxymax -p 8080 -m http -M hybrid -t 50 --enable-cache
```
Handle thousands of concurrent connections!

### 5. SOCKS5 Tunnel
```bash
./proxymax -p 1080 -m socks5 --enable-auth
```
Universal proxy for any TCP application!

## Configuring Your Browser

### Firefox
1. Settings → General → Network Settings → Settings
2. Select "Manual proxy configuration"
3. HTTP Proxy: `localhost`, Port: `8080`
4. Check "Also use this proxy for HTTPS"
5. Click OK

### Chrome/Chromium
```bash
chromium --proxy-server="http://localhost:8080"
```

Or use system proxy settings.

### Chrome with SOCKS5
```bash
chromium --proxy-server="socks5://localhost:1080"
```

## Testing HTTPS

The proxy automatically handles HTTPS through CONNECT tunneling:

```bash
curl -x http://localhost:8080 https://google.com
```

## Viewing Statistics

Stop the proxy with Ctrl+C to see statistics:

```
[INFO] Received signal 2, shutting down gracefully...

╔══════════════════════════════════════════════════════════╗
║                    Server Statistics                     ║
╠══════════════════════════════════════════════════════════╣
║ Total Connections:    42                                 ║
║ Active Connections:   0                                  ║
║ Total Requests:       156                                ║
║ Bytes Transferred:    1847293                            ║
║ Data Transferred:     1.76 MB                            ║
║ Uptime:               0d 00h 05m 23s                     ║
╚══════════════════════════════════════════════════════════╝
```

## Command Reference

| Option | Description | Example |
|--------|-------------|---------|
| `-p PORT` | Listen port | `-p 8080` |
| `-m MODE` | Proxy mode | `-m http`, `-m socks5` |
| `-t NUM` | Number of threads | `-t 20` |
| `-M MODEL` | Threading model | `-M pool`, `-M hybrid` |
| `--enable-cache` | Enable caching | |
| `--enable-auth` | Enable authentication | |
| `--enable-logging` | Enable logging | |
| `-c FILE` | Config file | `-c proxy.conf` |

## Troubleshooting

### "Address already in use"
Port is already taken. Use a different port:
```bash
./proxymax -p 8888 -m http
```

### "Permission denied" (port 80)
Ports < 1024 require root:
```bash
sudo ./proxymax -p 80 -m http
```

### Proxy not working
Check if it's running:
```bash
ps aux | grep proxymax
```

Test with curl:
```bash
curl -v -x http://localhost:8080 http://example.com
```

### High memory usage
Reduce cache size:
```bash
./proxymax -p 8080 -m http --enable-cache  # Uses default 100MB
```

Or use config file to set specific size.

## Next Steps

1. **Read the full README**: `cat README.md`
2. **Explore configuration**: Check `config/` directory for examples
3. **Learn the architecture**: Read `docs/ARCHITECTURE.md`
4. **Run tests**: `cd tests && ./test_proxy.sh`
5. **Customize**: Create your own configuration files

## Production Deployment

For production use, create a systemd service:

```bash
sudo nano /etc/systemd/system/proxymax.service
```

```ini
[Unit]
Description=ProxyMax Proxy Server
After=network.target

[Service]
Type=simple
User=proxy
ExecStart=/usr/local/bin/proxymax -c /etc/proxymax/proxy.conf
Restart=always

[Install]
WantedBy=multi-user.target
```

Enable and start:
```bash
sudo systemctl enable proxymax
sudo systemctl start proxymax
```

## Getting Help

- Check the README: `cat README.md`
- View help: `./proxymax --help`
- Architecture docs: `cat docs/ARCHITECTURE.md`
- Example configs: `ls config/`

## Common Patterns

### Pattern 1: Development Debugging
```bash
# Terminal 1: Run proxy with logging
./proxymax -p 8080 -m http --enable-logging

# Terminal 2: Test your app
export http_proxy=http://localhost:8080
./my-app

# Watch the proxy logs to see all HTTP traffic
```

### Pattern 2: Speed Up Web Browsing
```bash
# Large cache, many threads
./proxymax -p 8080 -m http --enable-cache -t 30
```

Configure your browser to use `localhost:8080`, enjoy faster page loads!

### Pattern 3: Secure Family Network
```bash
# Auth + filtering + logging
./proxymax -p 8080 -m http --enable-auth --enable-filtering --enable-logging
```

All family members must authenticate, harmful sites blocked, all activity logged.

### Pattern 4: Testing Tool
```bash
# Intercept and log all traffic
./proxymax -p 8080 -m http --enable-logging

# Point your test suite through the proxy
export http_proxy=http://localhost:8080
pytest tests/
```

Examine logs to debug API calls!

---

**Congratulations!** You're now running ProxyMax. Explore the features and customize it for your needs!
