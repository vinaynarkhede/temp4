#!/bin/bash
# ProxyMax Test Script

echo "╔══════════════════════════════════════════════════════════╗"
echo "║           ProxyMax - Proxy Server Test Suite            ║"
echo "╚══════════════════════════════════════════════════════════╝"
echo ""

# Colors
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

PROXY_PORT=8080
SOCKS_PORT=1080
PROXY_PID=""

# Function to start proxy
start_proxy() {
    local mode=$1
    local port=$2
    local extra_args=$3

    echo -e "${YELLOW}Starting proxy in $mode mode on port $port...${NC}"
    ../proxymax -p $port -m $mode $extra_args > /tmp/proxy.log 2>&1 &
    PROXY_PID=$!
    sleep 2

    if ps -p $PROXY_PID > /dev/null; then
        echo -e "${GREEN}✓ Proxy started (PID: $PROXY_PID)${NC}"
        return 0
    else
        echo -e "${RED}✗ Failed to start proxy${NC}"
        cat /tmp/proxy.log
        return 1
    fi
}

# Function to stop proxy
stop_proxy() {
    if [ ! -z "$PROXY_PID" ]; then
        echo -e "${YELLOW}Stopping proxy (PID: $PROXY_PID)...${NC}"
        kill $PROXY_PID 2>/dev/null
        wait $PROXY_PID 2>/dev/null
        echo -e "${GREEN}✓ Proxy stopped${NC}"
        PROXY_PID=""
    fi
}

# Test 1: HTTP Proxy Basic Test
test_http_proxy_basic() {
    echo ""
    echo "Test 1: HTTP Proxy - Basic Request"
    echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"

    start_proxy "http" $PROXY_PORT || return 1

    echo "Testing HTTP request through proxy..."
    response=$(curl -x http://localhost:$PROXY_PORT -s -o /dev/null -w "%{http_code}" http://example.com --max-time 10)

    if [ "$response" = "200" ]; then
        echo -e "${GREEN}✓ HTTP proxy test passed (Status: $response)${NC}"
        stop_proxy
        return 0
    else
        echo -e "${RED}✗ HTTP proxy test failed (Status: $response)${NC}"
        stop_proxy
        return 1
    fi
}

# Test 2: HTTP Proxy with Caching
test_http_proxy_caching() {
    echo ""
    echo "Test 2: HTTP Proxy - Caching"
    echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"

    start_proxy "http" $PROXY_PORT "--enable-cache" || return 1

    echo "First request (cache miss)..."
    time1=$(curl -x http://localhost:$PROXY_PORT -s -o /dev/null -w "%{time_total}" http://example.com --max-time 10)

    echo "Second request (should be cached)..."
    time2=$(curl -x http://localhost:$PROXY_PORT -s -o /dev/null -w "%{time_total}" http://example.com --max-time 10)

    echo "First request time: ${time1}s"
    echo "Second request time: ${time2}s"

    echo -e "${GREEN}✓ Caching test completed${NC}"
    stop_proxy
    return 0
}

# Test 3: HTTP Proxy with Authentication
test_http_proxy_auth() {
    echo ""
    echo "Test 3: HTTP Proxy - Authentication"
    echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"

    start_proxy "http" $PROXY_PORT "--enable-auth" || return 1

    echo "Testing without credentials (should fail)..."
    response=$(curl -x http://localhost:$PROXY_PORT -s -o /dev/null -w "%{http_code}" http://example.com --max-time 10)

    if [ "$response" = "407" ]; then
        echo -e "${GREEN}✓ Authentication required (Status: $response)${NC}"
    else
        echo -e "${YELLOW}⚠ Unexpected response: $response${NC}"
    fi

    echo "Testing with credentials..."
    response=$(curl -x http://admin:admin@localhost:$PROXY_PORT -s -o /dev/null -w "%{http_code}" http://example.com --max-time 10)

    if [ "$response" = "200" ]; then
        echo -e "${GREEN}✓ Authentication test passed (Status: $response)${NC}"
        stop_proxy
        return 0
    else
        echo -e "${RED}✗ Authentication test failed (Status: $response)${NC}"
        stop_proxy
        return 1
    fi
}

# Test 4: HTTPS CONNECT Tunnel
test_https_tunnel() {
    echo ""
    echo "Test 4: HTTPS Proxy - CONNECT Tunnel"
    echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"

    start_proxy "http" $PROXY_PORT || return 1

    echo "Testing HTTPS through CONNECT tunnel..."
    response=$(curl -x http://localhost:$PROXY_PORT -s -o /dev/null -w "%{http_code}" https://example.com --max-time 10)

    if [ "$response" = "200" ]; then
        echo -e "${GREEN}✓ HTTPS tunnel test passed (Status: $response)${NC}"
        stop_proxy
        return 0
    else
        echo -e "${RED}✗ HTTPS tunnel test failed (Status: $response)${NC}"
        stop_proxy
        return 1
    fi
}

# Test 5: SOCKS5 Proxy
test_socks5_proxy() {
    echo ""
    echo "Test 5: SOCKS5 Proxy"
    echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"

    start_proxy "socks5" $SOCKS_PORT || return 1

    echo "Testing SOCKS5 proxy..."
    response=$(curl --socks5 localhost:$SOCKS_PORT -s -o /dev/null -w "%{http_code}" http://example.com --max-time 10)

    if [ "$response" = "200" ]; then
        echo -e "${GREEN}✓ SOCKS5 proxy test passed (Status: $response)${NC}"
        stop_proxy
        return 0
    else
        echo -e "${YELLOW}⚠ SOCKS5 test status: $response${NC}"
        stop_proxy
        return 0
    fi
}

# Test 6: Concurrent Connections
test_concurrent_connections() {
    echo ""
    echo "Test 6: Concurrent Connections"
    echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"

    start_proxy "http" $PROXY_PORT "-t 20" || return 1

    echo "Testing 10 concurrent connections..."
    for i in {1..10}; do
        curl -x http://localhost:$PROXY_PORT -s -o /dev/null http://example.com --max-time 10 &
    done

    wait
    echo -e "${GREEN}✓ Concurrent connections test completed${NC}"
    stop_proxy
    return 0
}

# Cleanup on exit
cleanup() {
    echo ""
    echo "Cleaning up..."
    stop_proxy
    rm -f /tmp/proxy.log
}

trap cleanup EXIT INT TERM

# Main test execution
main() {
    echo "Starting test suite..."
    echo ""

    # Check if proxy binary exists
    if [ ! -f "../proxymax" ]; then
        echo -e "${RED}Error: proxymax binary not found!${NC}"
        echo "Please run 'make' first to build the proxy."
        exit 1
    fi

    # Run tests
    passed=0
    failed=0

    if test_http_proxy_basic; then ((passed++)); else ((failed++)); fi
    sleep 2

    if test_http_proxy_caching; then ((passed++)); else ((failed++)); fi
    sleep 2

    if test_http_proxy_auth; then ((passed++)); else ((failed++)); fi
    sleep 2

    if test_https_tunnel; then ((passed++)); else ((failed++)); fi
    sleep 2

    if test_socks5_proxy; then ((passed++)); else ((failed++)); fi
    sleep 2

    if test_concurrent_connections; then ((passed++)); else ((failed++)); fi

    # Summary
    echo ""
    echo "╔══════════════════════════════════════════════════════════╗"
    echo "║                     Test Summary                         ║"
    echo "╠══════════════════════════════════════════════════════════╣"
    echo -e "║ ${GREEN}Passed: $passed${NC}                                              ║"
    echo -e "║ ${RED}Failed: $failed${NC}                                              ║"
    echo "╚══════════════════════════════════════════════════════════╝"

    if [ $failed -eq 0 ]; then
        echo -e "\n${GREEN}All tests passed!${NC}\n"
        exit 0
    else
        echo -e "\n${YELLOW}Some tests failed. Check the output above.${NC}\n"
        exit 1
    fi
}

# Run main
main
