# ⚡ Linux Kernel Journey — One-Page Cheat Sheet (ft_irc)

---

## 1. THE 9 KERNEL LAYERS

```
   USER                 Your IRC Server (socket, bind, listen, poll, accept, send, recv, close...)
    │                          syscall instruction (trap to Ring 0)
    ▼
 ┌───┐  1. SYSCALL ENTRY      entry.S: save regs, route to sys_*
 ├───┤  2. VFS                struct file, socket_file_ops, fd table
 ├───┤  3. SOCKET LAYER (BSD) struct socket, proto_ops = inet_stream_ops
 ├───┤  4. NETWORK CORE (sock) struct sock, sk_receive/sk_write queues
 ├───┤  5. TCP LAYER          struct tcp_sock: seq nums, windows, retransmit
 ├───┤  6. IP LAYER           sk_buff: routing, headers, netfilter
 ├───┤  7. TRAFFIC CONTROL    qdisc: packet scheduling (pfifo_fast)
 ├───┤  8. DEVICE DRIVER      struct net_device, ndo_start_xmit
 └───┘  9. NIC HARDWARE       DMA rings, PHY → bits on wire
```

**Receive path:** flows **up** in reverse: `NIC → Driver → IP → TCP → sock → poll() sleeps → recv()`

---

## 2. SYSCALL DEPTH TABLE

| Syscall | Your Code | VFS | Socket | sock | TCP | IP | Qdisc | Driver | NIC | Sched |
|---------|-----------|:---:|:-----:|:----:|:---:|:--:|:-----:|:-----:|:---:|:----:|
| **socket** | `socket(AF_INET, SOCK_STREAM, 0)` | ✅ | ✅ | ✅ | ✅ | - | - | - | - | ✅ |
| **setsockopt** | `setsockopt(fd, SOL_SOCKET, SO_REUSEADDR)` | ✅ | ✅ | ✅ | - | - | - | - | - | - |
| **fcntl** | `fcntl(fd, F_SETFL, O_NONBLOCK)` | ✅ | - | - | - | - | - | - | - | - |
| **bind** | `bind(fd, &addr, sizeof(addr))` | ✅ | ✅ | ✅ | ✅ | - | - | - | - | - |
| **listen** | `listen(fd, 10)` | ✅ | ✅ | ✅ | ✅ | - | - | - | - | - |
| **poll** | `poll(fds, nfds, -1)` | ✅ | ✅ | ✅ | ✅ | - | - | - | - | ✅ |
| **accept** | `accept(fd, &addr, &len)` | ✅ | ✅ | ✅ | ✅ | - | - | - | - | ✅ |
| **recv** | `recv(fd, buf, 1024, 0)` | ✅ | ✅ | ✅ | ✅ | ✅ | - | - | - | ✅ |
| **send** | `send(fd, msg, len, 0)` | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ | - |
| **close** | `close(fd)` | ✅ | ✅ | ✅ | ✅ | - | - | - | - | ✅ |
| **signal** | `signal(SIGINT, handler)` | - | - | - | - | - | - | - | - | ✅ |

> `send()` is the **only** syscall that hits every layer down to the NIC. Everything else stops at TCP or higher.

---

## 3. KEY STRUCTURES (in journey order)

```
fd (int) → struct file → struct socket → struct sock → struct tcp_sock → sk_buff → net_device → NIC
```

| Structure | File | Key Fields | Created By |
|-----------|------|------------|------------|
| `struct file` | `include/linux/fs.h` | `f_op`, `private_data`, `f_flags` | `socket()` |
| `struct socket` | `include/linux/net.h` | `ops=inet_stream_ops`, `sk` | `socket()` |
| `struct sock` | `include/net/sock.h` | `sk_state`, `sk_receive_queue`, `sk_write_queue`, `sk_data_ready` | `socket()` |
| `struct inet_sock` | `include/net/inet_sock.h` | inherits sock + `inet_saddr`, `inet_sport`, `inet_daddr`, `inet_dport` | `socket()` |
| `struct tcp_sock` | `include/net/tcp.h` | inherits inet + `snd_nxt`, `rcv_nxt`, `snd_wnd`, `snd_cwnd`, `mss_cache` | `socket()` |
| `struct sk_buff` | `include/linux/skbuff.h` | `head/data/tail` pointers, `sk`, `dev`, `len` | On each send/recv |
| `struct net_device` | `include/linux/netdev.h` | `name`, `netdev_ops`, `flags` | Driver init |

---

## 4. HASH TABLES

| Table | Content | Used By |
|-------|---------|---------|
| **bhash** | Sockets bound to a port | `bind()` — check port conflicts |
| **lhash2** | Listening sockets | Incoming SYN — find socket by port |
| **ehash** | Established connections | Incoming data — find socket by 4-tuple |
| **syn_table** | Half-open (SYN_RECV) connections | Three-way handshake tracking |

---

## 5. TCP STATE MACHINE (your server's states)

```
socket()  ──► TCP_CLOSE
bind()          │
listen()   ──► TCP_LISTEN      ← server socket, stays here forever
                  │
                  │  (kernel handles 3-way handshake: SYN→SYN-ACK→ACK)
                  ▼
accept()   ──► TCP_ESTABLISHED ← client socket, for send/recv
                  │
close()    ──► FIN_WAIT_1 → FIN_WAIT_2 → TIME_WAIT (60s) → CLOSED
                  │
              (or RST if abortive close)
```

---

## 6. poll() WAIT QUEUE MECHANISM

```
 Your IRC Server            Socket's Wait Queue
      │                           │
      │  poll(fds, -1)            │
      ├────register wait_entry───►│
      │                           │
      │  [sleeping...]            │
      │                           │
      │         DATA ARRIVES      │  (SoftIRQ context)
      │         sk_data_ready()   │
      │◄────wake up────────────── │
      │                           │
      │  poll() returns POLLIN    │
      │  recv() → reads data      │
```

**Key insight:** Your server sleeps in the scheduler while waiting. The kernel's TCP stack processes incoming packets in SoftIRQ context and wakes your process only when data is ready.

---

## 7. SEND VS RECV FLOW (mini version)

```
SEND:                              RECV:
user buf → sk_write_queue          NIC → DMA → IRQ → NAPI poll
    → TCP segment (seq#)               → IP (routing, defrag)
    → IP header + route                 → TCP (checksum, seq, ordering)
    → qdisc (schedule)                  → sk_receive_queue
    → driver (DMA ring)                 → sk_data_ready() wakes poll()
    → NIC (wire)                        → recv() copies to user buf
```

**Critical:** `send()` returning ≠ data on wire. It means "kernel accepted your bytes into the send buffer." The actual transmission is asynchronous.

---

## 8. YOUR IRC SERVER TIMELINE

```
BOOT:
  socket()       → fd=3, TCP_CLOSE, 64KB send/recv buffers
  setsockopt()   → sk_reuse = true (survive restart)
  fcntl(O_NONBLOCK) → never block on a single client
  bind(6667)     → inserted into bhash[port]
  listen(10)     → TCP_LISTEN, inserted into lhash2, accept queue ready

EVENT LOOP:
  poll(fds, -1)  ──── Zzz (kernel still running: timers, ACKs, etc.)
                      │
  [NEW CLIENT]        │  ← kernel does 3-way handshake, pushes to accept queue
  poll() → POLLIN     │
  accept() → fd=5     │  TCP_ESTABLISHED, inserted into ehash
  fcntl(O_NONBLOCK)   │
                      │
  [CLIENT SENDS]      │  ← NIC → SoftIRQ → TCP → sk_receive_queue
  poll() → POLLIN     │  → sk_data_ready() wakes us
  recv(fd5)           │  → "NICK alice\r\n"
  send(fd5, reply)    │  → copied to sk_write_queue → TCP → IP → qdisc → NIC
                      │
  [DISCONNECT]        │  ← FIN or RST received
  recv() → 0          │
  close(fd5)          │  → FIN_WAIT1 → FIN_WAIT2 → TIME_WAIT (kernel handles)
                      │
  [SHUTDOWN]          │
  Ctrl+C → SIGINT     │
  close(all fds)      │
  close(server_fd)    │  removed from lhash2, kernel sends RST to pending
```

---

## 9. KEY MENTAL MODELS (remember these)

| # | Concept | Meaning |
|---|---------|---------|
| 1 | **fd is just a number** | The kernel resolves it to `struct file → struct socket → struct sock` |
| 2 | **`send()` returns ≠ delivered** | Return means "buffered in kernel," not "received by client" |
| 3 | **`recv()` is passive** | Data arrives asynchronously via interrupts. `recv()` just copies from kernel buffer |
| 4 | **`poll()` is parking** | You register interest, kernel parks you, wakes you when work is ready |
| 5 | **`accept()` is popping** | Kernel already completed the TCP handshake — you just dequeue the result |
| 6 | **Two socket types** | Listener (TCP_LISTEN, 1 per server) vs Connected (TCP_ESTABLISHED, N per client) |
| 7 | **`close()` triggers FIN** | Kernel handles the graceful shutdown state machine automatically |
| 8 | **SIGPIPE = broken client** | `signal(SIGPIPE, SIG_IGN)` prevents writing to a dead client from killing the server |
| 9 | **O_NONBLOCK is essential** | Without it, one slow client blocks the entire single-threaded event loop |
| 10 | **Kernel never sleeps** | When your server is idle, the kernel is still processing interrupts, timers, and ACKs |

---

> ⚡ **The golden chain:** `fd → VFS → socket → sock → TCP → IP → qdisc → driver → NIC`
