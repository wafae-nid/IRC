
# Walking Through `send()` and `recv()` Inside the Operating System

One of the most confusing things about network programming is understanding **what actually happens after you call `send()` or `recv()`**.

Many beginners imagine that `send()` directly sends bytes across the Internet or that `recv()` directly reads them from the network card.

That is **not** what happens.

Instead, both functions travel through several kernel subsystems before anything reaches or leaves the physical network.

This document follows the complete path for an IRC server.

---

# The Journey of `send()`

Suppose your IRC server wants to send this message:

```cpp
send(client_fd, ":server NOTICE Wafae :Hello!\r\n", len, 0);
```

Let's follow every step.

---

# Step 1 — Your Process Calls send()

Your IRC server is currently running in **user space**.

```
User Space

IRC Server
    │
    │ send()
    ▼
```

Calling `send()` is a **system call**.

A system call asks the kernel to perform privileged work.

The CPU switches from:

```
User Mode
        ↓
Kernel Mode
```

Only now does the kernel begin handling your request.

---

# Step 2 — File Descriptor Subsystem

The first thing the kernel receives is

```
client_fd
```

A file descriptor is simply an integer.

Example

```
client_fd = 7
```

The kernel cannot use "7" directly.

Instead it searches the process's file descriptor table.

```
Process

FD Table

0 → stdin
1 → stdout
2 → stderr
7 → socket object
```

Now the kernel has found the socket object.

---

# Step 3 — Socket Layer

The socket subsystem now takes over.

The socket object contains information such as

```
Protocol (TCP)

Local IP

Remote IP

Local Port

Remote Port

Socket state

Send buffer

Receive buffer
```

Conceptually

```
Socket

+-------------------------+
| TCP                     |
| Connected               |
| Send Buffer             |
| Receive Buffer          |
+-------------------------+
```

Since this socket uses TCP,

the socket layer forwards the request to the TCP subsystem.

```
send()

        │
        ▼

Socket Layer

        │
        ▼

TCP
```

---

# Step 4 — Copy From User Space

Your string currently lives in your program's memory.

```
User Space

char message[]
```

The kernel **cannot trust user memory directly**.

Therefore it copies the bytes into kernel memory.

```
User Space

message

        │ copy

        ▼

Kernel Send Buffer
```

This copy is performed immediately.

Once the copy finishes,

your program is free to modify or destroy the original buffer.

---

# Step 5 — TCP Send Buffer

Every TCP socket owns a **send buffer**.

Conceptually

```
TCP Socket

+----------------------+
| Send Buffer          |
|                      |
| NOTICE Hello...      |
+----------------------+
```

The data is **not yet on the network.**

It is simply waiting inside the kernel.

This is one of the biggest misconceptions.

After `send()` returns successfully,

it usually means

> "The kernel accepted your bytes."

It does **not** necessarily mean

> "The client has already received them."

---

# Step 6 — Can the Buffer Accept More Data?

The kernel checks

```
How much free space remains?
```

Example

```
Buffer size = 64 KB

Currently used = 10 KB

Free = 54 KB
```

Enough space exists.

The copy succeeds.

---

If instead

```
Buffer full
```

then there are two possibilities.

---

## Blocking Socket

```
send()
```

goes to sleep.

```
IRC Server

send()

    │
    ▼

Sleeping...
```

The process remains asleep until

```
Network transmits data

↓

Space becomes available

↓

Kernel wakes send()
```

---

## Non-blocking Socket

If

```
fcntl(fd, F_SETFL, O_NONBLOCK);
```

was previously called,

then `send()` never sleeps.

Instead it immediately returns

```
-1

errno = EAGAIN
```

meaning

```
Try again later.
```

This is exactly why IRC servers use non-blocking sockets.

---

# Step 7 — TCP Builds Packets

The TCP subsystem eventually decides it's time to transmit.

It breaks the byte stream into segments.

```
Hello IRC World

↓

TCP Segment 1

↓

TCP Segment 2
```

Each packet receives a TCP header.

```
+----------------+
| TCP Header     |
+----------------+
| Payload        |
+----------------+
```

The TCP header contains

- sequence number
- acknowledgement number
- flags
- checksum
- window size

---

# Step 8 — IP Layer

TCP now hands each packet to IP.

```
TCP

↓

IP
```

IP adds another header.

```
+----------------+
| IP Header      |
+----------------+
| TCP Header     |
+----------------+
| Payload        |
+----------------+
```

Now the kernel knows

```
Destination IP

Routing

TTL

Fragmentation
```

---

# Step 9 — Network Driver

The packet reaches the network driver.

```
IP

↓

Network Driver
```

The driver communicates with

```
Ethernet Card

Wi-Fi Card

Virtual NIC
```

---

# Step 10 — NIC Transmission Queue

The network card owns its own transmit queue.

```
NIC

Transmit Queue

Packet 1

Packet 2

Packet 3
```

The driver copies packets into this queue.

---

# Step 11 — Physical Transmission

Finally,

the network card converts the packets into electrical,

radio,

or optical signals.

```
NIC

↓

Wire

↓

Internet
```

The packet leaves your machine.

---

# Summary of send()

```
User Space

IRC Server

    │

send()

    │

System Call

    │

File Descriptor Table

    │

Socket Layer

    │

TCP

    │

Copy into Send Buffer

    │

Packet Creation

    │

IP

    │

Network Driver

    │

NIC Queue

    │

Physical Network
```

---

# The Journey of recv()

Now imagine another IRC client sends

```
PRIVMSG #42 :Hello
```

How does your server receive it?

---

# Step 1 — Network Card Receives Bits

The packet arrives from the Internet.

```
Internet

↓

NIC
```

The network card detects a complete Ethernet frame.

---

# Step 2 — Interrupt

The NIC notifies the CPU.

```
NIC

↓

Interrupt

↓

Kernel
```

Modern systems often use interrupt moderation or NAPI polling, but conceptually the result is the same: the kernel is notified that new packets have arrived.

---

# Step 3 — Network Driver

The network driver copies the packet into kernel memory.

```
NIC

↓

Driver Buffer
```

---

# Step 4 — IP Layer

The driver passes the packet upward.

```
Driver

↓

IP
```

The IP layer checks

- destination IP
- checksum
- fragmentation

If everything is valid,

the payload is handed to TCP.

---

# Step 5 — TCP Layer

TCP validates

- checksum
- sequence number
- ordering
- duplicate packets
- retransmissions

If necessary,

TCP waits until missing packets arrive.

Your application never sees incomplete TCP streams.

---

# Step 6 — Receive Buffer

The payload is copied into the socket's receive buffer.

```
Socket

+----------------------+
| Receive Buffer       |
| PRIVMSG Hello...     |
+----------------------+
```

At this point,

your application still has not executed `recv()`.

The bytes are waiting inside the kernel.

---

# Step 7 — poll() Notices Readable Data

When data enters the receive buffer,

the socket becomes readable.

The kernel marks it as

```
POLLIN
```

If your IRC server is sleeping inside

```cpp
poll()
```

the kernel wakes it.

```
Incoming Packet

↓

Receive Buffer

↓

POLLIN

↓

poll() wakes

↓

IRC Server resumes
```

This is why a well-designed event-driven server spends most of its idle time blocked in `poll()`, waiting for the kernel to report that work is available.

---

# Step 8 — Your Server Calls recv()

After `poll()` returns,

your server executes

```cpp
recv(client_fd, buffer, sizeof(buffer), 0);
```

Again,

execution switches into kernel mode.

---

# Step 9 — File Descriptor Lookup

The kernel again finds

```
client_fd

↓

Socket
```

---

# Step 10 — Copy to User Space

The kernel copies bytes

from

```
Receive Buffer
```

to

```
Your buffer
```

```
Kernel Receive Buffer

        │ copy

        ▼

char buffer[1024]
```

---

# Step 11 — Remove Bytes from Receive Buffer

Once copied,

those bytes are removed from the socket's receive buffer.

If more data remains,

the socket will continue to be reported as readable.

If the buffer becomes empty,

`poll()` will stop reporting `POLLIN` until new data arrives.

---

# Blocking vs Non-blocking recv()

If no data exists:

Blocking socket

```
recv()

↓

Sleep
```

The process sleeps until data arrives.

---

Non-blocking socket

```
recv()

↓

-1

errno = EAGAIN
```

No sleeping occurs.

---

# Summary of recv()

```
Internet

    │

Network Card

    │

Driver

    │

IP

    │

TCP

    │

Receive Buffer

    │

poll() wakes

    │

recv()

    │

Copy to User Buffer

    │

IRC Server
```

---

# Why Event-Driven IRC Servers Use poll()

Without `poll()`, calling `recv()` on every client would either waste CPU (constantly checking sockets that have no data) or block the server on one client.

With `poll()`:

1. The server sleeps efficiently while nothing is happening.
2. The kernel wakes it only when one or more sockets become ready.
3. The server calls `recv()` only on sockets reported as readable (`POLLIN`).
4. It calls `send()` only when appropriate, and with non-blocking sockets it avoids getting stuck if the send buffer is full.

This design lets a single thread handle many clients concurrently without blocking on any one connection.

---

# Complete Flow Diagram

```
                    SENDING

User Buffer
     │
     ▼
send()
     │
     ▼
File Descriptor Table
     │
     ▼
Socket Layer
     │
     ▼
TCP Send Buffer
     │
     ▼
TCP Segmentation
     │
     ▼
IP Layer
     │
     ▼
Network Driver
     │
     ▼
NIC Queue
     │
     ▼
Internet


                    RECEIVING

Internet
     │
     ▼
NIC
     │
     ▼
Network Driver
     │
     ▼
IP Layer
     │
     ▼
TCP
     │
     ▼
Receive Buffer
     │
     ▼
POLLIN
     │
     ▼
poll() wakes
     │
     ▼
recv()
     │
     ▼
User Buffer
```
