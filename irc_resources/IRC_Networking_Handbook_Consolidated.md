# IRC Networking Handbook — Consolidated Edition

> A unified reference for building an IRC server from the socket up.
> Organized by topic rather than by source file.

---

## Table of Contents

1. [What is a Socket?](#1-what-is-a-socket)
2. [Why Sockets Exist (Process Isolation)](#2-why-sockets-exist-process-isolation)
3. [Sockets vs Pipes](#3-sockets-vs-pipes)
4. [The Kernel Architecture](#4-the-kernel-architecture)
5. [Socket Creation: `socket()`](#5-socket-creation-socket)
6. [Socket Configuration: `setsockopt()`](#6-socket-configuration-setsockopt)
7. [Non-Blocking Mode: `fcntl()` & `O_NONBLOCK`](#7-non-blocking-mode-fcntl--o_nonblock)
8. [The Server Lifecycle](#8-the-server-lifecycle)
   - [bind()](#81-bind)
   - [listen()](#82-listen)
   - [accept() & The TCP Handshake](#83-accept--the-tcp-handshake)
9. [Data Transfer: `send()` & `recv()`](#9-data-transfer-send--recv)
   - [The Full `send()` Path](#91-the-full-send-path)
   - [The Full `recv()` Path](#92-the-full-recv-path)
   - [Send & Receive Buffers](#93-send--receive-buffers)
   - [Blocking vs Non-Blocking Behavior](#94-blocking-vs-non-blocking-behavior)
10. [I/O Multiplexing: `poll()`](#10-io-multiplexing-poll)
    - [Why `poll()`?](#101-why-poll)
    - [Kernel Internals of `poll()`](#102-kernel-internals-of-poll)
    - [Performance & Limitations](#103-performance--limitations)
    - [Alternatives: `select()` & `epoll()`](#104-alternatives-select--epoll)
11. [Signals & Graceful Shutdown](#11-signals--graceful-shutdown)
12. [Ports & Addresses](#12-ports--addresses)
13. [Error Handling Reference](#13-error-handling-reference)
14. [Common Misconceptions](#14-common-misconceptions)
15. [The IRC Protocol](#15-the-irc-protocol)
    - [What is IRC?](#151-what-is-irc)
    - [IRC Architecture](#152-irc-architecture)
    - [RFC 1459 vs RFC 2812](#153-rfc-1459-vs-rfc-2812)
    - [Key IRC Commands](#154-key-irc-commands)
    - [IRC Numeric Replies](#155-irc-numeric-replies)
    - [Netcat for Testing](#156-netcat-for-testing)
16. [Glossary](#16-glossary)

---

# 1. What is a Socket?

A **socket** is an **operating-system-managed communication endpoint** that allows two processes to exchange data. It is **not** the network connection itself — it is one end of a communication channel.

```text
Process A                Process B
    │                        │
 socket                    socket
    │                        │
    └────────┬───────────────┘
             │
       Kernel / Network
```

Sockets come in different **address families** that determine how communication happens:

| Address Family | Communication Type            | Created With                    |
| -------------- | ----------------------------- | ------------------------------- |
| `AF_INET`      | IPv4 networking (IRC server)  | `socket(AF_INET, ...)`          |
| `AF_INET6`     | IPv6 networking               | `socket(AF_INET6, ...)`         |
| `AF_UNIX`      | Local inter-process comms     | `socket(AF_UNIX, ...)`          |

**Key insight:** The same API (`socket()`, `bind()`, `listen()`, `accept()`, `connect()`, `send()`, `recv()`, `close()`) works for **all address families**. Only the address format changes.

---

# 2. Why Sockets Exist (Process Isolation)

Modern operating systems isolate each process in its own protected memory space. Process A cannot directly read or write Process B's memory.

```text
+-------------------+
| Process A         |     ✗ Direct access
| Memory            | ──► is not allowed
+-------------------+

+-------------------+
| Process B         |
| Memory            |
+-------------------+
```

Sockets provide a **safe, OS-managed channel** for these isolated processes to exchange data.

```text
Process A                Process B
    │                        │
 send(data)               recv(data)
    │                        │
    ▼                        ▲
┌──────────┐           ┌──────────┐
│ Socket A │ ───► Kernel ◄─── │ Socket B │
└──────────┘           └──────────┘
```

The operating system handles all the mechanics of getting data from one process to the other — whether they're on the same machine or across the internet.

---

# 3. Sockets vs Pipes

Both pipes and sockets are **inter-process communication (IPC)** mechanisms, but they have different design goals.

| Feature                     | Pipe                          | Unix Domain Socket              | Network Socket (`AF_INET`)        |
| --------------------------- | ----------------------------- | ------------------------------- | --------------------------------- |
| Process relationship        | Usually related (parent/child)| Any processes                   | Any processes on any machine      |
| Named (addressable)         | Anonymous by default          | Filesystem path (`/tmp/sock`)   | IP:Port                           |
| Client/Server model         | No                            | Yes (`listen`/`accept`)         | Yes                               |
| Direction                   | Usually one-way               | Full-duplex                     | Full-duplex                       |
| Can accept new connections  | No                            | Yes                             | Yes                               |
| Network capable             | No (local only)               | No (local only)                 | Yes                               |

Use **pipes** for simple `fork()`-based communication (e.g., `ls | grep txt`).  
Use **sockets** when you need a client/server model, regardless of whether the processes are local or remote.

---

# 4. The Kernel Architecture

When you call a socket function like `send()`, your request travels through **multiple kernel subsystems**. Each subsystem has one job:

```text
Userspace (your IRC server)
    │
    ▼
────────────────────────────────────────────
KERNEL SUBSYSTEMS
────────────────────────────────────────────
    │
    ▼
1. VFS (Virtual File System)
   └── Translates fd → struct file

2. Socket Subsystem (BSD API)
   └── Routes to the right protocol (TCP/UDP)

3. Networking Core (sock layer)
   └── Manages connection state & buffers

4. TCP Subsystem
   └── Sequence numbers, retransmission, flow control

5. IP Subsystem
   └── Routing, addressing, fragmentation

6. Traffic Control (qdisc)
   └── Queues & schedules packets for transmission

7. Device Driver Subsystem
   └── Converts packets into hardware frames

8. NIC (Hardware)
   └── Transmits bits on the wire
```

## Key Structures (Simplified)

| Subsystem       | Primary Structure | What It Holds                           |
| --------------- | ----------------- | --------------------------------------- |
| VFS             | `struct file`     | fd flags (`O_NONBLOCK`), pointer to socket |
| Socket          | `struct socket`   | Protocol type, pointer to `struct sock` |
| Networking core | `struct sock`     | Connection state, IPs, ports, buffers   |
| TCP             | `struct tcp_sock` | Sequence numbers, windows, RTT          |
| IP              | `sk_buff`         | Packet container                        |
| Driver          | `net_device`      | Hardware interface                      |

**Mental model:** `fd → VFS → socket → sock → tcp → IP → driver → NIC`

---

# 5. Socket Creation: `socket()`

```cpp
int fd = socket(AF_INET, SOCK_STREAM, 0);
```

## The Arguments

| Argument   | Value             | Meaning                        |
| ---------- | ----------------- | ------------------------------ |
| `domain`   | `AF_INET`         | Use IPv4 addresses             |
| `type`     | `SOCK_STREAM`     | Reliable byte stream (TCP)     |
| `protocol` | `0` (or `IPPROTO_TCP`) | "Choose default" → TCP   |

`protocol = 0` means "choose the default protocol for this type." For `AF_INET + SOCK_STREAM`, the default is TCP, making these equivalent:

```cpp
socket(AF_INET, SOCK_STREAM, 0);
socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
```

## What Happens Inside the Kernel

- A kernel socket object is allocated.
- It stores: family (`AF_INET`), type (`SOCK_STREAM`), protocol (`TCP`), state (`CREATED`).
- The kernel returns a **file descriptor** — an integer index into your process's FD table.
- **The FD is not the socket.** It's a handle that points to the kernel object.

```text
Your FD Table
    3 ───► Kernel Socket Object
               Family  = AF_INET
               Type    = SOCK_STREAM
               Protocol = TCP
               State   = CREATED
```

---

# 6. Socket Configuration: `setsockopt()`

```cpp
int setsockopt(int fd, int level, int option, const void *value, socklen_t size);
```

The `level` argument tells the kernel **which subsystem** owns the option:

## `SOL_SOCKET` — Socket Subsystem Options

| Option           | Purpose                                             |
| ---------------- | --------------------------------------------------- |
| `SO_REUSEADDR`   | Allow reuse of local address during `bind()`        |
| `SO_KEEPALIVE`   | Enable TCP keepalive probes                         |
| `SO_RCVBUF`      | Set receive buffer size                             |
| `SO_SNDBUF`      | Set send buffer size                                |
| `SO_LINGER`      | Control behavior of `close()` with pending data     |

```cpp
int opt = 1;
setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
```

## `IPPROTO_TCP` — TCP Subsystem Options

| Option         | Purpose                                |
| -------------- | -------------------------------------- |
| `TCP_NODELAY`  | Disable Nagle's algorithm              |

## `IPPROTO_IP` — IP Subsystem Options

| Option   | Purpose           |
| -------- | ----------------- |
| `IP_TTL` | Set packet TTL    |

**Key point:** `setsockopt()` is purely local — it changes kernel socket state. It never sends packets over the network.

---

# 7. Non-Blocking Mode: `fcntl()` & `O_NONBLOCK`

```cpp
fcntl(fd, F_SETFL, O_NONBLOCK);
```

## Where This Lives

`fcntl()` operates on the **file descriptor**, not the socket protocol. It modifies `f_flags` inside the kernel's `struct file`.

```text
fd = 3
    │
    ▼
struct file
    ├── f_flags ←── O_NONBLOCK stored HERE
    ├── f_op     (function table: read/write/etc.)
    └── private_data ───► struct socket ───► struct sock (TCP state)
```

## What Changes

| Operation | Blocking Socket              | Non-Blocking Socket                         |
| --------- | ---------------------------- | ------------------------------------------- |
| `accept()`| Wait until client connects   | Return `-1` with `errno = EAGAIN`           |
| `recv()`  | Wait until data arrives      | Return `-1` with `errno = EAGAIN`           |
| `send()`  | Wait until buffer has space  | Return `-1` with `errno = EAGAIN`           |

## Why Non-Blocking Is Essential in an IRC Server

An IRC server uses a single event loop:

```cpp
while (running)
{
    poll(fds, nfds, timeout);
    // handle ready sockets
}
```

**The philosophy:** `poll()` is the only function allowed to wait. `accept()`, `recv()`, and `send()` should **never** block. If they can't complete immediately, they return `EAGAIN` so the server can continue handling other clients.

> **Without non-blocking:** One slow client with a full send buffer would freeze the entire server.
> **With non-blocking:** That client gets `EAGAIN`, and the server moves on.

**Important:** `O_NONBLOCK` does **not** affect `poll()`. `poll()` has its own blocking behavior controlled by its `timeout` parameter. This is because `poll()` doesn't perform I/O — it asks "which FDs are ready?" — so it doesn't check `O_NONBLOCK`.

```cpp
poll(fds, nfds, -1);   // blocks until at least one FD is ready
poll(fds, nfds, 0);     // check and return immediately (non-blocking)
poll(fds, nfds, 5000);  // wait up to 5 seconds
```

---

# 8. The Server Lifecycle

The classic TCP server sequence:

```
socket()  ──► bind()  ──► listen()  ──► accept()  ──► send()/recv()
```

## 8.1 `bind()`

```cpp
int bind(int sockfd, const struct sockaddr *addr, socklen_t addrlen);
```

### Purpose

`bind()` associates a socket with a specific IP address and port. Without it, the kernel has no way to deliver incoming packets to your socket.

### The Address Structure

```cpp
sockaddr_in addr;

addr.sin_family      = AF_INET;          // IPv4
addr.sin_port        = htons(6667);      // Port (network byte order!)
addr.sin_addr.s_addr = INADDR_ANY;       // Listen on ALL interfaces
```

**Why `htons()`?** Different CPU architectures store bytes in different orders (endianness). Network byte order is big-endian. `htons()` ("host to network short") ensures the port number is correct regardless of your CPU.

**`INADDR_ANY`** means "listen on every network interface." If your machine has IPs `127.0.0.1`, `192.168.1.15`, and `10.0.0.5`, using `INADDR_ANY` accepts connections on all of them.

### What Happens Inside the Kernel

The kernel records which socket owns which address/port:

```text
Port      Socket
────────────────
80     ───► socket A
443    ───► socket B
6667   ───► server_fd
```

When a TCP packet arrives for port 6667, the kernel delivers it to `server_fd`.

### `EADDRINUSE`

If another program already uses port 6667, `bind()` returns `-1` with `errno = EADDRINUSE`. This is why `SO_REUSEADDR` is important — it allows binding to a recently-used port during restart.

### `bind()` Is Purely Local

`bind()` does **not**:
- Send packets
- Contact another computer
- Establish a connection

---

## 8.2 `listen()`

```cpp
int listen(int sockfd, int backlog);
```

### Purpose

`listen()` tells the kernel: **"This socket is a server socket. Start accepting incoming TCP connection requests."**

The `backlog` argument is a hint for how many completed connections can wait in the accept queue before your program calls `accept()`.

### What Changes in the Kernel

Before `listen()`:
```
Socket
    State = CLOSED (or BOUND)
```

After `listen()`:
```
Socket
    State = LISTEN
```

The TCP subsystem now knows: "Incoming connection requests for this port should be accepted."

### What `listen()` Does NOT Do

- It does **not** block waiting for clients.
- It does **not** return client sockets.
- It does **not** perform the TCP handshake.

```cpp
socket();
bind();
listen();  // returns immediately

// Your server is now ready.
// The kernel waits for clients in the background.
```

---

## 8.3 `accept()` & The TCP Handshake

```cpp
int accept(int sockfd, struct sockaddr *addr, socklen_t *addrlen);
```

### The Critical Insight

**Your server does NOT perform the TCP handshake. The kernel does it automatically.**

By the time `accept()` returns, the TCP connection is **already fully established**.

### The Full Handshake Flow

```text
Client                          Server
  │                               │
  │  ──── SYN ──────────────────► │
  │                               │  Kernel receives SYN
  │                               │  Finds listening socket for port 6667
  │                               │  Creates internal TCP connection object
  │                               │  Tracks sequence numbers
  │  ◄─── SYN+ACK ─────────────── │  Kernel sends SYN+ACK
  │                               │
  │  ──── ACK ──────────────────► │  Kernel receives ACK
  │                               │
  │                               │  State = ESTABLISHED
  │                               │  Places connection into accept queue
  │                               │
  │         Later...              │
  │                               │
  │          accept() ───────────►│  Your program calls accept()
  │                               │  Kernel removes connection from queue
  │                               │  Creates NEW socket for this client
  │  ◄────── client_fd ──────────│  Returns new fd
```

### The Listening Socket Never Changes

One of the most important concepts:

```
server_fd = 3          // Stays listening FOREVER

accept() → client_fd = 5   // Client A
accept() → client_fd = 6   // Client B
accept() → client_fd = 7   // Client C
```

The listening socket (`fd = 3`) is **never** used to communicate with clients. It only produces new connected sockets.

This is why one listening socket can serve thousands of clients.

### Accept Queue & Backlog

When clients connect faster than your server calls `accept()`, the kernel queues them:

```text
Accept Queue (backlog = 5)
┌──────────────┐
│ Client A     │
│ Client B     │
│ Client C     │
└──────────────┘

accept() → removes Client A and returns client_fd
```

If the queue is full and another client tries to connect:
- Linux may drop the SYN (client retries).
- Or the connection may be refused.

Modern Linux kernels treat `backlog` as a hint and apply their own internal limits (`/proc/sys/net/core/somaxconn`).

---

# 9. Data Transfer: `send()` & `recv()`

```cpp
ssize_t send(int sockfd, const void *buf, size_t len, int flags);
ssize_t recv(int sockfd, void *buf, size_t len, int flags);
```

## 9.1 The Full `send()` Path

```text
Your IRC Server (user space)
    │
    │  send(fd, ":server NOTICE Wafae :Hello!\r\n", 32, 0);
    │
    ▼
System Call ───► Kernel Mode
    │
    ▼
1. VFS: Translates fd → struct file
    │
    ▼
2. Socket Layer: Routes to TCP protocol handler
    │
    ▼
3. Copies bytes from user space → kernel TCP send buffer ⭐
    │   (Your program's job is done here. Data is now in kernel memory.)
    │
    ▼
4. TCP Subsystem (later, when it decides to transmit):
    │   - Breaks stream into segments
    │   - Assigns sequence numbers
    │   - Prepares retransmission logic
    │
    ▼
5. IP Subsystem: Adds IP header, determines route
    │
    ▼
6. Traffic Control (qdisc): Queues the packet for transmission
    │
    ▼
7. Device Driver: Converts to Ethernet frame
    │
    ▼
8. NIC: Transmits bits onto the wire
```

## 9.2 The Full `recv()` Path

```text
Internet
    │
    ▼
1. NIC: Receives bits, reconstructs Ethernet frame
    │
    ▼
2. Device Driver: Copies packet into kernel memory
    │
    ▼
3. IP Subsystem: Validates checksum, checks destination IP
    │
    ▼
4. TCP Subsystem:
    │   - Validates checksum & sequence number
    │   - Reorders packets if needed
    │   - Removes duplicates
    │   - Sends ACK
    │   - Copies payload into socket's receive buffer ⭐
    │
    ▼
5. Kernel marks socket as readable (POLLIN)
    │
    ▼
6. poll() wakes up (if server was sleeping)
    │
    ▼
7. Your server calls recv(fd, buffer, sizeof(buffer), 0)
    │
    ▼
8. Kernel copies bytes from receive buffer → your buffer
    │
    ▼
Your IRC Server now has the data
```

## 9.3 Send & Receive Buffers

Every connected socket has **two buffers** inside the kernel:

```text
Socket
┌──────────────────────┐
│ Send Buffer           │  ← Data waiting to be transmitted
│ (owned by kernel)     │
├──────────────────────┤
│ Receive Buffer        │  ← Data received but not yet read
│ (owned by kernel)     │
└──────────────────────┘
```

**Your program owns** `char buffer[1024]` in user space.  
**The kernel owns** the send and receive buffers.  
Data is **copied** between them via system calls.

### Buffer Sizes

Default sizes are typically ~16-128 KB per buffer. You can query or set them with `SO_RCVBUF` / `SO_SNDBUF`.

On Linux, the kernel doubles the value you pass to `setsockopt()` for internal overhead, so `setsockopt(fd, SOL_SOCKET, SO_RCVBUF, &size)` uses `2 * size` internally. Use `SO_RCVBUFFORCE` to bypass this if needed.

## 9.4 Blocking vs Non-Blocking Behavior

| Situation                   | Blocking                              | Non-Blocking                          |
| --------------------------- | ------------------------------------- | ------------------------------------- |
| `recv()` — buffer empty     | Sleep until data arrives              | Return `-1`, `errno = EAGAIN`         |
| `send()` — buffer full      | Sleep until space frees               | Return `-1`, `errno = EAGAIN`         |
| `accept()` — queue empty    | Sleep until client connects           | Return `-1`, `errno = EAGAIN`         |

### Important: `send()` Return Value

`send()` returns the number of bytes actually accepted by the kernel. It may be **less than** what you requested (a "short write"). Always check the return value:

```cpp
ssize_t sent = send(fd, data, len, 0);
if (sent == -1)
{
    if (errno == EAGAIN || errno == EWOULDBLOCK)
        // Try again later (buffer was full)
    else
        // Real error
}
else if (sent < (ssize_t)len)
{
    // Only part of the data was accepted.
    // Queue the remainder for the next attempt.
}
```

### Important: `recv()` Return Value

```cpp
ssize_t n = recv(fd, buf, sizeof(buf), 0);

if (n > 0)   // Received n bytes
if (n == 0)  // Peer closed the connection (EOF)
if (n == -1) // Error (check errno)
```

**TCP is a byte stream, not a message protocol.** One `send()` may require several `recv()` calls, and one `recv()` may return data from multiple `send()` calls. You must parse message boundaries yourself (e.g., `\r\n` for IRC).

---

# 10. I/O Multiplexing: `poll()`

## 10.1 Why `poll()`?

Without `poll()`, your server would need to either:
- **Block** on one client (`recv()`), ignoring everyone else.
- **Busy-wait** (constantly check every client), wasting CPU.

`poll()` solves this by letting the kernel **efficiently sleep** until one or more file descriptors become ready.

```cpp
struct pollfd fds[3];
fds[0].fd = server_fd;     fds[0].events = POLLIN;
fds[1].fd = client_a_fd;   fds[1].events = POLLIN;
fds[2].fd = client_b_fd;   fds[2].events = POLLIN;

int ready = poll(fds, 3, -1);  // Sleep until something happens
```

## 10.2 Kernel Internals of `poll()`

When you call `poll()`, here's what the kernel does:

### Step 1: Copy the `pollfd[]` Array

The kernel copies the user-provided array into kernel space (it can't trust user memory directly).

### Step 2: Allocate Internal Structures

The kernel creates a `poll_wqueues` structure that manages:
- Wait queue registrations
- The wake-up callback (`poll_wake()`)
- Cleanup when `poll()` finishes

For efficiency, the first ~32 entries use inline storage (no heap allocation). Additional entries use dynamically allocated `poll_table_page` pages.

### Step 3: Scan Every FD (O(n))

For each file descriptor:

- **If ready:** Set `revents` immediately (e.g., `POLLIN` if data is available).
- **If NOT ready:** Register the calling thread on that FD's wait queue with a `poll_wake()` callback.

```text
Driver finds FD not ready
        │
        ▼
Creates poll_table_entry
        │
        ▼
Inserts thread into FD's wait queue
        │
        ▼
Thread will sleep
```

### Step 4: Thread Parking

If no FDs are ready and `timeout > 0`, the thread enters interruptible sleep.

### Step 5: Wake-Up

When data arrives on a socket:

```text
Packet arrives
        │
        ▼
Socket driver walks its wait queue
        │
        ▼
Calls poll_wake() on each entry
        │
        ▼
poll_wake() wakes the sleeping thread
```

### Step 6: Cleanup

All wait queue registrations are removed. The `poll_wqueues` structure is destroyed.

### Step 7: Return Results

The kernel copies the updated `revents` back to user space and returns the number of ready FDs.

## 10.3 Performance & Limitations

**`poll()` is O(n)** — it scans every monitored FD on every call:

```text
10,000 FDs monitored → scan all 10,000 every time
                      (even if only 1 is ready)
```

Key bottlenecks:

| Bottleneck            | Why It Matters                                            |
| --------------------- | --------------------------------------------------------- |
| O(n) rescanning       | Every call scans the entire array                         |
| Linear memory copy    | Entire `pollfd[]` copied twice per call (user→kernel→user)|
| No event persistence  | Wait queues are rebuilt from scratch on every call        |

For a typical 42 `ft_irc` project with dozens or hundreds of clients, `poll()` is perfectly adequate.

## 10.4 Alternatives: `select()` & `epoll()`

| Function    | Complexity | Max FDs    | Notes                               |
| ----------- | ---------- | ---------- | ----------------------------------- |
| `select()`  | O(n)       | 1024 limit | Old API, fixed FD_SETSIZE           |
| `poll()`    | O(n)       | No limit   | Good for moderate numbers of FDs    |
| `epoll()`   | O(1)       | No limit   | Linux-only, best for 10,000+ FDs    |

**For `ft_irc`:** Use `poll()`. It's the standard choice for this project.

---

# 11. Signals & Graceful Shutdown

```cpp
volatile sig_atomic_t g_running = 1;

void handle_signal(int sig)
{
    (void)sig;
    g_running = 0;
}

int main()
{
    signal(SIGINT, handle_signal);

    while (g_running)
    {
        poll(fds, nfds, -1);
        // handle events...
    }

    // Cleanup: close all client sockets, destroy channels, etc.
    return 0;
}
```

## Why `volatile sig_atomic_t`?

| Keyword              | Purpose                                                  |
| -------------------- | -------------------------------------------------------- |
| `sig_atomic_t`       | Reads/writes are atomic with respect to signal handlers  |
| `volatile`           | Forces the compiler to re-read from memory each time     |

Without `volatile`, the compiler might optimize `g_running` into a register and never notice when the signal handler changes it.

**Note:** `signal()` is portable but has some platform-specific quirks. On Linux, `sigaction()` is preferred for production code.

---

# 12. Ports & Addresses

## Port Range

Ports are 16-bit unsigned integers (0–65535):

| Range     | Status                      |
| --------- | --------------------------- |
| 0–1023    | Privileged (requires root)  |
| 1024–49151| Registered / User           |
| 49152–65535| Dynamic / Ephemeral        |

Choose ports **1024–65535** for your IRC server to avoid permission issues. The standard IRC port is **6667**.

## Special Addresses

| Address        | Meaning                                   |
| -------------- | ----------------------------------------- |
| `INADDR_ANY`   | `0.0.0.0` — listen on all interfaces      |
| `INADDR_LOOPBACK` | `127.0.0.1` — localhost only          |

## Binding to Specific Interfaces

Use `INADDR_ANY` during development. For production, you might want to bind to a specific IP:

```cpp
inet_pton(AF_INET, "192.168.1.15", &addr.sin_addr);
```

---

# 13. Error Handling Reference

## Common `errno` Values

| errno           | System Call | Meaning                                                |
| --------------- | ----------- | ------------------------------------------------------ |
| `EAGAIN` / `EWOULDBLOCK` | `accept`, `recv`, `send` | Resource not ready (non-blocking) |
| `EADDRINUSE`    | `bind`      | Port already in use by another program                 |
| `ECONNRESET`    | `recv`, `send` | Peer closed connection abruptly                     |
| `EPIPE`         | `send`      | Peer closed connection (also triggers SIGPIPE)         |
| `EINTR`         | `accept`, `recv`, `send`, `poll` | Interrupted by a signal               |
| `EINVAL`        | `bind`      | Invalid arguments (e.g., port 0 without permissions)   |
| `EMFILE`        | `accept`, `socket` | Process has too many open file descriptors       |
| `ENFILE`        | `socket`    | System-wide file descriptor limit reached             |
| `ECONNREFUSED`  | `connect` (client) | Nothing listening on the target port              |

## Important: `EAGAIN` vs `EWOULDBLOCK`

On Linux, these are the same value. Always check both for portability:

```cpp
if (errno == EAGAIN || errno == EWOULDBLOCK)
    // Normal for non-blocking — try again later
```

## Important: `EINTR` (Signal Interruption)

System calls can be interrupted by signals. Always retry on `EINTR`:

```cpp
int ret;
do {
    ret = poll(fds, nfds, timeout);
} while (ret == -1 && errno == EINTR);
```

## File Descriptor Limits

Check your per-process FD limit with:

```bash
ulimit -n
```

Default is often 1024. You can raise it:

```bash
ulimit -n 4096
```

If your server needs to handle many clients, make sure this limit is high enough.

---

# 14. Common Misconceptions

### ❌ "`send()` immediately transmits data over the network."

**False.** `send()` copies bytes into the kernel's send buffer. The TCP subsystem decides when to actually transmit.

### ❌ "`recv()` reads directly from the network card."

**False.** `recv()` reads from the kernel's receive buffer. The kernel received and processed the data earlier.

### ❌ "One `send()` = one `recv()`."

**False.** TCP is a byte stream. The receiver may need multiple `recv()` calls to get all data, and one `recv()` may return data from multiple `send()` calls.

### ❌ "`recv()` waits for the full requested buffer size."

**False.** `recv()` returns whatever bytes are currently available (up to the requested size), without waiting for the buffer to fill.

### ❌ "The listening socket becomes the client socket after `accept()`."

**False.** The listening socket stays in `LISTEN` state forever. `accept()` creates an entirely new socket for each client.

### ❌ "`listen()` accepts clients."

**False.** `listen()` only marks the socket as ready to accept connections. The kernel does the actual accepting via the TCP handshake, and your program retrieves connected clients via `accept()`.

### ❌ "`bind()` communicates over the network."

**False.** `bind()` is a purely local kernel operation. It registers an address/port with the kernel's internal tables.

### ❌ "`O_NONBLOCK` makes `poll()` non-blocking."

**False.** `poll()` has its own blocking behavior controlled by its `timeout` parameter. `O_NONBLOCK` only affects I/O operations like `accept()`, `send()`, and `recv()`.

### ❌ "After `send()` returns successfully, the client has received the data."

**False.** `send()` returning successfully means the kernel has accepted your bytes into its send buffer. The data may still be waiting to be transmitted.

### ❌ "`accept()` performs the TCP handshake."

**False.** The kernel's TCP subsystem performs the entire handshake automatically. `accept()` just retrieves the already-established connection from a queue.

---

# 15. The IRC Protocol

## 15.1 What is IRC?

**Internet Relay Chat (IRC)** is a text-based protocol for real-time communication over TCP.

```
HTTP  → websites
SMTP  → email
FTP   → file transfer
IRC   → live text chat
```

Clients and servers communicate using **plain text commands** terminated by `\r\n`:

```irc
NICK wafae
USER wafae 0 * :Wafae
JOIN #42
PRIVMSG #42 :Hello everyone!
QUIT
```

No JSON. No HTTP. No XML. Just text lines ending with `\r\n`.

## 15.2 IRC Architecture

```text
         Client A
             │
             │  TCP Connection
             ▼
        IRC Server
       /     |     \
      /      |      \
     ▼       ▼       ▼
 Client B  Client C  Client D
```

- Clients never communicate directly. Everything goes through the server.
- The server manages: authentication, channels, message routing, and protocol enforcement.

## 15.3 RFC 1459 vs RFC 2812

### Timeline

| Year | RFC      | Description                  |
| ---- | -------- | ---------------------------- |
| 1993 | RFC 1459 | Original IRC specification   |
| 2000 | RFC 2810 | IRC Architecture             |
| 2000 | RFC 2811 | Channel Management           |
| 2000 | RFC 2812 | **Client Protocol** ← Use this |
| 2000 | RFC 2813 | Server Protocol              |

### What Changed

RFC 2812 is **not a new protocol** — it's the client-server portion of RFC 1459, **updated and clarified**.

| Aspect              | RFC 1459                    | RFC 2812                              |
| ------------------- | --------------------------- | ------------------------------------- |
| Scope               | Everything in one document  | Client ↔ Server protocol only         |
| Organization        | Monolithic                  | Focused and easier to navigate        |
| Clarity             | Some ambiguity              | Clearer specifications                |
| Numeric replies     | Original                    | Updated & clarified                   |
| Use for `ft_irc`    | Historical reference        | **Primary implementation reference**  |

**For `ft_irc`:** Read RFC 2812 first. Consult RFC 1459 for historical context.

## 15.4 Key IRC Commands

### Connection Registration

| Command | Example                     | Purpose                      |
| ------- | --------------------------- | ---------------------------- |
| `PASS`  | `PASS password`             | Server password (optional)   |
| `NICK`  | `NICK wafae`                | Set nickname                 |
| `USER`  | `USER wafae 0 * :Wafae`    | Set username and realname    |
| `QUIT`  | `QUIT :Goodbye!`            | Disconnect                   |

### Channel Operations

| Command  | Example                        | Purpose                        |
| -------- | ------------------------------ | ------------------------------ |
| `JOIN`   | `JOIN #42`                     | Join a channel                 |
| `PART`   | `PART #42`                     | Leave a channel                |
| `MODE`   | `MODE #42 +o wafae`           | Change channel/user modes      |
| `TOPIC`  | `TOPIC #42 :New topic`        | Get or set channel topic       |
| `KICK`   | `KICK #42 bad_user :Spam`     | Remove a user from channel     |

### Messaging

| Command   | Example                             | Purpose               |
| --------- | ----------------------------------- | --------------------- |
| `PRIVMSG` | `PRIVMSG #42 :Hello!`              | Send message to user or channel |
| `NOTICE`  | `NOTICE wafae :You are muted.`     | Like PRIVMSG but for automated messages |

### Server Queries

| Command | Example             | Purpose                       |
| ------- | ------------------- | ----------------------------- |
| `PING`  | `PING :server`      | Connection keepalive          |
| `PONG`  | `PONG :server`      | Response to PING              |
| `WHO`   | `WHO #42`           | List users in a channel       |
| `WHOIS` | `WHOIS wafae`       | Get information about a user  |

## 15.5 IRC Numeric Replies

IRC uses 3-digit numeric codes for replies instead of human-readable text. Here are the most important ones:

### Connection Replies

| Code | Name         | Meaning                        |
| ---- | ------------ | ------------------------------ |
| 001  | RPL_WELCOME  | Welcome to the IRC network     |
| 002  | RPL_YOURHOST | Your host info                 |
| 003  | RPL_CREATED  | Server creation date           |
| 004  | RPL_MYINFO   | Server version info            |

### Error Replies

| Code | Name                  | Meaning                                     |
| ---- | --------------------- | ------------------------------------------- |
| 401  | ERR_NOSUCHNICK       | No such nick/channel                        |
| 403  | ERR_NOSUCHCHANNEL    | No such channel                             |
| 404  | ERR_CANNOTSENDTOCHAN | Cannot send to channel                      |
| 431  | ERR_NONICKNAMEGIVEN  | No nickname given                           |
| 432  | ERR_ERRONEUSNICKNAME | Erroneous nickname                          |
| 433  | ERR_NICKNAMEINUSE    | Nickname already in use                     |
| 441  | ERR_USERNOTINCHANNEL | User is not in that channel                 |
| 442  | ERR_NOTONCHANNEL     | You're not on that channel                  |
| 443  | ERR_USERONCHANNEL    | User is already on channel                  |
| 461  | ERR_NEEDMOREPARAMS   | Not enough parameters                       |
| 462  | ERR_ALREADYREGISTRED | Unauthorized command (already registered)   |
| 464  | ERR_PASSWDMISMATCH   | Password incorrect                          |
| 471  | ERR_CHANNELISFULL    | Channel is full                             |
| 472  | ERR_UNKNOWNMODE      | Unknown mode character                      |
| 473  | ERR_INVITEONLYCHAN   | Cannot join invite-only channel             |
| 474  | ERR_BANNEDFROMCHAN   | Banned from channel                         |
| 475  | ERR_BADCHANNELKEY    | Wrong channel key (password)                |
| 482  | ERR_CHANOPRIVSNEEDED | Not a channel operator                      |

### Command Replies

| Code | Name              | Meaning                                  |
| ---- | ----------------- | ---------------------------------------- |
| 324  | RPL_CHANNELMODEIS | Channel mode information                 |
| 331  | RPL_NOTOPIC       | No topic is set                          |
| 332  | RPL_TOPIC         | Topic information                        |
| 353  | RPL_NAMREPLY      | List of users in a channel               |
| 366  | RPL_ENDOFNAMES    | End of NAMES list                        |
| 372  | RPL_MOTD          | Message of the day                       |
| 375  | RPL_MOTDSTART     | Start of MOTD                            |
| 376  | RPL_ENDOFMOTD     | End of MOTD                              |

The **format** of a numeric reply is:

```irc
:server 001 wafae :Welcome to the IRC Network wafae!~user@host
```

## 15.6 Netcat for Testing

**Netcat (`nc`)** is a simple TCP client that sends and receives raw bytes. It's invaluable for testing your IRC server because it doesn't process the IRC protocol — every byte you type goes directly to the server.

```bash
$ nc localhost 6667
PASS password
NICK wafae
USER wafae 0 * :Wafae
JOIN #42
PRIVMSG #42 :Hello from raw TCP!
```

Netcat is useful for testing:
- Malformed commands
- Missing parameters
- Edge cases (e.g., sending partial commands)
- Server responses without client-side processing

```text
      You (developer)
           │
           ▼
      Netcat (nc)
           │
    Raw TCP Connection
           │
           ▼
      IRC Server
           │
     Parses & responds
           │
           ▼
      Netcat displays
```

---

# 16. Glossary

| Term | Definition |
| ---- | ---------- |
| **AF_INET** | Address family for IPv4 networking |
| **AF_UNIX** | Address family for local Unix domain sockets |
| **SOCK_STREAM** | Socket type for reliable, ordered byte streams (TCP) |
| **SOCK_DGRAM** | Socket type for unreliable datagrams (UDP) |
| **File Descriptor (FD)** | An integer handle that refers to an open file/socket in the kernel |
| **System Call** | A controlled entry point into the kernel from user space |
| **Kernel Space** | Privileged memory area where the operating system runs |
| **User Space** | Unprivileged memory area where applications run |
| **TCP Handshake** | The 3-way SYN/SYN-ACK/ACK exchange that establishes a TCP connection |
| **Send Buffer** | Kernel-owned memory where data waits before being transmitted |
| **Receive Buffer** | Kernel-owned memory where received data waits to be read |
| **Byte Stream** | TCP provides a continuous stream of bytes with no message boundaries |
| **Non-Blocking** | I/O operations return immediately with `EAGAIN` if they can't complete |
| **I/O Multiplexing** | Monitoring multiple FDs with a single call (`poll()`) |
| **Accept Queue** | Kernel queue holding completed TCP connections waiting for `accept()` |
| **Backlog** | Maximum size hint for the accept queue |
| **O_NONBLOCK** | File status flag that makes I/O non-blocking |
| **EAGAIN** | Error code meaning "try again" — the resource isn't ready |
| **EADDRINUSE** | Error code meaning the port is already in use |
| **INADDR_ANY** | Special IP (0.0.0.0) meaning "listen on all interfaces" |
| **htons()** | Host-to-network short: converts port number to network byte order |
| **RFC** | Request For Comments — an official Internet protocol specification |
| **IRC** | Internet Relay Chat — a text-based chat protocol |
| **PRIVMSG** | IRC command for sending a private message |
| **Numeric Reply** | IRC response using 3-digit codes (e.g., 001 = welcome) |
| **qdisc** | Queuing discipline — kernel traffic control subsystem |
| **pollfd** | Structure passed to `poll()` containing fd, events, and revents |
| **POLLIN** | Event flag meaning data is available to read |
| **POLLOUT** | Event flag meaning the socket is ready to send data |
| **POLLHUP** | Event flag meaning the peer closed the connection |
| **POLLERR** | Event flag meaning an error occurred on the socket |

---

> **Pro tip:** This handbook is a reference — don't try to memorize it all at once. Come back to individual sections as you implement each part of your IRC server. The most common coding mistakes come from forgetting that `send()` and `recv()` work with kernel buffers, not directly with the network.
