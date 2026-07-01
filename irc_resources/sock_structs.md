



# 🧠 Linux Kernel Networking Model (ft_irc view)

This document explains how Linux is structured internally when handling `send()` in a TCP socket.

We focus on:
- Kernel subsystems (WHO does what)
- Their responsibilities (WHY they exist)
- Main structures (WHAT they use)
- Data flow during `send()`

---

# 🌍 FULL KERNEL NETWORKING PIPELINE

```
Userspace
   │
   ▼
System Call Interface
   │
   ▼
────────────────────────────────────────────
KERNEL SUBSYSTEMS
────────────────────────────────────────────
   │
   ▼
VFS (Virtual File System)
Socket Subsystem (BSD API layer)
Networking Core (sock layer)
TCP Subsystem
IP Subsystem
Traffic Control (qdisc)
Device Driver Subsystem
NIC Hardware
```

---

# 1. 🗂️ VFS (Virtual File System Subsystem)

## 🎯 Responsibility

- Makes everything look like a file
- Unifies files, pipes, sockets under one interface
- Handles file descriptors (`fd`)

---

## 📦 Main structure

### `struct file`

```c
struct file {
    const struct file_operations *f_op;
    void *private_data;
};
```

---

## 🧩 What VFS does in send()

- Takes `fd`
- Finds `struct file`
- Routes call into socket subsystem

```
fd → VFS → struct file → struct socket
```

---

# 2. 🔌 Socket Subsystem (BSD Socket Layer)

## 🎯 Responsibility

- Implements socket API:
  - socket()
  - bind()
  - listen()
  - accept()
  - send()
  - recv()
- Chooses which protocol handles the socket (TCP/UDP)

---

## 📦 Main structure

### `struct socket`

```c
struct socket {
    socket_state state;
    short type;
    struct sock *sk;
    const struct proto_ops *ops;
};
```

---

## 🧩 What Socket subsystem does in send()

- Receives request from VFS
- Calls protocol-specific function:
  - `ops->sendmsg()`
- Forwards to networking core (`sock`)

```
struct file → struct socket → proto_ops
```

---

# 3. 🌐 Networking Core Subsystem (Sock layer)

## 🎯 Responsibility

- Maintains connection state
- Stores IPs and ports
- Manages send/receive queues
- Acts as protocol-independent core

---

## 📦 Main structure

### `struct sock`

```c
struct sock {
    int sk_state;

    struct sk_buff_head sk_receive_queue;
    struct sk_buff_head sk_write_queue;

    __be32 sk_rcv_saddr;
    __be32 sk_daddr;

    __be16 sk_num;
    __be16 sk_dport;
};
```

---

## 🧩 What Networking Core does in send()

- Checks connection state (ESTABLISHED)
- Queues data in `sk_write_queue`
- Prepares data for TCP layer

```
socket → sock → queues data
```

---

# 4. 🚚 TCP Subsystem

## 🎯 Responsibility

- Reliability (retransmission)
- Ordering (sequence numbers)
- Flow control (windows)
- Congestion control
- Splitting data into segments

---

## 📦 Main structure

### `struct tcp_sock`

```c
struct tcp_sock {
    struct sock base;

    u32 snd_nxt;
    u32 rcv_nxt;

    u32 snd_wnd;
    u32 rcv_wnd;

    u32 srtt_us;
};
```

---

## 🧩 What TCP subsystem does in send()

- Takes data from `sock`
- Breaks it into segments
- Assigns sequence numbers
- Prepares reliable transmission

```
sock → tcp_sock → TCP segment
```

---

# 5. 🌐 IP Subsystem

## 🎯 Responsibility

- Routing packets
- IP addressing
- Fragmentation
- Packet forwarding

---

## 📦 Main structure

- `struct sk_buff` (packet container)

---

## 🧩 What IP subsystem does in send()

- Wraps TCP segment into IP packet
- Adds source/destination IP
- Chooses route

```
TCP segment → IP packet
```

---

# 6. 🚦 Traffic Control Subsystem (qdisc)

## 🎯 Responsibility

- Queue scheduling
- Bandwidth control
- Packet prioritization

---

## 📦 Main structure

- `sk_buff`
- qdisc structures

---

## 🧩 What it does in send()

- Decides WHEN packet is sent
- May delay or reorder packets

```
IP packet → queue → scheduling
```

---

# 7. 🚗 Device Driver Subsystem

## 🎯 Responsibility

- Interface between kernel and hardware
- Converts packets into Ethernet frames
- Sends data to NIC driver

---

## 📦 Main structure

- `struct net_device`
- `struct sk_buff`

---

## 🧩 What it does in send()

- Converts packet into hardware frame
- Pushes it to NIC

```
IP packet → Ethernet frame
```

---

# 8. 📡 NIC (Hardware Layer)

## 🎯 Responsibility

- Sends bits over wire / Wi-Fi
- Receives frames from network

No kernel structures here anymore — this is hardware.

---

# 🔁 FULL send() FLOW (WITH SUBSYSTEMS)

```
send(fd)

  │
  ▼
VFS Subsystem
  └── struct file

  ▼
Socket Subsystem (BSD API)
  └── struct socket

  ▼
Networking Core Subsystem
  └── struct sock

  ▼
TCP Subsystem
  └── struct tcp_sock

  ▼
IP Subsystem
  └── struct sk_buff

  ▼
Traffic Control Subsystem
  └── qdisc queueing

  ▼
Device Driver Subsystem
  └── struct net_device

  ▼
NIC Hardware
```

---

# 🎯 MENTAL MODEL (IMPORTANT)

| Subsystem | Job | Structure |
|----------|-----|----------|
| VFS | file abstraction | `struct file` |
| Socket subsystem | API routing | `struct socket` |
| Networking core | connection state | `struct sock` |
| TCP subsystem | reliability | `struct tcp_sock` |
| IP subsystem | routing | `sk_buff` |
| Driver subsystem | hardware bridge | `net_device` |
| NIC | physical transmission | hardware |

---

# 🧠 FINAL ONE-LINE MODEL

```
fd → VFS → socket → sock → tcp → IP → driver → NIC
```

---

# 🚀 KEY TAKEAWAY (ft_irc)

When you call:

```c
send(fd, data, len, 0);
```

You are NOT sending data directly.

You are triggering a chain of kernel subsystems:

- VFS finds your socket
- Socket subsystem chooses TCP
- Networking core stores the state
- TCP makes it reliable
- IP routes it
- Driver sends it
- NIC transmits it

Each subsystem has ONE job — and ONE structure it mainly works with.
```
