# 🚨 Syscall Error Conditions — 42 IRC Server

## When and Why Each Syscall Can Fail

---

> **Purpose:** Every syscall can fail. Knowing *when* and *why* is the difference between a server that crashes mysteriously and one that handles errors gracefully. This document covers every error each syscall in your IRC server can return, with the **most likely causes for your project** highlighted.

---

## Table of Contents

1. [`socket()`](#1-socket)
2. [`setsockopt()`](#2-setsockopt)
3. [`fcntl()`](#3-fcntl)
4. [`bind()`](#4-bind)
5. [`listen()`](#5-listen)
6. [`poll()`](#6-poll)
7. [`accept()`](#7-accept)
8. [`recv()`](#8-recv)
9. [`send()`](#9-send)
10. [`close()`](#10-close)
11. [`signal()`](#11-signal)

---

<a name="1-socket"></a>
## 1. `socket()`

### Your code
```cpp
server_fd = socket(AF_INET, SOCK_STREAM, 0);
```

### Error table

| errno | Meaning | When it happens | In your IRC server? |
|-------|---------|-----------------|:-------------------:|
| **EACCES** | Permission denied | Process has no permission to create socket (uncommon on Linux for AF_INET) | ❌ Rare |
| **EAFNOSUPPORT** | Address family not supported | AF_INET not compiled into kernel | ❌ Extremely rare |
| **EINVAL** | Invalid argument | Unknown protocol, invalid flags | ❌ Only if you pass wrong args |
| **EMFILE** | Process fd limit reached | Process has too many open fds (ulimit -n) | ⚠️ Possible with many clients |
| **ENFILE** | System-wide fd limit reached | System ran out of file structures | ❌ Very rare |
| **ENOBUFS / ENOMEM** | Out of memory | Kernel couldn't allocate socket structures | ❌ Rare (system under pressure) |
| **EPROTONOSUPPORT** | Protocol not supported | SOCK_STREAM not available for AF_INET | ❌ Extremely rare |

### ⚠️ Most relevant to your IRC server

```cpp
if (server_fd == -1) {
    // Most likely: EMFILE (too many clients connected)
    // Your server should handle this gracefully
    std::cout << "socket failed\n";
    return;
}
```

---

<a name="2-setsockopt"></a>
## 2. `setsockopt()`

### Your code
```cpp
int opt = 1;
setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
```

### Error table

| errno | Meaning | When it happens | In your IRC server? |
|-------|---------|-----------------|:-------------------:|
| **EBADF** | Bad file descriptor | `server_fd` is not a valid fd | ⚠️ Possible if `socket()` failed |
| **EFAULT** | Bad address | `&opt` points to invalid memory | ❌ Won't happen (stack variable) |
| **EINVAL** | Invalid argument | `optlen` doesn't match option type | ❌ Fixed at compile time |
| **ENOPROTOOPT** | Unknown option | Option doesn't exist at given level | ❌ SO_REUSEADDR is standard |
| **ENOTSOCK** | Not a socket | `server_fd` is a regular file, not a socket | ⚠️ Possible if fd was closed/reused |
| **EDOM** | Domain error | Option value is out of range (very rare) | ❌ |

### ⚠️ Most relevant to your IRC server

```cpp
if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
    // Most likely: EBADF if socket() already failed
    // Without this, you might get EADDRINUSE on next restart!
    std::cout << "setsockopt failed\n";
    close(server_fd);
    return;
}
```

---

<a name="3-fcntl"></a>
## 3. `fcntl()`

### Your code
```cpp
fcntl(server_fd, F_SETFL, O_NONBLOCK);
fcntl(client_fd, F_SETFL, O_NONBLOCK);  // for each new client
```

### Error table

| errno | Meaning | When it happens | In your IRC server? |
|-------|---------|-----------------|:-------------------:|
| **EBADF** | Bad file descriptor | fd is not valid | ⚠️ If previous socket/bind/accept failed |
| **EACCES** | Permission denied | Trying to set incompatible flags on fd type | ❌ Rare |
| **EINVAL** | Invalid argument | Invalid command or flags | ❌ F_SETFL and O_NONBLOCK are standard |
| **EDEADLK** | Deadlock | (F_SETLKW only — not relevant) | ❌ |
| **EOVERFLOW** | Value too large | (F_GETOWN only) | ❌ |

### ⚠️ Most relevant to your IRC server

```cpp
if (fcntl(server_fd, F_SETFL, O_NONBLOCK) == -1) {
    // Most likely: EBADF (socket() or accept() failed earlier)
    // Your server cannot work without this!
    std::cout << "fcntl failed\n";
    close(server_fd);
    return;
}
```

**Why this matters:** If `fcntl(O_NONBLOCK)` fails on a client socket, your server will **block** on that client's `recv()`/`send()` and freeze the entire event loop.

---

<a name="4-bind"></a>
## 4. `bind()`

### Your code
```cpp
bind(server_fd, (struct sockaddr *)&addr, sizeof(addr));
```

### Error table

| errno | Meaning | When it happens | In your IRC server? |
|-------|---------|-----------------|:-------------------:|
| **EACCES** | Permission denied | Port < 1024 requires root, or address is protected | ✅ If you try port < 1024 |
| **EADDRINUSE** | Address already in use | Port is held by another socket (often in TIME_WAIT) | ✅ **Common on restart without SO_REUSEADDR** |
| **EBADF** | Bad file descriptor | `server_fd` is invalid | ⚠️ If `socket()` failed |
| **EFAULT** | Bad address | `&addr` points to invalid memory | ❌ Stack variable |
| **EINVAL** | Invalid argument | Socket already bound, or addr_len wrong | ❌ Won't happen |
| **ENOTSOCK** | Not a socket | `server_fd` is not a socket | ⚠️ Rare |
| **EOPNOTSUPP** | Operation not supported | Wrong socket type for bind | ❌ SOCK_STREAM supports bind |
| **EADDRNOTAVAIL** | Address not available | INADDR_ANY on machine with no network (unlikely) | ❌ Rare |

### ⚠️ Most relevant to your IRC server

```cpp
if (bind(server_fd, (struct sockaddr *)&addr, sizeof(addr)) == -1) {
    // Most likely: EADDRINUSE (another server is running on this port)
    //              or EACCES (port < 1024 without root)
    // Only happens once at startup
    std::cout << "binding failed\n";
    close(server_fd);
    return;
}
```

**Without SO_REUSEADDR:** Crash → restart immediately → `EADDRINUSE` for ~60s (TIME_WAIT).

---

<a name="5-listen"></a>
## 5. `listen()`

### Your code
```cpp
listen(server_fd, 10);
```

### Error table

| errno | Meaning | When it happens | In your IRC server? |
|-------|---------|-----------------|:-------------------:|
| **EBADF** | Bad file descriptor | `server_fd` is invalid | ⚠️ If `bind()` failed |
| **EDESTADDRREQ** | Destination address required | Socket not bound | ⚠️ If `bind()` was skipped or failed |
| **EINVAL** | Invalid argument | Socket already connected or listening, or backlog negative | ❌ Won't happen |
| **ENOTSOCK** | Not a socket | `server_fd` is not a socket | ⚠️ Rare |
| **EOPNOTSUPP** | Operation not supported | Socket type doesn't support listen (e.g., UDP) | ❌ SOCK_STREAM supports it |

### ⚠️ Most relevant to your IRC server

```cpp
if (listen(server_fd, 10) == -1) {
    // Most likely: flags if bind() failed (server_fd is -1)
    // or EINVAL on very old kernels
    std::cout << "listening failed\n";
    close(server_fd);
    return;
}
```

---

<a name="6-poll"></a>
## 6. `poll()`

### Your code
```cpp
int ret = poll(fds.data(), fds.size(), -1);
```

### Error table

| errno | Meaning | When it happens | In your IRC server? |
|-------|---------|-----------------|:-------------------:|
| **EBADF** | Bad file descriptor | One of the fds in the array is invalid | ✅ **Can happen if a client fd was closed but still in array** |
| **EFAULT** | Bad address | `fds` array points to invalid memory | ❌ `fds.data()` is valid |
| **EINTR** | Interrupted by signal | A signal was caught while waiting | ✅ **Will happen on Ctrl+C (SIGINT)** |
| **EINVAL** | Invalid argument | `nfds > RLIMIT_NOFILE` | ❌ Won't happen |
| **ENOMEM** | Out of memory | Kernel couldn't allocate internal poll tables | ❌ Very rare |

### ⚠️ Most relevant to your IRC server

```cpp
if (poll(fds.data(), fds.size(), -1) < 0) {
    // EBADF: You have a stale fd in your array!
    //   → You removed a client from clients[] but forgot to remove from fds[]
    //
    // EINTR: Normal! Ctrl+C triggers SIGINT → poll() returns -1/EINTR
    //   → Check g_running flag, if 0, break out of the loop
    //
    // Your server MUST handle both of these correctly
    if (errno == EINTR) {
        // This is fine — signal was caught, continue
        continue;
    }
    // EBADF means a bug in your fd tracking
    std::cout << "poll failed\n";
    break;
}
```

**Critical:** Most 42 IRC projects forget to handle `EBADF` properly. If you `close(fd)` but don't remove it from your `fds` array, `poll()` will return `EBADF` every time until you fix it.

---

<a name="7-accept"></a>
## 7. `accept()`

### Your code
```cpp
int client_fd = accept(server_fd, (struct sockaddr*)&addr, &len);
```

### Error table

| errno | Meaning | When it happens | In your IRC server? |
|-------|---------|-----------------|:-------------------:|
| **EAGAIN / EWOULDBLOCK** | No connections pending | Non-blocking socket + no connections in accept queue | ✅ **Normal with non-blocking — same value** |
| **EBADF** | Bad fd | `server_fd` is invalid | ⚠️ Rare |
| **ECONNABORTED** | Connection aborted | Client sent RST before accept | ⚠️ Can happen under heavy load |
| **EFAULT** | Bad address | `&addr` points to invalid memory | ❌ |
| **EINTR** | Interrupted by signal | Signal caught while blocked in accept | ⚠️ But your server is non-blocking + poll-based |
| **EINVAL** | Invalid argument | Socket not listening, or invalid addrlen | ✅ **If you call accept on the wrong fd** |
| **EMFILE** | Process fd limit | Too many open files (ulimit) | ✅ **Possible with many clients** |
| **ENFILE** | System fd limit | System ran out of file structures | ❌ Rare |
| **ENOTSOCK** | Not a socket | `server_fd` is not a socket | ❌ |
| **EOPNOTSUPP** | Operation not supported | Socket type doesn't support accept | ❌ |
| **EPROTO** | Protocol error | Some protocol error occurred | ❌ Rare |

### ⚠️ Most relevant to your IRC server

```cpp
int client_fd = accept(server_fd, (struct sockaddr*)&addr, &len);
if (client_fd < 0) {
    // Most common: ECONNABORTED — client disconnected before you called accept
    //              EMFILE — too many clients (ulimit -n)
    //              EAGAIN — should NOT happen here because poll() said POLLIN
    
    if (errno == EMFILE || errno == ENFILE) {
        // Too many connections — consider dropping oldest client
        // or just skipping this accept
    }
    std::cout << "accept failed\n";
    return;
}
```

**Common bug:** Forgetting to set `len = sizeof(addr)` before each call to `accept()`. If `len` is garbage, you might get `EINVAL`.

---

<a name="8-recv"></a>
## 8. `recv()`

### Your code
```cpp
ssize_t bytes = recv(client_fd, buff, sizeof(buff) - 1, 0);
```

### Error table

| errno | Meaning | When it happens | In your IRC server? |
|-------|---------|-----------------|:-------------------:|
| **EAGAIN / EWOULDBLOCK** | No data available | Non-blocking socket + receive queue empty | ✅ **Normal — means no data right now** |
| **EBADF** | Bad fd | `client_fd` is invalid | ⚠️ If client was already removed |
| **ECONNREFUSED** | Connection refused | Remote peer refused connection | ❌ Only for connect() |
| **EFAULT** | Bad address | `buff` points to invalid memory | ❌ Stack buffer |
| **EINTR** | Interrupted | Signal caught while blocked | ⚠️ Your socket is non-blocking, so won't happen |
| **EINVAL** | Invalid argument | Invalid flags | ❌ |
| **ENOMEM** | No memory | Kernel couldn't allocate | ❌ Rare |
| **ENOTCONN** | Not connected | Socket not connected | ⚠️ If you use wrong fd |
| **ENOTSOCK** | Not a socket | `client_fd` is not a socket | ❌ |
| **ECONNRESET** | Connection reset | Peer sent RST (crashed/force-closed) | ✅ **Common when client disconnects abruptly** |
| **ETIMEDOUT** | Connection timed out | Keepalive detected dead connection | ⚠️ If your server runs very long idle |
| **EPIPE** | Broken pipe | Socket shutdown for writing | ⚠️ Rare for recv |

### ⚠️ Most relevant to your IRC server

```cpp
ssize_t bytes = recv(client_fd, buff, sizeof(buff) - 1, 0);

if (bytes == -1) {
    if (errno == EAGAIN || errno == EWOULDBLOCK) {
        // Normal! No data right now with non-blocking.
        // Should NOT happen if poll() just returned POLLIN,
        // but can happen if another thread/event stole the data
        return;  // nothing to do
    }
    if (errno == ECONNRESET || errno == ETIMEDOUT) {
        // Client disconnected abruptly — remove them
        remove_client(client_fd);
        return;
    }
    // Any other error — remove client to be safe
    remove_client(client_fd);
    return;
}

if (bytes == 0) {
    // ⭐ Client disconnected gracefully (FIN received)
    // This is the NORMAL way a client leaves!
    remove_client(client_fd);
    return;
}
```

**Critical distinction:**
- `recv() == 0` → client **gracefully** closed (FIN)
- `recv() == -1, errno == ECONNRESET` → client **abruptly** disconnected (RST)
- `recv() == -1, errno == EAGAIN` → no data right now (**don't** remove client!)

---

<a name="9-send"></a>
## 9. `send()`

### Your code
```cpp
send(client_fd, msg.c_str(), msg.size(), 0);
```

### Error table

| errno | Meaning | When it happens | In your IRC server? |
|-------|---------|-----------------|:-------------------:|
| **EAGAIN / EWOULDBLOCK** | Send buffer full | Non-blocking + kernel send buffer is full | ✅ **Common with slow clients, same value** |
| **EBADF** | Bad fd | `client_fd` is invalid | ⚠️ If client was already removed |
| **ECONNRESET** | Connection reset | Peer sent RST (crashed/disconnected) | ✅ **Common — client disconnected** |
| **EDESTADDRREQ** | Address required | Socket not connected and no dest address | ❌ Connected socket |
| **EFAULT** | Bad address | `msg.c_str()` points to invalid memory | ❌ |
| **EINTR** | Interrupted | Signal caught | ⚠️ Your socket is non-blocking |
| **EINVAL** | Invalid argument | Invalid flags | ❌ |
| **EISCONN** | Already connected | Wrong flags on connected socket | ❌ |
| **EMSGSIZE** | Message too large | Message exceeds allowed size | ❌ IRC messages are tiny |
| **ENOBUFS** | No buffer space | Network subsystem out of buffers | ⚠️ Rare under heavy load |
| **ENOMEM** | No memory | Kernel out of memory | ❌ |
| **ENOTCONN** | Not connected | Socket not connected | ⚠️ If you send after close |
| **ENOTSOCK** | Not a socket | Not a socket fd | ❌ |
| **EPIPE** | Broken pipe | Connection broken + SIGPIPE not ignored | ✅ **Your server ignores SIGPIPE, so just EPIPE** |

### ⚠️ Most relevant to your IRC server

```cpp
bool Server::send_to_client(int fd, std::string msg) {
    msg += "\r\n";
    if (send(fd, msg.c_str(), msg.size(), 0) == -1) {
        // ECONNRESET → client crashed, remove them
        // EPIPE → connection broken, remove them
        // EAGAIN → send buffer full (rare for IRC, but possible)
        
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            // Buffer is full — you'd need to queue the message
            // and send it later when POLLOUT fires
            // (Most 42 projects don't implement this and just drop)
        }
        
        // For ECONNRESET, EPIPE, etc. — client is dead
        return false;  // caller will remove_client()
    }
    return true;
}
```

**Critical:** Without `signal(SIGPIPE, SIG_IGN)`, `send()` to a dead client would **kill your entire server process** with SIGPIPE before `send()` even returns `-1`. This is why you have it in `main()`.

**Nagle's behavior:** With Nagle's algorithm, `send()` might succeed (data buffered) but the actual TCP segment won't be sent until enough data accumulates or the receiver ACKs. This is fine for IRC.

---

<a name="10-close"></a>
## 10. `close()`

### Your code
```cpp
close(client_fd);    // when client disconnects
close(server_fd);    // on server shutdown
```

### Error table

| errno | Meaning | When it happens | In your IRC server? |
|-------|---------|-----------------|:-------------------:|
| **EBADF** | Bad fd | fd is not an open file descriptor | ✅ **Can happen if you `close()` twice** |
| **EINTR** | Interrupted | Signal caught while closing (rare) | ⚠️ Very rare |
| **EIO** | I/O error | I/O error during close | ❌ Rare on sockets |

### ⚠️ Most relevant to your IRC server

```cpp
void Server::remove_client(int fd) {
    // Remove from clients vector...
    // Remove from fds vector...
    
    if (close(fd) == -1 && errno == EBADF) {
        // This means fd was already closed!
        // Either you called close() twice on the same fd,
        // or the fd was never valid.
        // This is a BUG in your bookkeeping.
        std::cout << "double close detected!\n";
    }
}
```

**Common bug:** Calling `close()` but forgetting to remove the fd from your poll array, then `poll()` fails with `EBADF` because the fd is now invalid.

**Server shutdown:** In your `~Server()` destructor, you call `close()` on every `fds[i].fd`. The listening socket is in that array too — that's correct.

---

<a name="11-signal"></a>
## 11. `signal()` / `sigaction()`

### Your code
```cpp
signal(SIGINT,  Server::signal_handler);    // Ctrl+C
signal(SIGQUIT, Server::signal_handler);    // Ctrl+\
signal(SIGPIPE, SIG_IGN);                   // Prevent crash on dead client
```

### Error table

| errno | Meaning | When it happens | In your IRC server? |
|-------|---------|-----------------|:-------------------:|
| **EINVAL** | Invalid signal | Signal number is out of range (1-31, 32-64) | ❌ SIGINT=2, SIGQUIT=3, SIGPIPE=13 |
| **EFAULT** | Bad address | Handler struct points to invalid memory | ❌ |
| **EPERM** | Permission denied | Trying to catch/ignore SIGKILL or SIGSTOP | ✅ **If you accidentally try to catch SIGKILL (9)** |
| **EINVAL (sigaction)** | Invalid flags | Bad sa_flags | ❌ |

### ⚠️ Most relevant to your IRC server

```cpp
signal(SIGINT,  Server::signal_handler);    // OK
signal(SIGQUIT, Server::signal_handler);    // OK
signal(SIGPIPE, SIG_IGN);                   // OK

// WRONG:
// signal(SIGKILL, handler);  → EPERM, always fails
// signal(999, handler);      → EINVAL
```

**The critical one for IRC:**
- `SIGPIPE` without `SIG_IGN` → client disconnects → server tries to `send()` → **server process dies instantly**
- `SIGPIPE` with `SIG_IGN` → client disconnects → server tries to `send()` → returns `-1` with `errno = EPIPE` → server lives

---

## Quick Reference: Most Likely Errors Your IRC Server Will Hit

| # | Syscall | Most likely error | What it means |
|---|---------|------------------|---------------|
| 1 | `socket()` | `EMFILE` | Too many clients (hit ulimit) |
| 2 | `setsockopt()` | `EBADF` | socket() failed earlier |
| 3 | `fcntl()` | `EBADF` | socket() or accept() failed earlier |
| 4 | `bind()` | `EADDRINUSE` | Port already taken (another server running) |
| 5 | `listen()` | `EINVAL` | bind() not called or failed |
| 6 | `poll()` | `EINTR` | Ctrl+C / SIGINT caught (normal!) |
| 6 | `poll()` | `EBADF` | **Bug:** closed an fd but didn't remove from poll array |
| 7 | `accept()` | `ECONNABORTED` | Client disconnected before you called accept |
| 7 | `accept()` | `EMFILE` | Too many clients (ulimit) |
| 8 | `recv()` | **0 (not error)** | Client gracefully disconnected (FIN) |
| 8 | `recv()` | `ECONNRESET` | Client crashed (RST) |
| 8 | `recv()` | `EAGAIN` | No data right now (normal with non-blocking) |
| 9 | `send()` | `EPIPE` | Client already disconnected (without SIG_IGN → process dies!) |
| 9 | `send()` | `ECONNRESET` | Client crashed (RST received) |
| 9 | `send()` | `EAGAIN` | Send buffer full (slow consumer) |
| 10 | `close()` | `EBADF` | **Bug:** double close, or fd was already invalid |
| 11 | `signal()` | `EPERM` | Trying to catch SIGKILL or SIGSTOP |

---

## Error Handling Checklist for Your IRC Server

```cpp
// ✅ socket() fails? → print error, exit
// ✅ setsockopt() fails? → close socket, exit
// ✅ fcntl() fails? → close socket, exit
// ✅ bind() fails? → close socket, exit
// ✅ listen() fails? → close socket, exit
// ✅ poll() returns -1/EINTR? → check g_running, continue
// ✅ poll() returns -1/EBADF? → bug in your fd tracking, fix it!
// ✅ accept() fails? → skip, continue (don't crash)
// ✅ recv() returns 0 → client disconnected, remove
// ✅ recv() returns -1/ECONNRESET → client crashed, remove
// ✅ recv() returns -1/EAGAIN → skip (normal with non-blocking)
// ✅ send() returns -1/EPIPE → client dead, remove
// ✅ send() returns -1/EAGAIN → buffer full (optionally queue message)
// ✅ close() returns -1 → log it (but client is already gone)
// ✅ signal() returns SIG_ERR → log it (but rare)
```

---

> **Golden rule:** Every syscall can fail. If you don't check the return value, your server **will** crash at the worst possible moment. If you **do** check it, your server will gracefully handle a crashed client, a full buffer, or a port conflict.
