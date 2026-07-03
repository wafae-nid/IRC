# Linux Kernel Journey: From Syscall to Hardware

## A Complete Walkthrough of Every System Call in the 42 IRC Project

---

> **Purpose:** When your IRC server calls a syscall like `send()` or `recv()`, it doesn't just "send data." The request travels through ~7 distinct kernel subsystems, each with a specific job. This document traces every syscall in your project from the **VFS layer** down to the **hardware** — and back up again.

---

# Table of Contents

1. [The Big Picture — Kernel Subsystems in Order](#1-the-big-picture)
2. [Syscall #1: `socket()` — Create an Endpoint](#2-socket)
3. [Syscall #2: `setsockopt()` — Configure the Socket](#3-setsockopt)
4. [Syscall #3: `fcntl()` — Control the File Descriptor](#4-fcntl)
5. [Syscall #4: `bind()` — Assign an Address](#5-bind)
6. [Syscall #5: `listen()` — Start Listening](#6-listen)
7. [Syscall #6: `poll()` — Wait for Events](#7-poll)
8. [Syscall #7: `accept()` — Accept a Connection](#8-accept)
9. [Syscall #8: `recv()` — Receive Data](#9-recv)
10. [Syscall #9: `send()` — Send Data](#10-send)
11. [Syscall #10: `close()` — Tear Down](#11-close)
12. [Syscall #11: `signal()` / `sigaction()` — Handle Signals](#12-signal)
13. [Syscall #12: `write()` / `read()` (Implied)](#13-write-read)
14. [Complete Reference Table](#14-reference-table)
15. [Kernel Data Structures Quick Reference](#15-data-structures)

---

<a name="1-the-big-picture"></a>
# 1. The Big Picture — Kernel Subsystems in Order

Every system call in the networking path touches these layers, top to bottom:

```
                USER SPACE (Ring 3)
   ┌───────────────────────────────────────┐
   │        Your IRC Server (C++)          │
   │    socket() bind() listen() poll()    │
   │   accept() send() recv() close()      │
   └──────────────┬────────────────────────┘
                  │  syscall instruction (syscall/sysenter/int 0x80)
                  ▼
                KERNEL SPACE (Ring 0)
   ┌──────────────────────────────────────┐
   │    1. SYSCALL ENTRY (entry.S)        │  ← trap to kernel mode
   │     - Save registers                  │
   │     - Route to sys_* handler         │
   ├──────────────────────────────────────┤
   │    2. VFS LAYER                      │  ← "everything is a file"
   │     - struct file (fd table)         │
   │     - struct file_operations         │
   │     - Permission checks              │
   ├──────────────────────────────────────┤
   │    3. SOCKET LAYER (BSD API)         │  ← protocol-agnostic socket API
   │     - struct socket                  │
   │     - struct proto_ops (ops table)   │
   ├──────────────────────────────────────┤
   │    4. NETWORK CORE (sock layer)      │  ← connection state & buffers
   │     - struct sock                    │
   │     - sk_receive_queue / sk_write_queue
   ├──────────────────────────────────────┤
   │    5. TCP LAYER                      │  ← reliability, flow control
   │     - struct tcp_sock                │
   │     - seq nums, windows, retransmit  │
   ├──────────────────────────────────────┤
   │    6. IP LAYER                       │  ← routing, addressing
   │     - struct sk_buff                 │
   │     - Routing table (FIB)            │
   │     - Netfilter hooks (iptables)     │
   ├──────────────────────────────────────┤
   │    7. TRAFFIC CONTROL (qdisc)        │  ← packet scheduling
   │     - pfifo_fast, fq_codel, etc.     │
   ├──────────────────────────────────────┤
   │    8. DEVICE DRIVER LAYER            │  ← NIC abstraction
   │     - struct net_device              │
   │     - ndo_start_xmit (driver func)   │
   ├──────────────────────────────────────┤
   │    9. NIC HARDWARE                   │  ← physical transmission
   │     - DMA ring buffers               │
   │     - PHY (electrical/optical)       │
   └──────────────────────────────────────┘
```

**On the receive path**, the data flows **up** through the same layers in reverse, driven by hardware interrupts and SoftIRQs (software interrupts), then finally reaches the sleeping `poll()` / `recv()` call waiting in user space.

---

<a name="2-socket"></a>
# 2. Syscall #1: `socket()`

### IRC Server Usage

```cpp
server_fd = socket(AF_INET, SOCK_STREAM, 0);
//            ↑         ↑            ↑
//         IPv4       TCP       default protocol
```

### Subsystem Breakdown

| # | Subsystem | Job | What it creates/sets |
|---|-----------|-----|---------------------|
| 1 | **Syscall Entry** | Trap to kernel mode | None — saves registers, dispatches to `sys_socket()` |
| 2 | **Socket Layer (BSD)** | Allocate generic socket + look up address family | `struct socket` with `ops = &inet_stream_ops` |
| 3 | **INET Family (IPv4)** | Select TCP protocol + allocate TCP socket | `struct tcp_sock` (which **is** a `struct sock`), links `socket->sk = sock` |
| 4 | **TCP Subsystem** | Prepare TCP state | `sk->sk_prot = &tcp_prot`, `sk_state = TCP_CLOSE`, init buffers & timers |
| 5 | **VFS** | File descriptor management | `struct file` with `f_op = socket_file_ops`, `private_data = socket`, assigns fd number |

### Structures Created (in order)

```
1. struct socket_alloc { socket + inode }     ← BSD socket layer
2. struct tcp_sock { inet_sock { sock {...} } } ← INET family (this IS the sock)
3. struct file { f_op, private_data = socket }  ← VFS

Links:
  fd 3 → struct file → struct socket → struct tcp_sock (which embeds struct sock)

State after socket():
  sk_state = TCP_CLOSE
  socket->ops = &inet_stream_ops    ← route future API calls to TCP
  sk->sk_prot = &tcp_prot           ← route future protocol calls to TCP
```

---

<a name="3-setsockopt"></a>
# 3. Syscall #2: `setsockopt()`

### IRC Server Usage

```cpp
int opt = 1;
setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
//          ↑              ↑              ↑            ↑        ↑
//        sock fd      socket level  reuse addr    opt value  opt len
```

### Full Kernel Journey

```
  User: setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, optlen)
         │
         ▼
  ┌──────────────────────────────────────────────────────────┐
  │ 1. SYSCALL ENTRY                                          │
  │    ┌────────────────────────────────────────────────────┐ │
  │    │ sys_setsockopt()                                    │ │
  │    │   File: net/socket.c                                │ │
  │    │   - Copies opt value from user space → kernel       │ │
  │    │   - Looks up fd → struct file → struct socket       │ │
  │    └────────────────────────────────────────────────────┘ │
  │                                                           │
  │ 2. VFS → SOCKET LAYER                                     │
  │    ┌────────────────────────────────────────────────────┐ │
  │    │ sock_setsockopt()                                   │ │
  │    │   - Level = SOL_SOCKET → handled at socket layer   │ │
  │    │   - Calls socket->ops->setsockopt()                 │ │
  │    │     which is inet_setsockopt()                      │ │
  │    └────────────────────────────────────────────────────┘ │
  │                                                           │
  │ 3. SOCKET-LEVEL HANDLING                                  │
  │    ┌────────────────────────────────────────────────────┐ │
  │    │ sock_setsockopt() → SO_REUSEADDR handler            │ │
  │    │   - Sets sk->sk_reuse = SK_CAN_REUSE               │ │
  │    │   - This flag is checked by TCP during bind()       │ │
  │    │     to allow binding to an address in TIME_WAIT     │ │
  │    └────────────────────────────────────────────────────┘ │
  │                                                           │
  │ 4. RETURN                                                │
  │    0 on success, -1 on error                             │
  └──────────────────────────────────────────────────────────┘
```

### What `SO_REUSEADDR` Actually Does

Without it: if the server crashes and restarts quickly, `bind()` fails with `EADDRINUSE` because the port is stuck in `TIME_WAIT` (2 * MSL, typically 60 seconds).

With it: the kernel allows the new server to bind to the same address even if a previous socket is in `TIME_WAIT`. The kernel handles this gracefully — old lingering packets will be discarded by the new connection's sequence numbers.

### Subsystems Touched

| Subsystem | What It Does |
|-----------|-------------|
| Syscall Entry | Copies data from user → kernel memory |
| VFS | Resolves fd → struct socket |
| Socket Layer | Routes to protocol-agnostic option handler |
| sock layer | Sets `sk_reuse` flag on the `struct sock` |

---

<a name="4-fcntl"></a>
# 4. Syscall #3: `fcntl()`

### IRC Server Usage

```cpp
fcntl(server_fd, F_SETFL, O_NONBLOCK);
// or
fcntl(client_fd, F_SETFL, O_NONBLOCK);
```

### Full Kernel Journey

```
  User: fcntl(fd, F_SETFL, O_NONBLOCK)
         │
         ▼
  ┌──────────────────────────────────────────────────────────┐
  │ 1. SYSCALL ENTRY                                          │
  │    ┌────────────────────────────────────────────────────┐ │
  │    │ sys_fcntl()                                        │ │
  │    │   File: fs/fcntl.c                                 │ │
  │    │   - Finds fd in process fd table                    │ │
  │    │   - Gets struct file                                │ │
  │    │   - For F_SETFL: calls setfl() internally           │ │
  │    └────────────────────────────────────────────────────┘ │
  │                                                           │
  │ 2. VFS LAYER                                              │
  │    ┌────────────────────────────────────────────────────┐ │
  │    │ setfl() — operates on file flags                   │ │
  │    │   File: fs/fcntl.c                                 │ │
  │    │   - Modifies file->f_flags                         │ │
  │    │   - For O_NONBLOCK: sets FMODE_NONBLOCK flag       │ │
  │    │   - If file_operations->flock exists, calls it      │ │
  │    └────────────────────────────────────────────────────┘ │
  │                                                           │
  │ 3. NOTIFICATION TO SOCKET LAYER                           │
  │    ┌────────────────────────────────────────────────────┐ │
  │    │ Since socket_file_ops has no ->flock, nothing more  │ │
  │    │ happens at socket layer. BUT...                     │ │
  │    │ The non-blocking flag affects ALL future socket I/O │ │
  │    │ because send()/recv() check:                       │ │
  │    │   file->f_flags & O_NONBLOCK                       │ │
  │    └────────────────────────────────────────────────────┘ │
  │                                                           │
  │ 4. HOW NON-BLOCKING AFFECTS LATER SYSCALLS                │
  │    send():                                                │
  │      If send buffer full → return -1 / EAGAIN            │
  │      (instead of blocking/sleeping)                       │
  │    recv():                                                │
  │      If no data available → return -1 / EAGAIN           │
  │      (instead of blocking/sleeping)                       │
  │    connect():                                             │
  │      Returns -1 / EINPROGRESS immediately                 │
  │      (TCP handshake continues in background)              │
  │                                                           │
  │ 5. RETURN                                                │
  │    0 on success, -1 on error                             │
  └──────────────────────────────────────────────────────────┘
```

### Why This Matters for Your IRC Server

Your server handles **many clients in a single thread** using `poll()`. Without `O_NONBLOCK`:

- A single `recv()` on a socket with no data would **block the entire server**
- A single `send()` to a client with a full buffer would **block the entire server**
- All other clients would freeze

With `O_NONBLOCK`, the server never blocks on any single client.

### Subsystems Touched

| Subsystem | What It Does |
|-----------|-------------|
| Syscall Entry | Dispatches to fcntl handler |
| VFS | Modifies file descriptor flags in `struct file` |
| (Socket Layer) | Implicitly — affects future sock_sendmsg/sock_recvmsg behavior |

---

<a name="5-bind"></a>
# 5. Syscall #4: `bind()`

### IRC Server Usage

```cpp
struct sockaddr_in addr;
addr.sin_family = AF_INET;
addr.sin_port = htons(s_port);      // e.g., htons(6667)
addr.sin_addr.s_addr = INADDR_ANY;  // 0.0.0.0

bind(server_fd, (struct sockaddr *)&addr, sizeof(addr));
```

### Full Kernel Journey

```
  User: bind(server_fd, &addr, addrlen)
         │
         ▼
  ┌──────────────────────────────────────────────────────────┐
  │ 1. SYSCALL ENTRY                                          │
  │    ┌────────────────────────────────────────────────────┐ │
  │    │ sys_bind()                                         │ │
  │    │   File: net/socket.c                               │ │
  │    │   - Copies sockaddr from user → kernel memory       │ │
  │    │   - Resolves fd → file → socket                     │ │
  │    │   - Calls socket->ops->bind() = inet_bind()         │ │
  │    └────────────────────────────────────────────────────┘ │
  │                                                           │
  │ 2. VFS → SOCKET LAYER                                     │
  │    ┌────────────────────────────────────────────────────┐ │
  │    │ Points to inet_bind() via socket->ops->bind         │ │
  │    │ inet_bind() (net/ipv4/af_inet.c)                   │ │
  │    │   - Calls sk->sk_prot->bind() = tcp_v4_bind()      │ │
  │    └────────────────────────────────────────────────────┘ │
  │                                                           │
  │ 3. TCP BIND                                               │
  │    ┌────────────────────────────────────────────────────┐ │
  │    │ tcp_v4_bind() (net/ipv4/tcp_ipv4.c)                │ │
  │    │                                                     │ │
  │    │   VALIDATION:                                       │ │
  │    │   - Portable range: if `< 1024`, needs CAP_NET_BIND │ │
  │    │   - Checks SO_REUSEADDR flag on socket              │ │
  │    │                                                     │ │
  │    │   HASH TABLE OPERATIONS:                            │ │
  │    │   - Checks bind hash table (bhash) for conflicts    │ │
  │    │   - If port is already in use:                      │ │
  │    │     - Without SO_REUSEADDR → EADDRINUSE             │ │
  │    │     - With SO_REUSEADDR and TIME_WAIT → allowed     │ │
  │    │   - Inserts into bhash:                            │ │
  │    │       bhash[port_hash] → sock                       │ │
  │    │                                                     │ │
  │    │   STORES ADDRESS:                                   │ │
  │    │   - sk->sk_num  = htons(port)   // local port      │ │
  │    │   - sk->sk_rcv_saddr = INADDR_ANY // local IP     │ │
  │    │   - inet_sk(sk)->inet_sport = htons(port)         │ │
  │    │   - inet_sk(sk)->inet_saddr = 0                   │ │
  │    └────────────────────────────────────────────────────┘ │
  │                                                           │
  │ 4. RETURN                                                │
  │    0 on success, -1 on error                             │
  └──────────────────────────────────────────────────────────┘
```

### The Bind Hash Table (`bhash`)

The kernel maintains a global hash table indexed by port number:

```
bhash (struct inet_bind_hashbucket):
  bucket[0] → sock1 → sock2 → ... (sockets bound to port X)
  bucket[1] → ...
  ...
  bucket[port % BHASH_SIZE] → sock (your server socket)
```

This allows the kernel to quickly check if a port is already in use when `bind()` is called.

### Subsystems Touched

| Subsystem | What It Does |
|-----------|-------------|
| Syscall Entry | Copies sockaddr to kernel, routes to socket layer |
| VFS | Resolves fd to socket |
| Socket Layer | Protocol-agnostic bind routing |
| INET Family | Dispatches to TCP-specific bind |
| TCP Layer | Validates port, checks SO_REUSEADDR, inserts into bhash |

---

<a name="6-listen"></a>
# 6. Syscall #5: `listen()`

### IRC Server Usage

```cpp
listen(server_fd, 10);
//              ↑
//        backlog = max pending connections
```

### Full Kernel Journey

```
  User: listen(server_fd, backlog)
         │
         ▼
  ┌──────────────────────────────────────────────────────────┐
  │ 1. SYSCALL ENTRY                                          │
  │    ┌────────────────────────────────────────────────────┐ │
  │    │ sys_listen()                                       │ │
  │    │   File: net/socket.c                               │ │
  │    │   - Resolves fd → socket                           │ │
  │    │   - Calls socket->ops->listen() = inet_listen()    │ │
  │    └────────────────────────────────────────────────────┘ │
  │                                                           │
  │ 2. SOCKET LAYER                                           │
  │    ┌────────────────────────────────────────────────────┐ │
  │    │ inet_listen()                                      │ │
  │    │   - Calls sk->sk_prot->listen() = tcp_v4_listen()  │ │
  │    └────────────────────────────────────────────────────┘ │
  │                                                           │
  │ 3. TCP LISTEN                                             │
  │    ┌────────────────────────────────────────────────────┐ │
  │    │ tcp_v4_listen() → inet_csk_listen_start()          │ │
  │    │   File: net/ipv4/tcp_ipv4.c / net/ipv4/inet_connection_sock.c │
  │    │                                                     │ │
  │    │   STATE TRANSITION:                                 │ │
  │    │   sk->sk_state = TCP_LISTEN                         │ │
  │    │   (was TCP_CLOSE from socket())                     │ │
  │    │                                                     │ │
  │    │   REQUEST SOCKET QUEUE INIT:                        │ │
  │    │   - Allocates struct inet_connection_sock           │ │
  │    │   - Initializes icsk_accept_queue:                  │ │
  │    │     • syn_table[N] — hash table for half-open SYN  │ │
  │    │       requests (SYN_RECV state)                     │ │
  │    │     • icsk_accept_queue — FIFO of fully-established │ │
  │    │       connections (ESTABLISHED, waiting for accept) │ │
  │    │                                                     │ │
  │    │   HASH TABLE INSERTION (lhash2):                    │ │
  │    │   - Inserts this socket into the listen hash table  │ │
  │    │   - lhash2 is indexed by port for fast incoming SYN │ │
  │    │     lookup                                          │ │
  │    │     lhash2[port_hash] → this listening socket       │ │
  │    │                                                     │ │
  │    │   BACKLOG:                                          │ │
  │    │   - icsk_accept_qlen = 0 (will increment on accept) │ │
  │    │   - sk->sk_max_ack_backlog = backlog (your 10)      │ │
  │    └────────────────────────────────────────────────────┘ │
  │                                                           │
  │ 4. RETURN                                                │
  │    0 on success, -1 on error                             │
  └──────────────────────────────────────────────────────────┘
```

### What Happens in the Kernel After `listen()` (No Syscall Required)

After `listen()`, the kernel automatically handles TCP three-way handshakes **without your server doing anything**:

```
  Client                          Server (kernel)
    │                                  │
    ├─── SYN ────────────────────────► │
    │                                  │  Kernel allocates request_sock
    │                                  │  Enters SYN_RECV state
    │                                  │  Inserts into syn_table
    │◄──── SYN-ACK ────────────────── │  Kernel sends SYN-ACK
    │                                  │  Starts SYN_RECV timer
    │                                  │
    ├─── ACK ────────────────────────► │
    │                                  │  Kernel completes handshake
    │                                  │  request_sock → full sock
    │                                  │  Pushes sock to accept queue
    │                                  │  (now your poll() sees POLLIN)
    │                                  │
```

Your `accept()` just pops the already-completed connection from the queue.

### Subsystems Touched

| Subsystem | What It Does |
|-----------|-------------|
| Syscall Entry | Validates, dispatches |
| VFS / Socket Layer | Routes to TCP listen handler |
| TCP Layer | Transitions state to TCP_LISTEN, creates accept queue |
| INET Hash Tables | Inserts into lhash2 for incoming SYN lookup |

---

<a name="7-poll"></a>
# 7. Syscall #6: `poll()`

### IRC Server Usage

```cpp
struct pollfd fds[N];   // array of { fd, events, revents }
int ret = poll(fds, nfds, -1);  // timeout = -1 → wait forever
```

### Full Kernel Journey

```
  User: poll(fds, nfds, timeout)
         │
         ▼
  ┌──────────────────────────────────────────────────────────┐
  │ 1. SYSCALL ENTRY                                          │
  │    ┌────────────────────────────────────────────────────┐ │
  │    │ sys_poll() / do_sys_poll()                         │ │
  │    │   File: fs/select.c                                │ │
  │    │   - Copies pollfd array from user → kernel          │ │
  │    │   - Allocates internal poll list                    │ │
  │    └────────────────────────────────────────────────────┘ │
  │                                                           │
  │ 2. FOR EACH FD: VFS → file_operations                     │
  │    ┌────────────────────────────────────────────────────┐ │
  │    │ For each fd in the array:                           │ │
  │    │   - Resolves fd → struct file                      │ │
  │    │   - Calls file->f_op->poll() = sock_poll()        │ │
  │    │   (sets up a waitqueue entry for each socket)       │ │
  │    └────────────────────────────────────────────────────┘ │
  │                                                           │
  │ 3. SOCKET POLL                                            │
  │    ┌────────────────────────────────────────────────────┐ │
  │    │ sock_poll()                                        │ │
  │    │   File: net/socket.c                               │ │
  │    │   - Calls socket->ops->poll() = tcp_poll()         │ │
  │    └────────────────────────────────────────────────────┘ │
  │                                                           │
  │ 4. TCP POLL                                               │
  │    ┌────────────────────────────────────────────────────┐ │
  │    │ tcp_poll() (net/ipv4/tcp.c)                        │ │
  │    │   - Adds current process to the socket's waitqueue  │ │
  │    │   - Checks sk_receive_queue (data available?)       │ │
  │    │   - Checks sk_write_queue (space for send?)         │ │
  │    │   - Returns POLLIN/POLLOUT/POLLERR/POLLHUP mask     │ │
  │    │                                                     │ │
  │    │   For listening socket:                             │ │
  │    │     - Checks icsk_accept_queue (connections ready?) │ │
  │    │     - Non-empty → POLLIN                            │ │
  │    │     - Empty → no flags (wait)                       │ │
  │    │                                                     │ │
  │    │   For connected socket:                             │ │
  │    │     - Data in receive queue → POLLIN                │ │
  │    │     - Socket closed by peer → POLLHUP               │ │
  │    │     - Space in send buffer → POLLOUT               │ │
  │    └────────────────────────────────────────────────────┘ │
  │                                                           │
  │ 5. SCHEDULING                                             │
  │    ┌────────────────────────────────────────────────────┐ │
  │    │ If NO events are ready on ANY socket:               │ │
  │    │   - Scheduler puts process to sleep (TASK_INTERRUPTIBLE)
  │    │   - Process is removed from run queue               │ │
  │    │   - "Parked" until:                                 │ │
  │    │     • New data arrives on monitored socket          │ │
  │    │       → data_ready callback wakes process           │ │
  │    │     • New connection arrives on listen socket       │ │
  │    │       → kernel pushes to accept queue, wakes proc  │ │
  │    │     • Timer expires (if timeout set)                │ │
  │    │     • Signal received (SIGINT, etc.)                │ │
  │    │                                                     │ │
  │    │ If events ARE ready (or become ready while polling): │ │
  │    │   - Records revents on each fd                      │ │
  │    │   - Returns immediately (or after timeout)           │ │
  │    └────────────────────────────────────────────────────┘ │
  │                                                           │
  │ 6. RETURN TO USER                                         │
  │    - Copies results back to user space                    │
  │    - Returns number of ready fds (or error)               │
  └──────────────────────────────────────────────────────────┘
```

### The Wait Queue Mechanism (Key Insight)

```
                    Task (IRC Server)
                         │
                    [poll() call]
                         │
                    ┌────┴────┐
                    │ wait  entry │  ← process's "I'm waiting" ticket
                    └─────────┘
                         │
        ┌───┬───┬───┬───┼───┬───┬───┬───┐
        │   │   │   │   │   │   │   │   │   ← socket's wait queue
        └───┴───┴───┴───┴───┴───┴───┴───┘

WHEN DATA ARRIVES (interrupt context / SoftIRQ):
    sk_data_ready() callback is called
      → wakes up all tasks in the socket's wait queue
      → scheduler marks tasks as TASK_RUNNING
      → poll() finishes, returns ready events
```

### Subsystems Touched

| Subsystem | What It Does |
|-----------|-------------|
| Syscall Entry | Copies pollfd arrays, dispatches |
| VFS | Resolves fds to struct files, calls file_operations->poll |
| Socket Layer | Routes to tcp_poll |
| TCP Layer | Checks receive/send queues, accept queue |
| Process Scheduler | Parks process when no events, wakes it on data arrival |

---

<a name="8-accept"></a>
# 8. Syscall #7: `accept()`

### IRC Server Usage

```cpp
struct sockaddr_in addr;
socklen_t len = sizeof(addr);
int client_fd = accept(server_fd, (struct sockaddr *)&addr, &len);
```

### Full Kernel Journey

```
  User: accept(server_fd, &addr, &addrlen)
         │
         ▼
  ┌──────────────────────────────────────────────────────────┐
  │ 1. SYSCALL ENTRY                                          │
  │    ┌────────────────────────────────────────────────────┐ │
  │    │ sys_accept4() → do_accept()                        │ │
  │    │   File: net/socket.c                               │ │
  │    │   - Resolves fd → listening socket                  │ │
  │    │   - Calls socket->ops->accept() = inet_accept()   │ │
  │    └────────────────────────────────────────────────────┘ │
  │                                                           │
  │ 2. SOCKET LAYER                                           │
  │    ┌────────────────────────────────────────────────────┐ │
  │    │ inet_accept() → inet_csk_accept()                  │ │
  │    │   File: net/ipv4/af_inet.c                         │ │
  │    └────────────────────────────────────────────────────┘ │
  │                                                           │
  │ 3. TCP ACCEPT QUEUE                                       │
  │    ┌────────────────────────────────────────────────────┐ │
  │    │ inet_csk_accept()                                  │ │
  │    │   File: net/ipv4/inet_connection_sock.c            │ │
  │    │                                                     │ │
  │    │   1. Check if non-blocking:                         │ │
  │    │      If O_NONBLOCK and queue empty → return -1     │ │
  │    │      (EAGAIN / EWOULDBLOCK)                         │ │
  │    │                                                     │ │
  │    │   2. Dequeue from icsk_accept_queue:               │ │
  │    │      - Queue filled by kernel TCP handshake code   │ │
  │    │      - Contains fully-established struct sock       │ │
  │    │      - If empty and blocking: sleep on wait queue  │ │
  │    │                                                     │ │
  │    │   3. Returns the new struct sock                    │ │
  │    └────────────────────────────────────────────────────┘ │
  │                                                           │
  │ 4. NEW SOCKET CREATION                                    │
  │    ┌────────────────────────────────────────────────────┐ │
  │    │ Accept returns a pre-built struct sock              │ │
  │    │ Created by kernel during TCP handshake:             │ │
  │    │   sk_state = TCP_ESTABLISHED                        │ │
  │    │   sk_daddr  = client IP                             │ │
  │    │   sk_dport  = client port                           │ │
  │    │   sk_rcv_saddr = server IP                          │ │
  │    │   sk_num    = server port                           │ │
  │    │   Already inserted into ehash (established hash)   │ │
  │    │                                                     │ │
  │    │ The kernel then:                                    │ │
  │    │   - Allocates new struct file for this socket       │ │
  │    │   - Sets file->f_op = socket_file_ops             │ │
  │    │   - Sets file->private_data = new socket           │ │
  │    │   - Allocates NEW file descriptor in process table  │ │
  │    └────────────────────────────────────────────────────┘ │
  │                                                           │
  │ 5. COPY ADDRESS TO USER                                   │
  │    - Copies client's sockaddr back to user space          │
  │    - (Your inet_ntop() call reads this for hostname)      │
  │                                                           │
  │ 6. RETURN                                                │
  │    Returns new fd (e.g., 5, 6, 7...)                      │
  └──────────────────────────────────────────────────────────┘
```

### What's Happening in the Kernel Between `listen()` and `accept()`

Your server is sleeping in `poll()`. Meanwhile, the kernel is doing this automatically:

```
Incoming SYN packet:
  1. NIC interrupt → Driver → IP → TCP layer
  2. TCP lookup: lhash2[destination_port] → find listening socket
  3. Allocate request_sock (struct tcp_request_sock)
  4. Store: client IP, port, ISN, MSS, window scale
  5. State: TCP_SYN_RECV
  6. Send SYN-ACK back to client
  7. Start SYN-RECV timer

ACK comes back:
  1. TCP lookup: ehash[4-tuple] → find request_sock
  2. Verify SYN-ACK was sent (sequence check)
  3. Allocate full struct sock + tcp_sock
  4. Initialize all TCP state:
     - snd_nxt, rcv_nxt, snd_wnd, rcv_wnd
     - Congestion control state
     - Timestamps, SACK, etc.
  5. State: TCP_ESTABLISHED
  6. Push new sock onto icsk_accept_queue
  7. Wake up process waiting in poll()
  8. Remove request_sock from syn_table
```

### The Two Queues of a Listening Socket

```
Listening Socket (TCP_LISTEN)
┌──────────────────────────────────────┐
│                                      │
│  syn_table (hash table):             │   ← half-open connections
│    bucket[hash(saddr,sport,daddr)]   │      (SYN received, waiting ACK)
│      → request_sock1                 │
│      → request_sock2                 │
│                                      │
│  icsk_accept_queue (FIFO list):      │   ← fully established connections
│    [sockA] → [sockB] → [sockC]      │      (ready for accept())
│                                      │
└──────────────────────────────────────┘
```

### Subsystems Touched

| Subsystem | What It Does |
|-----------|-------------|
| Syscall Entry | Validates, dispatches |
| VFS | Allocates new struct file + fd for the new socket |
| Socket Layer | Calls inet_accept |
| TCP Layer | Dequeues from accept queue, returns established sock |
| Process Scheduler | If queue empty and blocking, sleeps until connection arrives |

---

<a name="9-recv"></a>
# 9. Syscall #8: `recv()`

### IRC Server Usage

```cpp
char buff[1024];
ssize_t bytes = recv(client_fd, buff, sizeof(buff) - 1, 0);
```

### Full Kernel Journey

```
  User: recv(client_fd, buff, len, flags)
         │
         ▼
  ┌──────────────────────────────────────────────────────────┐
  │ 1. SYSCALL ENTRY                                          │
  │    ┌────────────────────────────────────────────────────┐ │
  │    │ sys_recvfrom()                                     │ │
  │    │   File: net/socket.c                               │ │
  │    │   - Resolves fd → struct file → struct socket      │ │
  │    │   - Calls socket->ops->recvmsg() = inet_recvmsg() │ │
  │    └────────────────────────────────────────────────────┘ │
  │                                                           │
  │ 2. SOCKET LAYER                                           │
  │    ┌────────────────────────────────────────────────────┐ │
  │    │ inet_recvmsg() → tcp_recvmsg()                     │ │
  │    │   File: net/ipv4/af_inet.c                         │ │
  │    └────────────────────────────────────────────────────┘ │
  │                                                           │
  │ 3. TCP RECEIVE                                            │
  │    ┌────────────────────────────────────────────────────┐ │
  │    │ tcp_recvmsg() (net/ipv4/tcp.c)                     │ │
  │    │   File: net/ipv4/tcp.c                             │ │
  │    │                                                     │ │
  │    │   1. LOCK SOCKET (bh_lock_sock)                     │ │
  │    │      - Acquire socket spinlock (may sleep if locked)│ │
  │    │                                                     │ │
  │    │   2. CHECK RECEIVE QUEUE                            │ │
  │    │      - Lock sk_receive_queue                        │ │
  │    │      - Dequeue sk_buff(s) from queue                │ │
  │    │      - If empty:                                    │ │
  │    │        • Non-blocking → return -EAGAIN              │ │
  │    │        • Blocking → sleep on wait queue             │ │
  │    │                                                     │ │
  │    │   3. COPY DATA TO USER                              │ │
  │    │      - skb_copy_datagram_msg()                      │ │
  │    │      - Copies from sk_buff data → user buffer        │ │
  │    │      - Handles partial reads (if len < skb size)    │ │
  │    │      - Tracks how many bytes copied                 │ │
  │    │                                                     │ │
  │    │   4. CLEANUP                                        │ │
  │    │      - Frees consumed sk_buff(s)                    │ │
  │    │      - Updates rcv_nxt (next expected seq)          │ │
  │    │      - Updates window (rcv_wnd = free buffer space) │ │
  │    │      - May send ACK with updated window             │ │
  │    │      - Unlock socket                                │ │
  │    └────────────────────────────────────────────────────┘ │
  │                                                           │
  │ 4. RETURN                                                │
  │    Returns: number of bytes copied, or -1 on error        │
  └──────────────────────────────────────────────────────────┘
```

### The Asynchronous Receive Path (How Data Gets into the Queue)

Data arrives ASYNCHRONOUSLY — your `recv()` call doesn't drive the receive process:

```
                        HARDWARE INTERRUPT CONTEXT
NIC receives packet → DMA to RAM → IRQ → NAPI poll
    │
    ▼
  Driver: netif_receive_skb(skb)
    │
    ▼
  IP Layer: ip_rcv(skb)
    │  - Checks IP checksum
    │  - Checks destination IP (is it for us?)
    │  - Defragmentation if needed
    │  - Netfilter PREROUTING hook
    ▼
  TCP Layer: tcp_v4_rcv(skb)
    │
    ▼
  tcp_rcv_established(skb) or tcp_rcv_state_process(skb)
    │
    ├── Validate: checksum, seq nums, window
    ├── Handle: ACKs, data, window updates
    ├── If data:
    │     - Allocate skb if needed (or use incoming skb)
    │     - Queue to sk_receive_queue
    │     - Update rcv_nxt
    │     - Call sk_data_ready() → wake up poll()/recv()
    │
    └── If ACK:
          - Update snd_una (acknowledged data)
          - Free acknowledged skbs from write queue
          - Update congestion window
```

### Why `recv()` Is Always Called **After** `poll()`

Your code flow:

```
poll(fds, nfds, -1)      ← BLOCK HERE until something happens
    │
    ▼  (wakes up when data arrives)
    
if (fds[i].revents & POLLIN) {
    recv(client_fd, buff, sizeof(buff), 0);  ← NOW data is guaranteed (or was)
}
```

The kernel guarantees: once `poll()` returns `POLLIN` for a socket, a subsequent `recv()` on that socket will return data **immediately** (unless another thread stole it, but in your single-threaded server, that's not possible).

### Subsystems Touched

| Subsystem | What It Does |
|-----------|-------------|
| Syscall Entry | Copies user buffer pointer, dispatches |
| VFS | Resolves fd to socket |
| Socket Layer | Routes to tcp_recvmsg |
| TCP Layer | Dequeues from receive queue, copies to user, updates TCP state |
| Scheduler | If blocking: parks process until data arrives |

---

<a name="10-send"></a>
# 10. Syscall #9: `send()`

### IRC Server Usage

```cpp
std::string msg = ":server NOTICE Wafae :Hello!\r\n";
send(client_fd, msg.c_str(), msg.size(), 0);
```

### Full Kernel Journey

```
  User: send(client_fd, buffer, len, flags)
         │
         ▼
  ┌──────────────────────────────────────────────────────────┐
  │ 1. SYSCALL ENTRY                                          │
  │    ┌────────────────────────────────────────────────────┐ │
  │    │ sys_sendto() → sock_sendmsg()                      │ │
  │    │   File: net/socket.c                               │ │
  │    │   - Resolves fd → struct file → struct socket      │ │
  │    │   - Builds struct msghdr from user arguments        │ │
  │    │   - Calls socket->ops->sendmsg()                      │ │
  │    │     = inet_sendmsg()                                │ │
  │    └────────────────────────────────────────────────────┘ │
  │                                                           │
  │ 2. SOCKET LAYER                                           │
  │    ┌────────────────────────────────────────────────────┐ │
  │    │ inet_sendmsg() → tcp_sendmsg()                     │ │
  │    │   File: net/ipv4/af_inet.c                         │ │
  │    └────────────────────────────────────────────────────┘ │
  │                                                           │
  │ 3. TCP SEND                                               │
  │    ┌────────────────────────────────────────────────────┐ │
  │    │ tcp_sendmsg() (net/ipv4/tcp.c)                     │ │
  │    │   File: net/ipv4/tcp.c                             │ │
  │    │                                                     │ │
  │    │   1. LOCK SOCKET                                    │ │
  │    │      - Acquire socket lock (bh_lock_sock)           │ │
  │    │                                                     │ │
  │    │   2. COPY FROM USER SPACE                           │ │
  │    │      - Copies data from user buffer into sk_buff(s) │ │
  │    │      - Allocates new sk_buff(s) for the data        │ │
  │    │      - Queues to sk_write_queue                      │ │
  │    │      - May use corking (Nagle's algorithm)           │ │
  │    │                                                     │ │
  │    │   3. CHECK BUFFER SPACE                             │ │
  │    │      - if (sk_write_queue full):                    │ │
  │    │        • Non-blocking → return -EAGAIN              │ │
  │    │        • Blocking → sleep until space available     │ │
  │    │                                                     │ │
  │    │   4. TRIGGER TRANSMIT (if not corked)               │ │
  │    │      - Calls tcp_push() → __tcp_push_pending_frames │ │
  │    │                                                     │ │
  │    │   5. UNLOCK SOCKET                                  │ │
  │    └────────────────────────────────────────────────────┘ │
  │                                                           │
  │ 4. TCP PUSH & TRANSMIT (Still in syscall context)         │
  │    ┌────────────────────────────────────────────────────┐ │
  │    │ __tcp_push_pending_frames()                        │ │
  │    │   - tcp_write_xmit()                                │ │
  │    │     • Build TCP segment(s) from sk_write_queue       │ │
  │    │     • Assign sequence number (snd_nxt)              │ │
  │    │     • Calculate checksum                            │ │
  │    │     • Start retransmit timer                        │ │
  │    │     • Call IP layer:                               │ │
  │    └────────────────────────────────────────────────────┘ │
  │                                                           │
  │ 5. IP LAYER                                               │
  │    ┌────────────────────────────────────────────────────┐ │
  │    │ ip_queue_xmit() / __ip_local_out()                  │ │
  │    │   - Builds IP header (version, IHL, TOS, TTL, etc) │ │
  │    │   - Performs routing lookup (FIB)                   │ │
  │    │   - Determines source IP (if not set)               │ │
  │    │   - Calls Netfilter LOCAL_OUT hook                  │ │
  │    │   - Computes IP checksum                            │ │
  │    │   - Calls neigh_output (ARP resolution if needed)   │ │
  │    └────────────────────────────────────────────────────┘ │
  │                                                           │
  │ 6. NEIGHBOR SUBSYSTEM (ARP)                               │
  │    ┌────────────────────────────────────────────────────┐ │
  │    │ neigh_resolve_output()                              │ │
  │    │   - Looks up destination MAC in ARP cache           │ │
  │    │   - If not in cache: queues packet, sends ARP req  │ │
  │    │   - If in cache: builds Ethernet header              │ │
  │    └────────────────────────────────────────────────────┘ │
  │                                                           │
  │ 7. TRAFFIC CONTROL (qdisc)                                │
  │    ┌────────────────────────────────────────────────────┐ │
  │    │ dev_queue_xmit() → __dev_xmit_skb()                 │ │
  │    │   - Determines TX queue (XPS or hash)               │ │
  │    │   - Enqueues skb into qdisc                        │ │
  │    │   - If qdisc is FIFO and not full:                  │ │
  │    │     • Calls sch_direct_xmit() → ndo_start_xmit()   │ │
  │    │   - If qdisc is full or shaped: queues for later   │ │
  │    └────────────────────────────────────────────────────┘ │
  │                                                           │
  │ 8. DEVICE DRIVER                                          │
  │    ┌────────────────────────────────────────────────────┐ │
  │    │ ndo_start_xmit = driver's transmit function         │ │
  │    │   (e.g., e1000_xmit_frame for Intel e1000 NIC)     │ │
  │    │                                                    │ │
  │    │   - Maps skb data to DMA (if not already mapped)   │ │
  │    │   - Writes to NIC's TX descriptor ring             │ │
  │    │   - Rings NIC's doorbell (register write)           │ │
  │    │   - NIC starts transmitting bits on wire           │ │
  │    └────────────────────────────────────────────────────┘ │
  │                                                           │
  │ 9. RETURN TO USER                                         │
  │    Returns: number of bytes sent (from userspace)         │
  │    Note: This is the # of bytes copied to kernel,        │
  │    NOT necessarily the # of bytes actually on the wire!  │
  └──────────────────────────────────────────────────────────┘
```

### The "send() Already Returned But Data Isn't Sent Yet" Problem

This is a critical understanding:

```
send() returns immediately ──► data is in the kernel send buffer
                                    │
                                    │ (not necessarily on the wire yet!)
                                    ▼
                              sk_write_queue
                                    │
                           TCP decides when to send
                                    │
                         ┌──────────┴──────────┐
                         │                     │
                    Nagle says:          Window is:
                    "wait for more"     "full - wait for ACK"
                         │                     │
                         └──────────┬──────────┘
                                    ▼
                              Actually transmitted
```

Your IRC server's `send()` returns success meaning "the kernel accepted your bytes." The bytes may still be sitting in the send buffer waiting for:

- Nagle's algorithm to flush
- The receiver's window to open
- The retransmit timer to expire
- The congestion window to allow more

### Subsystems Touched

| Subsystem | What It Does |
|-----------|-------------|
| Syscall Entry | Copies user data reference, dispatches |
| VFS | Resolves fd to socket |
| Socket Layer | Routes to tcp_sendmsg |
| TCP Layer | Copies to sk_write_queue, segments, builds TCP headers |
| IP Layer | Routes packet, builds IP headers, Netfilter |
| Neighbor (ARP) | Resolves MAC address for next-hop |
| Traffic Control | Queues packet for scheduling |
| Device Driver | Maps to DMA, writes to NIC ring buffer |
| NIC Hardware | Transmits bits on wire |

---

<a name="11-close"></a>
# 11. Syscall #10: `close()`

### IRC Server Usage

```cpp
close(client_fd);    // when a client disconnects
close(server_fd);    // when the server shuts down (~Server destructor)

// Also in a loop in ~Server:
for (size_t i = 0; i < fds.size(); i++)
    close(fds[i].fd);
```

### Full Kernel Journey (Connected Socket)

```
  User: close(fd)
         │
         ▼
  ┌──────────────────────────────────────────────────────────┐
  │ 1. SYSCALL ENTRY                                          │
  │    ┌────────────────────────────────────────────────────┐ │
  │    │ sys_close()                                        │ │
  │    │   File: fs/open.c                                  │ │
  │    │   - Gets struct file from fd table                  │ │
  │    │   - Removes fd from process fd table                │ │
  │    │   - Decrements file refcount                       │ │
  │    │   - If refcount reaches 0: calls file->f_op->release│ │
  │    │     which is sock_close() in socket_file_ops        │ │
  │    └────────────────────────────────────────────────────┘ │
  │                                                           │
  │ 2. SOCKET RELEASE                                         │
  │    ┌────────────────────────────────────────────────────┐ │
  │    │ sock_close() → __sock_release()                     │ │
  │    │   - Calls socket->ops->release() = inet_release() │ │
  │    └────────────────────────────────────────────────────┘ │
  │                                                           │
  │ 3. TCP DISCONNECT                                         │
  │    ┌────────────────────────────────────────────────────┐ │
  │    │ inet_release() → tcp_close()                       │ │
  │    │   File: net/ipv4/af_inet.c / tcp.c                │ │
  │    │                                                     │ │
  │    │   tcp_close() does:                                 │ │
  │    │                                                     │ │
  │    │   1. FLUSH SEND BUFFER                              │ │
  │    │      - Transmit any remaining queued data          │ │
  │    │                                                     │ │
  │    │   2. START GRACEFUL CLOSE (FIN)                     │ │
  │    │      - sk->sk_state = TCP_FIN_WAIT1                 │ │
  │    │      - Send FIN segment                             │ │
  │    │      - Recalculate write_allowed: false             │ │
  │    │      - tcp_send_fin()                               │ │
  │    │                                                     │ │
  │    │   OR if SO_LINGER with zero timeout:               │ │
  │    │      - sk->sk_state = TCP_CLOSE                     │ │
  │    │      - Send RST segment (abort)                     │ │
  │    │      - Skip graceful shutdown                       │ │
  │    └────────────────────────────────────────────────────┘ │
  │                                                           │
  │ 4. RESOURCE CLEANUP                                       │
  │    ┌────────────────────────────────────────────────────┐ │
  │    │ tcp_clear_xmit_timers()                             │ │
  │    │   - Deletes retransmit timer                        │ │
  │    │   - Deletes keepalive timer                         │ │
  │    │   - Deletes delayed ACK timer                       │ │
  │    │   - Deletes zero-window probe timer                 │ │
  │    │                                                     │ │
  │    │ sock_put() → sk_free() → __sk_free()               │ │
  │    │   - Decrements sock refcount                        │ │
  │    │   - If refcount reaches 0:                          │ │
  │    │     • Calls sk_prot->destroy() (TCP cleanup)        │ │
  │    │     • Frees sk_buff(s) in all queues                │ │
  │    │     • Removes from ehash and bhash                  │ │
  │    │     • Frees struct sock + tcp_sock memory           │ │
  │    │                                                     │ │
  │    │ sock_release() continues:                           │ │
  │    │   - Frees struct socket memory                      │ │
  │    │   - Frees struct file memory                        │ │
  │    └────────────────────────────────────────────────────┘ │
  │                                                           │
  │ 5. RETURN                                                │
  │    0 on success, -1 on error                             │
  └──────────────────────────────────────────────────────────┘
```

### The TCP State Machine on Close

```
ESTABLISHED
    │  close() called
    ▼
FIN_WAIT_1    ──send FIN──►    CLOSE_WAIT   (remote receives FIN)
    │                              │
    │  (wait for remote's ACK)    remote calls close()
    ▼                              ▼
FIN_WAIT_2    ◄──send ACK──     LAST_ACK
    │                              │
    │  (wait for remote's FIN)    (wait for ACK of FIN)
    ▼                              ▼
TIME_WAIT     ◄──send FIN───    CLOSED
    │   (2 * MSL = ~60s)
    ▼
CLOSED
```

Your server doesn't call `shutdown()` — it just calls `close()`. The kernel handles the FIN handshake automatically.

### Close on the Listening Socket (Server Shutdown)

```
close(server_fd):
  1. Remove from lhash2 (no more incoming connections)
  2. Abort any pending SYN queue entries (send RST)
  3. Abort any established connections in accept queue (send RST)
  4. Free resources
```

### Subsystems Touched

| Subsystem | What It Does |
|-----------|-------------|
| Syscall Entry | Removes fd from process table, decrements refcount |
| VFS | Calls file_operations->release |
| Socket Layer | Routes to TCP close |
| TCP Layer | FIN handshake, state transitions, timer cleanup |
| INET Hash Tables | Removes from ehash, bhash, lhash2 |
| Memory Manager | Frees struct sock, socket, file, sk_buffs |
| Process Scheduler | Wakes up any processes waiting on this socket |

---

<a name="12-signal"></a>
# 12. Syscall #11: `signal()` / `sigaction()`

### IRC Server Usage

```cpp
signal(SIGINT,  Server::signal_handler);    // Ctrl+C → stop server
signal(SIGQUIT, Server::signal_handler);    // Ctrl+\ → stop server
signal(SIGPIPE, SIG_IGN);                   // Ignore broken pipe errors
```

### What's Really Happening

`signal()` is a glibc wrapper. The actual kernel syscall is `sigaction()` or `rt_sigaction()`.

### Full Kernel Journey

```
  User: signal(SIGINT, handler_function)  →  sigaction(SIGINT, &act, NULL)
         │
         ▼
  ┌──────────────────────────────────────────────────────────┐
  │ 1. SYSCALL ENTRY                                          │
  │    ┌────────────────────────────────────────────────────┐ │
  │    │ sys_rt_sigaction()                                 │ │
  │    │   File: kernel/signal.c                            │ │
  │    │   - Copies struct sigaction from user space         │ │
  │    │   - Validates signal number (1-31 or 32-64)         │ │
  │    │   - SIGKILL and SIGSTOP cannot be caught/ignored   │ │
  │    └────────────────────────────────────────────────────┘ │
  │                                                           │
  │ 2. PROCESS SIGNAL TABLE                                   │
  │    ┌────────────────────────────────────────────────────┐ │
  │    │ Do to current process (current->sighand)           │ │
  │    │                                                    │ │
  │    │ Each process has a struct sighand_struct:          │ │
  │    │   sighand->action[SIGINT] = { handler, flags, ... }│ │
  │    │   sighand->action[SIGQUIT] = { handler, flags, ...}│ │
  │    │   sighand->action[SIGPIPE] = SIG_IGN               │ │
  │    │                                                    │ │
  │    │ Changes:                                           │ │
  │    │   - Writes new sigaction into the process's table  │ │
  │    │   - If SA_SIGINFO is set, handler takes siginfo_t  │ │
  │    └────────────────────────────────────────────────────┘ │
  │                                                           │
  │ 3. SIGNAL DELIVERY (When Signal Arrives)                  │
  │    ┌────────────────────────────────────────────────────┐ │
  │    │ Example: User presses Ctrl+C                       │ │
  │    │                                                    │ │
  │    │ 1. TTY layer detects Ctrl+C                        │ │
  │    │ 2. TTY driver sends SIGINT to foreground process   │ │
  │    │ 3. Kernel signal delivery:                         │ │
  │    │    a. Sets TIF_SIGPENDING flag on the process      │ │
  │    │    b. On return from interrupt/ syscall:           │ │
  │    │       - Check TIF_SIGPENDING                       │ │
  │    │       - Call do_signal()                           │ │
  │    │       - Fetch sigaction for SIGINT                 │ │
  │    │       - If handler exists:                         │ │
  │    │         • Modify user stack to call handler        │ │
  │    │         • On return from signal: restore context    │ │
  │    │       - If SIG_DFL: default action (terminate)    │ │
  │    │       - If SIG_IGN: silently discard               │ │
  │    └────────────────────────────────────────────────────┘ │
  │                                                           │
  │ 4. SPECIAL: SIGPIPE                                       │
  │    ┌────────────────────────────────────────────────────┐ │
  │    │ signal(SIGPIPE, SIG_IGN);                          │ │
  │    │                                                    │ │
  │    │ SIGPIPE is generated by the kernel when:           │ │
  │    │   - send() / write() on a socket that received RST │ │
  │    │                                                    │ │
  │    │ Without SIG_IGN: send() returns -1 + SIGPIPE kills │ │
  │    │ With SIG_IGN: send() just returns -1 (EPIPE)       │ │
  │    │                                                    │ │
  │    │ Why this matters for IRC:                          │ │
  │    │   - Client disconnects unexpectedly                │ │
  │    │   - Server tries to send() to that client          │ │
  │    │   - Without SIG_IGN → SIGPIPE kills server         │ │
  │    │   - With SIG_IGN → send() returns -1, server lives  │ │
  │    └────────────────────────────────────────────────────┘ │
  └──────────────────────────────────────────────────────────┘
```

### Subsystems Touched

| Subsystem | What It Does |
|-----------|-------------|
| Syscall Entry | Copies sigaction struct, validates |
| Process Manager | Writes signal handler to process's sighand table |
| TTY Driver | Generates SIGINT on Ctrl+C (for interactive processes) |
| Signal Delivery | On context switch: checks pending signals, runs handlers |
| Socket Layer | Generates SIGPIPE on write to closed socket |

---

<a name="13-write-read"></a>
# 13. Syscall #12: `write()` / `read()` (Implied)

Your code also implicitly uses `write()` and `read()` — these are the underlying syscalls that `send()` and `recv()` are built on.

For sockets, `read(fd, buf, n)` is **identical** to `recv(fd, buf, n, 0)`, and `write(fd, buf, n)` is **identical** to `send(fd, buf, n, 0)`.

The VFS routes them through the same path:

```
read()  → file->f_op->read_iter → sock_read_iter → sock_recvmsg → tcp_recvmsg
write() → file->f_op->write_iter → sock_write_iter → sock_sendmsg → tcp_sendmsg
```

---

<a name="14-reference-table"></a>
# 14. Complete Reference Table

| # | Syscall | IRC Usage | VFS | Socket | sock | TCP | IP | qdisc | Driver | NIC | Sched |
|---|---------|-----------|:---:|:-----:|:----:|:---:|:--:|:-----:|:---:|:---:|:---:|
| 1 | `socket()` | Create server socket | ✅ | ✅ | ✅ | ✅ | - | - | - | - | ✅ * |
| 2 | `setsockopt()` | SO_REUSEADDR | ✅ | ✅ | ✅ | - | - | - | - | - | - |
| 3 | `fcntl()` | O_NONBLOCK | ✅ | - | - | - | - | - | - | - | - |
| 4 | `bind()` | Assign port | ✅ | ✅ | ✅ | ✅ | - | - | - | - | - |
| 5 | `listen()` | Start listening | ✅ | ✅ | ✅ | ✅ | - | - | - | - | - |
| 6 | `poll()` | Event loop | ✅ | ✅ | ✅ | ✅ | - | - | - | - | ✅ |
| 7 | `accept()` | New client | ✅ | ✅ | ✅ | ✅ | - | - | - | - | ✅ |
| 8 | `recv()` | Read commands | ✅ | ✅ | ✅ | ✅ | ✅ | - | - | - | ✅ |
| 9 | `send()` | Send responses | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | - |
| 10 | `close()` | Cleanup | ✅ | ✅ | ✅ | ✅ | - | - | - | - | ✅ |
| 11 | `signal()` | SIGINT/QUIT/PIPE | - | - | - | - | - | - | - | - | ✅ |

> ✅ * = The process scheduler allocates a new file descriptor in `socket()`.

> Note: `send()` is the only syscall that goes all the way down to the NIC because it's the only one that needs to **transmit data**. All other syscalls end at the TCP or socket layer.

---

<a name="15-data-structures"></a>
# 15. Kernel Data Structures Quick Reference

### Core Structures (In Order of the Journey)

```
┌────────────────────────────────────────────────────────────────┐
│                        USER SPACE                               │
│  int fd = 3;  (just a number!)                                 │
└────────────────────────────────────────────────────────────────┘
         │
         ▼  syscall
┌────────────────────────────────────────────────────────────────┐
│  struct file (VFS)              fs/file_table.c               │
│  ┌──────────────────────────────┐                              │
│  │ f_flags = O_NONBLOCK         │  ← set by fcntl()           │
│  │ f_op    = socket_file_ops    │  ← function table           │
│  │ private_data = struct socket │  ← the real socket          │
│  │ f_count = 1                  │  ← refcount                 │
│  └──────────────────────────────┘                              │
└────────────────────────────────────────────────────────────────┘
         │
         ▼
┌────────────────────────────────────────────────────────────────┐
│  struct socket (Socket Layer)     include/linux/net.h          │
│  ┌──────────────────────────────┐                              │
│  │ state  = SS_CONNECTED        │  or SS_UNCONNECTED          │
│  │ type   = SOCK_STREAM         │  ← TCP                      │
│  │ ops    = &inet_stream_ops    │  ← function table           │
│  │ sk     = struct sock *       │  ← protocol-specific sock   │
│  │ file   = struct file *       │  ← backlink to VFS          │
│  └──────────────────────────────┘                              │
└────────────────────────────────────────────────────────────────┘
         │
         ▼
┌────────────────────────────────────────────────────────────────┐
│  struct sock (Network Core)     include/net/sock.h             │
│  ┌──────────────────────────────┐                              │
│  │ sk_state = TCP_ESTABLISHED   │  or TCP_LISTEN, TCP_CLOSE   │
│  │ sk_prot  = &tcp_prot         │                              │
│  │ sk_rcvbuf = 131072           │  ← receive buffer size       │
│  │ sk_sndbuf = 131072           │  ← send buffer size          │
│  │ sk_receive_queue             │  ← list of incoming data     │
│  │ sk_write_queue               │  ← list of outgoing data     │
│  │ sk_rcv_saddr                 │  ← local IP (bind)           │
│  │ sk_daddr                     │  ← remote IP                 │
│  │ sk_num                       │  ← local port                │
│  │ sk_dport                     │  ← remote port               │
│  │ sk_sleep                     │  ← wait queue (for poll)    │
│  │ sk_data_ready                │  ← callback (wakes reader)  │
│  └──────────────────────────────┘                              │
└────────────────────────────────────────────────────────────────┘
         │
         ▼
┌────────────────────────────────────────────────────────────────┐
│  struct inet_sock (INET layer)   include/net/inet_sock.h       │
│  ┌──────────────────────────────┐                              │
│  │ sk = (inherits struct sock) │                              │
│  │ inet_saddr  = 0.0.0.0       │  ← source IP                 │
│  │ inet_sport  = htons(6667)   │  ← source port (network byte)│
│  │ inet_daddr  = 10.0.0.42     │  ← destination IP            │
│  │ inet_dport  = htons(51000)  │  ← destination port          │
│  │ inet_opt    = IP options    │  ← (rarely used)             │
│  └──────────────────────────────┘                              │
└────────────────────────────────────────────────────────────────┘
         │
         ▼
┌────────────────────────────────────────────────────────────────┐
│  struct tcp_sock (TCP layer)    include/net/tcp.h              │
│  ┌──────────────────────────────┐                              │
│  │ inet = (inherits inet_sock) │                              │
│  │ snd_nxt = 100               │  ← next sequence to send     │
│  │ rcv_nxt = 200               │  ← next sequence expected    │
│  │ snd_wnd = 65535             │  ← send window (flow ctl)    │
│  │ rcv_wnd = 65535             │  ← receive window            │
│  │ srtt_us = 10000             │  ← smoothed RTT (usecs)      │
│  │ mss_cache = 1460            │  ← max segment size          │
│  │ snd_cwnd  = 10              │  ← congestion window         │
│  │ tcp_header_len = 20         │  ← TCP header size (no opts) │
│  └──────────────────────────────┘                              │
└────────────────────────────────────────────────────────────────┘
         │
         ▼
┌────────────────────────────────────────────────────────────────┐
│  struct sk_buff (Packet)        include/linux/skbuff.h         │
│  ┌──────────────────────────────┐                              │
│  │ head → ┌──────────────────┐  │                              │
│  │ data → │ IP header        │  │                              │
│  │        │ TCP header       │  │                              │
│  │        │ payload          │  │                              │
│  │ tail → └──────────────────┘  │                              │
│  │ sk    = &sock              │  │                              │
│  │ len   = payload_size      │  │                              │
│  │ cb[]  = TCP control block  │  │                              │
│  │ dev   = net_device *      │  │                              │
│  └──────────────────────────────┘                              │
└────────────────────────────────────────────────────────────────┘

    sk_buff is THE packet. It travels through:
    TCP → IP → qdisc → Driver → NIC (on send)
    NIC → Driver → IP → TCP → socket (on receive)
```

### Special Structures for Listening Sockets

```
struct inet_connection_sock (for TCP_LISTEN state)
  ┌──────────────────────────────────────┐
  │ icsk_accept_queue:                    │
  │   • qlen = current queue length       │
  │   • max_qlen = backlog (your 10)     │
  │   • queue = FIFO of established socks │
  │ syn_table: size = max_qlen           │
  │   hash of request_sock entries        │
  └──────────────────────────────────────┘

struct request_sock (SYN_RECV state)
  ┌──────────────────────────────────────┐
  │ saddr  = client IP                   │
  │ daddr  = server IP                   │
  │ sport  = client port                 │
  │ dport  = server port                 │
  │ rcv_isn = server's initial seq num   │
  │ snt_isn = client's initial seq num   │
  │ mss    = client's MSS option         │
  │ wscale = window scale option         │
  └──────────────────────────────────────┘
```

### Hash Tables Quick Reference

| Hash Table | Content | Used By | Purpose |
|------------|---------|---------|---------|
| **bhash** | Bound sockets | `bind()` | Check port conflicts |
| **lhash2** | Listening sockets | Incoming SYN | Find listening socket by port |
| **ehash** | Established sockets | Incoming data | Find connection by 4-tuple |
| **syn_table** | Half-open connections | Three-way handshake | Track SYNs awaiting ACK |

---

# Appendix: The Full Timeline of Your IRC Server

```
BOOT:
  socket(AF_INET, SOCK_STREAM, 0)       → fd=3 (TCP_CLOSE)
  setsockopt(fd, SO_REUSEADDR, 1)       → sk_reuse = allowed
  fcntl(fd, F_SETFL, O_NONBLOCK)        → non-blocking mode
  bind(fd, port 6667)                   → inserted into bhash
  listen(fd, 10)                        → TCP_LISTEN, inserted into lhash2

EVENT LOOP (repeats forever):
  poll(fds, -1)          ────┬─── sleeping in kernel (wait queue)
                             │
  [NEW CONNECTION]:          │  ← kernel completes TCP handshake
    poll() returns POLLIN   │     pushes new sock to accept queue
    accept(fd)               │  → new fd=5 (TCP_ESTABLISHED)
    fcntl(fd5, O_NONBLOCK)  │  → non-blocking
                             │
  [DATA ARRIVES]:           │  ← NIC → Driver → IP → TCP
    poll() returns POLLIN   │     data in sk_receive_queue
    recv(fd5, buff, 1024)   │  → "NICK alice\r\n"
    send(fd5, response)     │  → copy to sk_write_queue, transmit
                             │
  [CLIENT DISCONNECTS]:     │  ← FIN received (or RST)
    recv(fd5) → 0           │  → close(fd5)
    close(fd5)              │  → TCP FIN_WAIT1 → FIN_WAIT2 → TIME_WAIT
                            │     (kernel handles this asynchronously)
                            │
  [SERVER SHUTDOWN]:
    Ctrl+C → SIGINT
    g_running = 0
    loop: close(all fds)    │  → FIN to all clients
    close(server_fd)        │  → removed from lhash2
                            │  → actual process exit
```

---

> **Written for 42 ft_irc students.** Understanding this journey transforms networking from "magic function calls" into a clear, layered system. Every syscall in your project is just asking a specific kernel subsystem to do its one job.

> **Key insight to remember:** When your server is idle — sleeping in `poll()` — the kernel is *still running*. Interrupts are firing, SoftIRQs are processing, TCP timers are ticking, ACKs are being generated automatically. Your server only wakes up when there's actual work to do.
