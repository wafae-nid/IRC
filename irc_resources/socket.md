
# Socket

# What is a Socket?

A **socket** is an **operating-system-managed communication endpoint** that allows two processes to exchange data.

Depending on the address family used, sockets can communicate:

* **Across a network** (using IPv4/IPv6 and protocols such as TCP or UDP)
* **Locally on the same machine** (using Unix domain sockets)

The socket itself is simply the endpoint. The operating system determines how the communication is performed based on the socket's address family.

## Network Socket (Different Machines)

This is the type of socket used in an IRC server.

```text
Client Application
        │
     Socket
        │
   TCP/IP Stack
        │
     Network
        │
   TCP/IP Stack
        │
     Socket
        │
Server Application
```

Created with:

```cpp
socket(AF_INET, SOCK_STREAM, 0);
```

* `AF_INET` → IPv4
* `SOCK_STREAM` → TCP

Communication is identified by an **IP address** and a **port number**.

---

## Local (Unix Domain) Socket

Sockets can also be used for communication between processes running on the **same machine**.

In this case, no network hardware or TCP/IP protocol is involved. The kernel transfers the data directly between the two sockets.



Call                                                Address Family    Socket Type    Protocol Used
socket(AF_INET, SOCK_STREAM, 0)    IPv4                   Stream    TCP
socket(AF_INET6, SOCK_STREAM, 0)    IPv6                  Stream    TCP
socket(AF_INET, SOCK_DGRAM, 0)       IPv4                   Datagram    UDP
socket(AF_UNIX, SOCK_STREAM, 0)   Unix domain          Stream    Unix domain stream protocol (not TCP)
socket(AF_UNIX, SOCK_DGRAM, 0)    Unix domain        Datagram    Unix domain datagram protocol (not UDP)

```text
Process A
    │
 Socket
    │
 Kernel
    │
 Socket
    │
Process B
```
Process A
    │
send()
    │
Socket A
    │
─────────────── Kernel Memory ───────────────
            copies "Hello"
─────────────────────────────────────────────
    │
Socket B
    │
recv()
    │
Process B

Created with:

```cpp
socket(AF_UNIX, SOCK_STREAM, 0);
```

Instead of using an IP address and port, Unix domain sockets use a **filesystem path** (for example, `/tmp/my_socket`).

---

## Summary

A socket is **not** a networking protocol—it is a generic communication endpoint provided by the operating system.

The address family determines how communication occurs:

| Address Family | Communication Type                |
| -------------- | --------------------------------- |
| `AF_INET`      | IPv4 networking                   |
| `AF_INET6`     | IPv6 networking                   |
| `AF_UNIX`      | Local inter-process communication |

This is why the same socket API (`socket()`, `bind()`, `listen()`, `accept()`, `send()`, and `recv()`) can be used for both network communication and local inter-process communication.
---


# Why Use a Socket Instead of a Pipe?

Pipes and sockets are both mechanisms for inter-process communication (IPC), but they are designed for different use cases.

A **pipe** is simple and efficient, but it is mainly intended for communication between **related processes**, such as a parent and its child after a `fork()`.

```text
Parent
   │
 pipe()
   │
fork()
   │
 ├──────────────┐
 │              │
 ▼              ▼
Parent        Child
```

Pipes are commonly used by shells to implement commands like:

```bash
ls | grep txt
```

---

## Limitations of Pipes

Pipes have several limitations:

* Primarily designed for related processes.
* Anonymous pipes have no public address.
* Cannot accept connections from new processes.
* Do not provide a client/server model.
* Bidirectional communication usually requires two pipes.

---

## Why Unix Domain Sockets Exist

Unix domain sockets solve these limitations by providing a **named communication endpoint** on the local machine.

A server creates a socket and binds it to a filesystem path:

```text
/tmp/chat.sock
```

Any process that knows this path can connect to it, even if the processes are completely unrelated.

```text
Client Process
      │
connect("/tmp/chat.sock")
      │
    Kernel
      │
accept()
      │
Server Process
```

This allows local communication using the same client/server model as network programming.

---

## Pipe vs Unix Domain Socket

| Pipe                          | Unix Domain Socket                            |
| ----------------------------- | --------------------------------------------- |
| Mainly for related processes  | Works with any processes                      |
| Anonymous by default          | Has a filesystem path (address)               |
| No client/server model        | Supports client/server communication          |
| Cannot accept new connections | Can `listen()` and `accept()`                 |
| Usually one-way               | Full-duplex (send and receive simultaneously) |

---
# Why Do We Need Sockets?

Modern operating systems isolate each process in its own protected memory space. A process cannot directly read from or write to another process's memory.

```text
+-------------------+
| Process A         |
| Memory            |
+-------------------+

        ✗ Direct access is not allowed

+-------------------+
| Process B         |
| Memory            |
+-------------------+
```

Because of this isolation, processes need a safe way to exchange data. A **socket** provides this mechanism.

A socket is an **operating-system-managed communication endpoint**. Instead of communicating through shared memory, each process sends data to its socket, and the kernel safely transfers it to the destination socket.

```text
Process A
    │
 send()
    │
 Socket
    │
 Kernel
    │
 Socket
    │
 recv()
    │
Process B
```

---

# Why Use Sockets?

Sockets provide a **unified communication interface** that works for both:

* Communication between processes on the **same machine** (`AF_UNIX`)
* Communication between processes on **different machines** (`AF_INET` / `AF_INET6`)

Applications use the same programming interface regardless of where the other process is located.

```cpp
socket();
bind();
listen();
accept();
connect();
send();
recv();
close();
```
## Do We Still Use `bind()`, `listen()`, and `accept()` for Processes on the Same Machine?

Yes—**if you are implementing a server**.

The need for `bind()`, `listen()`, and `accept()` depends on the **client/server model**, not on whether the processes are on the same machine.

For a server using **Unix domain sockets (`AF_UNIX`)**, the workflow is the same as a TCP server:

```text
Server                          Client
------                          ------
socket()                        socket()
bind()                          connect()
listen()           ◄──────────►
accept()
send()/recv()                   send()/recv()
```

The main difference is the address used:

| Network Socket (`AF_INET`) | Unix Domain Socket (`AF_UNIX`)             |
| -------------------------- | ------------------------------------------ |
| IP address + Port          | Filesystem path (e.g., `/tmp/server.sock`) |

---

## When Are `listen()` and `accept()` Not Needed?

They are **not** used when the communication channel is already established, such as with:

* `pipe()`
* `socketpair()`

In these cases, the processes can immediately exchange data using `read()`/`write()` or `send()`/`recv()` because there is no need to wait for clients to connect.

---

## Summary

Whether processes are on the **same machine** or **different machines** does **not** determine whether you use `bind()`, `listen()`, and `accept()`.

What matters is the communication model:

* **Client/Server** → Use `bind()`, `listen()`, `accept()`, and `connect()`.
* **Already-connected communication** (e.g., `pipe()` or `socketpair()`) → No `bind()`, `listen()`, or `accept()` are needed.
The operating system handles the underlying communication, whether it occurs through the local kernel or across a network.

---

# Summary

Sockets exist because processes cannot directly access each other's memory. They provide a safe, operating-system-managed communication endpoint that allows processes to exchange data locally or over a network using the same consistent API.
# Summary

Pipes are ideal for simple communication between related processes.

Unix domain sockets are more flexible because they allow **unrelated processes** to communicate through a named endpoint while using the same socket API (`socket()`, `bind()`, `listen()`, `accept()`, `connect()`, `send()`, and `recv()`) used for network programming. This consistency makes it easy to write applications that can communicate either locally or across a network with very little change to the code.

Applications need a standard way to communicate with one another.

For example:

- A web browser communicates with a web server.
- An IRC client communicates with an IRC server.
- A game client communicates with a game server.

Sockets provide a **common communication interface** that every network application can use, regardless of the underlying hardware or operating system.

Without sockets, every application would need its own custom method for accessing the network, making communication between programs inconsistent and impractical.

Sockets allow developers to focus on **what data to send**, while the operating system provides a consistent interface for transmitting and receiving that data.

---

# Key Idea

A socket **does not represent the entire network**.

It simply represents **one endpoint of a communication channel**. Every network connection has two endpoints:

- One socket on the client.
- One socket on the server.

Together, these two sockets form the communication channel through which data flows.

---

# In One Sentence

> **A socket is a communication endpoint that provides applications with a standard way to exchange data with other applications over a network.**
