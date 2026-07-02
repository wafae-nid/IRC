# Listening Socket vs Connected Socket — How They Work Together in an IRC Server

---

## The Core Distinction

An IRC server uses **two fundamentally different kinds of sockets**:

| | **Listening Socket** | **Connected Socket** |
|---|---|---|
| **Purpose** | Wait for new clients | Communicate with one connected client |
| **Created by** | `socket()` + `bind()` + `listen()` | `accept()` |
| **How many** | **One** per IRC server (usually) | **Many** — one per connected client |
| **State** | `LISTEN` | `ESTABLISHED` |
| **Used for `send()`/`recv()`** | ❌ Never | ✅ Yes |
| **Used for `accept()`** | ✅ Yes | ❌ Never |
| **Lifetime** | Entire server lifetime | From connection until client disconnects |
| **Closes when client leaves** | ❌ No | ✅ Yes |

```text
                     ┌──────────────────────┐
                     │   Listening Socket    │
                     │   fd = 3             │
                     │   State = LISTEN     │
                     │   Port = 6667        │
                     └──────────┬───────────┘
                                │
                    ┌───────────┴───────────┐
                    │       accept()         │
                    └───────────┬───────────┘
                                │
          ┌─────────────────────┼─────────────────────┐
          │                     │                     │
          ▼                     ▼                     ▼
┌──────────────────┐  ┌──────────────────┐  ┌──────────────────┐
│ Connected Socket  │  │ Connected Socket  │  │ Connected Socket  │
│ fd = 5            │  │ fd = 6            │  │ fd = 7            │
│ State = ESTABLISHED│  │ State = ESTABLISHED│  │ State = ESTABLISHED│
│ Client: Alice      │  │ Client: Bob       │  │ Client: Charlie   │
└──────────────────┘  └──────────────────┘  └──────────────────┘
```

---

## 1. The Listening Socket — The Doorman

### Creation

```cpp
// Step 1: Create a socket
int server_fd = socket(AF_INET, SOCK_STREAM, 0);

// Step 2: Allow address reuse (avoids EADDRINUSE on restart)
int opt = 1;
setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

// Step 3: Bind to port 6667 on all interfaces
sockaddr_in addr;
addr.sin_family = AF_INET;
addr.sin_port = htons(6667);
addr.sin_addr.s_addr = INADDR_ANY;
bind(server_fd, (struct sockaddr *)&addr, sizeof(addr));

// Step 4: Start listening
listen(server_fd, SOMAXCONN);
```

After `listen()`, the kernel registers this socket in its internal listening table:

```text
Kernel Listening Table

Port 6667 ───► server_fd (LISTEN)

```

### The Listening Socket's Only Job

1. **Wait** for incoming TCP SYN packets (connection requests).
2. **Let the kernel handle** the TCP three-way handshake automatically.
3. **Queue** completed connections in the accept queue.
4. **Hand off** completed connections via `accept()`.

### What the Listening Socket Never Does

- ❌ Never sends or receives IRC messages.
- ❌ Never stores client information (nickname, username).
- ❌ Never joins channels.
- ❌ Never changes state — it stays `LISTEN` from `listen()` until `close()`.

### Event Loop Integration

```cpp
struct pollfd fds[MAX_CLIENTS + 1];
int nfds = 0;

// Add the listening socket
fds[nfds].fd     = server_fd;
fds[nfds].events = POLLIN;  // Only POLLIN, never POLLOUT
nfds++;

while (g_running)
{
    int ready = poll(fds, nfds, -1);

    for (int i = 0; i < nfds; i++)
    {
        if (fds[i].revents & POLLIN)
        {
            if (fds[i].fd == server_fd)
            {
                // ⭐ Listening socket is readable → new client waiting
                int client_fd = accept(server_fd, NULL, NULL);
                add_client(client_fd);
            }
            else
            {
                // Connected socket is readable → data from client
                handle_client_data(fds[i].fd);
            }
        }
    }
}
```

**Key insight:** The listening socket is readable (`POLLIN`) when a new connection is waiting in the accept queue — not when data has arrived. The kernel treats "a completed TCP handshake" the same as "data arrived."

---

## 2. The Connected Socket — The Client's Private Line

### Creation

```cpp
sockaddr_in client_addr;
socklen_t len = sizeof(client_addr);

int client_fd = accept(server_fd, (struct sockaddr *)&client_addr, &len);
```

Each call to `accept()` creates a **brand new socket**:

```text
server_fd  = 3   ← Stays LISTEN forever
                     │
accept() → fd = 5   ← Client A
accept() → fd = 6   ← Client B
accept() → fd = 7   ← Client C
```

### Kernel State

```text
Socket fd = 5
┌──────────────────────────────┐
│ State     = ESTABLISHED       │
│ Local IP  = 192.168.1.20     │
│ Local Port = 6667             │
│ Remote IP  = 10.0.0.42       │
│ Remote Port = 51000           │
│ Send Buffer   → data waiting to be sent      │
│ Receive Buffer ← data received from client   │
└──────────────────────────────┘
```

The 4-tuple `(local IP, local port, remote IP, remote port)` uniquely identifies this connection.

### What a Connected Socket Does

1. **Receives** IRC commands from the client (`NICK`, `USER`, `JOIN`, `PRIVMSG`, etc.).
2. **Sends** IRC responses and messages to the client.
3. **Is tracked** by the server in its client list.
4. **Is closed** when the client disconnects (`QUIT` or timeout).

### Event Loop Integration

```cpp
// Connected sockets are added to the pollfd array too
fds[nfds].fd     = client_fd;
fds[nfds].events = POLLIN | POLLOUT;  // Can check both
nfds++;
```

```cpp
// Handling a connected socket
if (fds[i].revents & POLLIN)
{
    // Client sent data — read the IRC command
    char buf[512];
    ssize_t n = recv(fds[i].fd, buf, sizeof(buf) - 1, 0);

    if (n <= 0)
    {
        // Client disconnected or error → remove
        remove_client(fds[i].fd);
        close(fds[i].fd);
    }
    else
    {
        buf[n] = '\0';
        parse_and_execute(fds[i].fd, buf);
    }
}

if (fds[i].revents & POLLOUT)
{
    // Socket is ready to send — flush any queued messages
    send_queued_data(fds[i].fd);
}
```

---

## 3. How They Work Together in the Event Loop

```cpp
// ┌─────────────────────────────────────────────┐
// │         IRC Server Event Loop               │
// └─────────────────────────────────────────────┘

// ┌──────────────────────────────────────────────┐
// │ poll(fds, nfds, timeout)                     │
// │                                              │
// │   ┌─ Listening socket readable?              │
// │   │   → accept() → new connected socket      │
// │   │   → add to pollfd[] array                │
// │   │   → register with server (no nickname yet│
// │   └──────────────────────────────────────────│
// │                                              │
// │   ┌─ Connected socket readable (POLLIN)?     │
// │   │   → recv() → parse IRC command           │
// │   │   → NICK/USER? → register client        │
// │   │   → JOIN? → add to channel              │
// │   │   → PRIVMSG? → relay to others          │
// │   │   → QUIT? → remove & close              │
// │   └──────────────────────────────────────────│
// │                                              │
// │   ┌─ Connected socket writable (POLLOUT)?    │
// │   │   → send() queued messages               │
// │   └──────────────────────────────────────────│
// └──────────────────────────────────────────────┘
```

---

## 4. Side-by-Side: Full Lifecycle

```
Time    Listening Socket (fd=3)          Connected Sockets
────    ─────────────────────────        ─────────────────
│       socket()
│       bind()
│       listen()
│       State = LISTEN
│
│       poll() ─── sleeping ──►
│
│   ←── SYN ──── from Client A
│       Kernel handles handshake
│       Connection queued
│
│   ←── poll() wakes (POLLIN)
│       accept() → fd=5
│                                       fd=5 created
│                                       State = ESTABLISHED
│                                       Client: unregistered
│
│       poll() ─── sleeping ──►
│
│   ←── Client A sends: NICK alice\r\n
│                                       recv(fd=5)
│                                       parse: NICK alice
│                                       Client: alice (half-registered)
│
│   ←── Client A sends: USER alice...\r\n
│                                       recv(fd=5)
│                                       parse: USER alice...
│                                       Client: fully registered
│                                       Send RPL_WELCOME (001)
│
│       poll() ─── sleeping ──►
│
│   ←── SYN ──── from Client B
│       Kernel handles handshake
│       Connection queued
│
│   ←── poll() wakes (POLLIN)
│       accept() → fd=6
│                                       fd=6 created
│                                       State = ESTABLISHED
│                                       Client: unregistered
│
│       poll() ─── sleeping ──►
│
│   ←── Client A sends: JOIN #42\r\n
│                                       recv(fd=5)
│                                       parse: JOIN #42
│                                       Add alice to #42
│                                       Send JOIN to all in #42
│
│   ←── Client B sends: NICK bob\r\n
│                                       recv(fd=6)
│                                       parse: NICK bob
│
│   ←── Client A sends: PRIVMSG #42 :hi\r\n
│                                       recv(fd=5)
│                                       parse: PRIVMSG #42 :hi
│                                       Send to all in #42 (except alice)
│
│   ←── Client A disconnects
│                                       recv(fd=5) → 0
│                                       Remove alice from channels
│                                       close(fd=5)
│                                       Remove fd=5 from pollfd[]
│
│       poll() ─── sleeping ──►
│       ...
│
│       Ctrl+C (SIGINT)
│       g_running = 0
│       poll() wakes
│                                       close(fd=6)
│                                       (all other clients)
│       close(server_fd)
│       State = CLOSED
```

---

## 5. Key Differences at a Glance

| Aspect | Listening Socket | Connected Socket |
|--------|-----------------|-----------------|
| **Who creates it** | `socket()` + `bind()` + `listen()` | `accept()` |
| **File descriptor** | Same fd forever (e.g., 3) | New fd each time (5, 6, 7, ...) |
| **Kernel state** | `LISTEN` | `ESTABLISHED` |
| **TCP handshake** | Not involved — kernel does it | Already completed when created |
| **Read behavior (POLLIN)** | "New connection available" | "Data received from client" |
| **Write behavior (POLLOUT)** | Never needed | "Send buffer has space" |
| **Data to send/recv** | None (only `accept()`) | IRC commands and messages |
| **Number per server** | **1** (the server socket) | **N** (one per client) |
| **Client tracking** | Not tracked | Tracked in client list |
| **When it closes** | Server shutdown | Client disconnects |
| **`SO_REUSEADDR`** | Important (restart safety) | Not needed |

---

## 6. The Mental Model

```
┌───────────────────────────────────────────────────────┐
│                     IRC Server                         │
│                                                        │
│   ┌─────────────────────┐                              │
│   │  Listening Socket   │  ←── The "front door"       │
│   │  (fd = 3)           │      Only one. Always there.│
│   │  Port 6667          │      Never sends/receives.  │
│   │  State = LISTEN     │      Just produces clients. │
│   └──────────┬──────────┘                              │
│              │                                         │
│              │  accept()                               │
│              ▼                                         │
│   ┌─────────────────────┐                              │
│   │  Connected Socket   │  ←── One per client         │
│   │  (fd = 5)           │      Created by accept()    │
│   │  Client: Alice      │      Handles IRC messages   │
│   │  State: ESTABLISHED │      Removed on disconnect  │
│   └─────────────────────┘                              │
│                                                        │
│   ┌─────────────────────┐                              │
│   │  Connected Socket   │  ←── Another client         │
│   │  (fd = 6)           │      Independent from fd=5  │
│   │  Client: Bob        │      Different send/recv    │
│   │  State: ESTABLISHED │      Different buffers      │
│   └─────────────────────┘                              │
│                                                        │
│   ┌─────────────────────┐                              │
│   │  Connected Socket   │  ←── And another...         │
│   │  (fd = 7)           │      Up to FD limit         │
│   │  Client: Charlie    │      All share same port    │
│   │  State: ESTABLISHED │      All share same server  │
│   └─────────────────────┘                              │
└───────────────────────────────────────────────────────┘
```

**The critical rule:** The listening socket is the factory. Connected sockets are the products. The factory never becomes a product, and products never become the factory.

---

## 7. Common IRC Server Implementation Mistakes

### ❌ Mistake 1: Using the listening socket for data I/O

```cpp
// WRONG — never do this
recv(server_fd, buf, sizeof(buf), 0);  // server_fd is LISTEN, not ESTABLISHED
```

The listening socket is not connected to any client. It will never have data to receive.

### ❌ Mistake 2: Closing the listening socket when a client disconnects

```cpp
// WRONG
close(server_fd);   // This kills the ENTIRE server, not just one client
```

Only close the `client_fd` returned by `accept()` when a client disconnects. Close `server_fd` only when the server shuts down.

### ❌ Mistake 3: Forgetting to make connected sockets non-blocking

```cpp
// WRONG — blocking connected sockets will freeze the server
int client_fd = accept(server_fd, NULL, NULL);
// Missing: fcntl(client_fd, F_SETFL, O_NONBLOCK);
```

Without `O_NONBLOCK`, one slow client with a full send buffer blocks the entire event loop.

### ❌ Mistake 4: Mixing up which socket to poll for what

```cpp
// WRONG — listening socket doesn't need POLLOUT
fds[0].fd = server_fd;
fds[0].events = POLLIN | POLLOUT;  // POLLOUT is useless here
```

The listening socket will always be writable (it never has a send buffer to fill). Only connected sockets need `POLLOUT`.

### ❌ Mistake 5: Not tracking which fds are listening vs connected

```cpp
// WRONG — checking POLLHUP on the listening socket
if (fds[i].revents & POLLHUP)
{
    // This can't happen for the listening socket until shutdown
}
```

`POLLHUP` occurs on connected sockets when the peer closes. The listening socket won't get `POLLHUP` until the server itself shuts down.

---

## 8. Summary

> **The listening socket waits for new people to arrive at the door. Connected sockets are the private conversations happening in the rooms. The door attendant (listening socket) never joins any conversation — their only job is to open the door and point newcomers to a room.**

| | Listening Socket | Connected Socket |
|---|---|---|
| **Analogy** | The front door of a club | A private table inside |
| **Job** | Let people in | Talk to customers |
| **Number** | 1 door | Many tables |
| **Who knocks** | SYN packets (new connections) | IRC messages (`\r\n` lines) |
| **Response** | `accept()` → new fd | Parse + execute command |
| **When it goes away** | Club closes (server shuts down) | Customer leaves (client disconnects) |
