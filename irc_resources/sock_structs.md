



# Linux Socket Structures and Their Place in the OS

When an application creates a TCP socket, several objects participate in the communication process.

Some of these structures are created by your program, while others exist only inside the Linux kernel.

Understanding where each structure belongs helps explain how a simple call like `send()` eventually becomes a TCP packet on the network.

---

# Complete Overview

```
                         Userspace
┌──────────────────────────────────────────────────────────────┐

                    Application (IRC Server)

                socket()
                bind()
                listen()
                accept()
                poll()
                send()
                recv()

        │

        ▼

    struct sockaddr
    struct sockaddr_in
    struct pollfd

└──────────────────────────────────────────────────────────────┘
                        System Call Boundary
================================================================

                           Kernel Space

        │

File Descriptor Table
        │
        ▼
    struct file
        │
        ▼
    struct socket
        │
        ▼
     struct sock
        │
        ▼
   struct tcp_sock
        │
        ▼
     TCP Layer
        │
        ▼
      IP Layer
        │
        ▼
 Network Driver
        │
        ▼
Network Interface Card (NIC)
```

---

# Level Guide

| Level | Meaning |
|--------|---------|
| ⭐⭐⭐⭐⭐ | Must know for ft_irc |
| ⭐⭐⭐⭐☆ | Good conceptual knowledge |
| ⭐⭐⭐☆☆ | Useful to understand |
| ⭐⭐☆☆☆ | Optional |
| ⭐☆☆☆☆ | Linux kernel internals |

---

# Userspace Structures

These are the structures that **your IRC server creates and manipulates directly**.

---

# struct sockaddr

**Level:** ⭐⭐⭐⭐⭐ Essential

## Purpose

`struct sockaddr` is a **generic socket address**.

It allows the same networking functions (`bind()`, `connect()`, `accept()`, etc.) to work with different address families such as:

- IPv4
- IPv6
- UNIX Domain Sockets

Since every address family has a different structure, Linux uses `sockaddr` as a common interface.

## Definition

```cpp
struct sockaddr
{
    sa_family_t sa_family;
    char        sa_data[14];
};
```

## Fields

| Field | Description |
|--------|-------------|
| `sa_family` | Specifies the address family (`AF_INET`, `AF_INET6`, `AF_UNIX`, etc.). |
| `sa_data` | Raw address data. Its interpretation depends on the address family. Normally you never access it directly. |

## Used By

- `bind()`
- `connect()`
- `accept()`
- `getsockname()`
- `getpeername()`

## Why it Exists

Instead of creating different versions of every socket function for IPv4, IPv6, and UNIX sockets, Linux uses one generic structure.

Most applications actually create a `sockaddr_in` and cast it:

```cpp
bind(fd, (struct sockaddr *)&addr, sizeof(addr));
```

---

# struct sockaddr_in

**Level:** ⭐⭐⭐⭐⭐ Essential

## Purpose

Represents an **IPv4 socket address**.

Unlike `sockaddr`, this structure contains fields that are meaningful for IPv4 networking.

## Definition

```cpp
struct sockaddr_in
{
    sa_family_t    sin_family;
    in_port_t      sin_port;
    struct in_addr sin_addr;
    unsigned char  sin_zero[8];
};
```

## Fields

| Field | Description |
|--------|-------------|
| `sin_family` | Must be `AF_INET`. |
| `sin_port` | TCP/UDP port number in network byte order (`htons()`). |
| `sin_addr` | IPv4 address (`127.0.0.1`, `192.168.1.10`, etc.). |
| `sin_zero` | Padding bytes so the structure has the same size as `sockaddr`. Ignore them. |

## Example

```cpp
sockaddr_in addr;

addr.sin_family = AF_INET;
addr.sin_port = htons(6667);
addr.sin_addr.s_addr = INADDR_ANY;
```

## Used Before

- `bind()`
- `connect()`

---

# struct pollfd

**Level:** ⭐⭐⭐⭐⭐ Essential

## Purpose

Represents **one file descriptor monitored by `poll()`**.

Every socket that you want to monitor has one `pollfd`.

## Definition

```cpp
struct pollfd
{
    int   fd;
    short events;
    short revents;
};
```

## Fields

| Field | Description |
|--------|-------------|
| `fd` | File descriptor to monitor. |
| `events` | Events you're interested in (`POLLIN`, `POLLOUT`, etc.). |
| `revents` | Events that actually occurred. Filled by the kernel. |

## Used By

```cpp
poll(fds, nfds, timeout);
```

---

# Kernel Structures

Everything below this point is created automatically by the Linux kernel.

Your IRC server never allocates these structures itself.

---

# File Descriptor

**Level:** ⭐⭐⭐⭐⭐ Essential

## Purpose

A file descriptor is simply an integer returned by `socket()`.

Example:

```cpp
int server_fd = socket(AF_INET, SOCK_STREAM, 0);
```

Possible result:

```
server_fd = 3
```

The file descriptor is **not** the socket itself.

It is only an index into the process's **file descriptor table**.

```
fd
 │
 ▼
struct file
```

---

# struct file

**Level:** ⭐⭐⭐☆☆ Useful

## Purpose

Represents an **open file** inside Linux.

Linux treats sockets exactly like files.

Every file descriptor points to one `struct file`.

## Simplified Definition

```cpp
struct file
{
    const struct file_operations *f_op;
    void *private_data;
};
```

## Fields

| Field | Description |
|--------|-------------|
| `f_op` | Table of operations available for this file. |
| `private_data` | For sockets, points to the corresponding `struct socket`. |

## Responsibilities

- Connects the file descriptor to the kernel object.
- Integrates sockets with the Virtual File System (VFS).

Relationship:

```
File Descriptor
      │
      ▼
struct file
```

---

# struct socket

**Level:** ⭐⭐⭐⭐☆ Good Conceptual Knowledge

## Purpose

Represents the **generic BSD socket object**.

This is the object that implements the socket API.

It does **not** implement TCP itself.

## Simplified Definition

```cpp
struct socket
{
    socket_state           state;
    short                  type;
    struct sock           *sk;
    const struct proto_ops *ops;
};
```

## Fields

| Field | Description |
|--------|-------------|
| `state` | Current socket state (listening, connected, etc.). |
| `type` | Socket type (`SOCK_STREAM`, `SOCK_DGRAM`, etc.). |
| `sk` | Pointer to the networking object (`struct sock`). |
| `ops` | Table of socket operations (`bind`, `listen`, `accept`, `sendmsg`, `recvmsg`, etc.). |

## Responsibilities

- Represents the BSD socket interface.
- Dispatches socket operations.
- Connects the socket layer to the networking layer.

Relationship:

```
struct socket
      │
      ▼
struct sock
```

---

# struct sock

**Level:** ⭐⭐⭐☆☆ Useful

## Purpose

Represents the **generic networking state**.

Every network protocol (TCP, UDP, etc.) builds on top of this structure.

## Simplified Definition

```cpp
struct sock
{
    int                  sk_state;

    struct socket       *sk_socket;

    struct sk_buff_head  sk_receive_queue;

    struct sk_buff_head  sk_write_queue;

    __be32               sk_rcv_saddr;

    __be32               sk_daddr;

    __be16               sk_num;

    __be16               sk_dport;
};
```

## Fields

| Field | Description |
|--------|-------------|
| `sk_state` | Current connection state (`LISTEN`, `ESTABLISHED`, etc.). |
| `sk_socket` | Pointer back to the owning `struct socket`. |
| `sk_receive_queue` | Incoming packets waiting for `recv()`. |
| `sk_write_queue` | Outgoing packets waiting to be transmitted. |
| `sk_rcv_saddr` | Local IP address. |
| `sk_daddr` | Remote IP address. |
| `sk_num` | Local port. |
| `sk_dport` | Remote port. |

## Responsibilities

- Stores connection information.
- Maintains packet queues.
- Stores addresses and ports.
- Tracks connection state.

This is where networking actually begins.

---

# struct tcp_sock

**Level:** ⭐⭐☆☆☆ Optional

## Purpose

Represents a **TCP connection**.

It extends `struct sock` by adding TCP-specific state.

## Simplified Definition

```cpp
struct tcp_sock
{
    struct sock inet_conn;

    u32 snd_nxt;

    u32 rcv_nxt;

    u32 snd_wnd;

    u32 rcv_wnd;

    u32 srtt_us;
};
```

## Fields

| Field | Description |
|--------|-------------|
| `inet_conn` | Base networking object (`struct sock`). |
| `snd_nxt` | Next TCP sequence number to send. |
| `rcv_nxt` | Next sequence number expected. |
| `snd_wnd` | Current send window size. |
| `rcv_wnd` | Current receive window size. |
| `srtt_us` | Estimated round-trip time. |

## Responsibilities

- Reliability
- Retransmissions
- Congestion control
- Flow control
- Sliding window management

---

# How send() Travels Through These Structures

Suppose your IRC server calls:

```cpp
send(client_fd, buffer, len, 0);
```

Internally the request travels like this:

```
Userspace

Application
    │
send()
    │
    ▼
File Descriptor
    │
══════════════════════════════════════
Kernel
══════════════════════════════════════
    │
    ▼
struct file
    │
    ▼
struct socket
    │
    ▼
struct sock
    │
    ▼
struct tcp_sock
    │
    ▼
TCP
    │
    ▼
IP
    │
    ▼
Network Driver
    │
    ▼
NIC
```

---

# What Should You Know for ft_irc?

## ⭐⭐⭐⭐⭐ Master

- `sockaddr`
- `sockaddr_in`
- `pollfd`
- File Descriptor

Know:

- every field
- why it exists
- how it is used

---

## ⭐⭐⭐⭐ Understand

- `struct socket`

Know:

- its purpose
- its relationship with `struct sock`
- its role in the BSD socket API

---

## ⭐⭐⭐ Understand Conceptually

- `struct file`
- `struct sock`

Know:

- where they live
- what they are responsible for

No need to memorize every field.

---

## ⭐⭐ Optional

- `struct tcp_sock`

Know only that it extends `struct sock` with TCP-specific information.

---

# Final Summary

```
Application
      │
      ▼
sockaddr
sockaddr_in
pollfd
      │
═══════════════════════════════
System Call Boundary
═══════════════════════════════
      │
      ▼
File Descriptor
      │
      ▼
struct file
      │
      ▼
struct socket
      │
      ▼
struct sock
      │
      ▼
struct tcp_sock
      │
      ▼
TCP/IP Stack
      │
      ▼
Network Interface Card
```

For **ft_irc**, you should **master the userspace structures** (`sockaddr`, `sockaddr_in`, `pollfd`, and file descriptors), understand the role of **`struct socket`**, and have a conceptual understanding of **`struct file`** and **`struct sock`**. The deeper kernel structures like **`struct tcp_sock`** are useful for understanding how Linux implements TCP but are not required to implement or defend your IRC server.
