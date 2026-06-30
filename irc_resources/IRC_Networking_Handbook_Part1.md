# IRC Networking Handbook - Part 1

## Introduction

This handbook explains networking in the order the kernel thinks about
it. The goal is not to memorize APIs but to understand *why* they exist.

# Chapter 1 - The Kernel

Your program cannot directly access hardware. Whenever it wants to
create a socket, read a file, or allocate privileged resources, it asks
the kernel through a system call.

    Your Program
         |
    System Call
         |
       Kernel
         |
    Hardware

The kernel performs the privileged work and returns the result.

# Chapter 2 - Why the Kernel Uses Subsystems

A modern operating system is far too large to be one source file.
Instead it is divided into subsystems, each responsible for one area.

    Kernel
    ├── Memory Manager
    ├── Process Scheduler
    ├── File System
    └── Networking

This follows the software engineering principle of **separation of
concerns**: one component should have one responsibility.

# Chapter 3 - Networking Subsystems

Networking is also too large to be a single component.

    Networking
    ├── Socket subsystem
    ├── TCP subsystem
    └── IP subsystem

## Socket subsystem

The socket subsystem manages socket objects.

Responsibilities:

-   socket()
-   bind()
-   listen()
-   accept()
-   close()
-   socket buffers
-   socket options
-   ownership of local addresses

Think:

> "I manage sockets."

It does **not** perform retransmissions or routing.

## TCP subsystem

TCP provides reliable byte-stream communication.

Responsibilities:

-   sequence numbers
-   acknowledgements
-   retransmissions
-   congestion control
-   flow control
-   Nagle algorithm

Think:

> "Give me bytes. I'll deliver them reliably."

## IP subsystem

IP moves packets between machines.

Responsibilities:

-   routing
-   source and destination addresses
-   TTL
-   fragmentation

Think:

> "I know where packets should go."

# Chapter 4 - The Socket Object

Calling

``` cpp
int fd = socket(AF_INET, SOCK_STREAM, 0);
```

creates one kernel socket object.

Conceptually:

    Socket Object
    -------------
    Family = AF_INET
    Type = SOCK_STREAM
    Protocol = TCP
    State = CREATED

The kernel returns a file descriptor.

    FD Table

    3 ---> Socket Object

The file descriptor is only a handle. The socket lives inside the
kernel.

## One Socket, Multiple Subsystems

There is only one socket object.

Different subsystems manage different parts.

                     Socket Object

         +----------------+----------------+
         |                |                |
     Socket settings   TCP information   IP information

Like one house:

-   electrician works on wiring
-   plumber works on pipes
-   gardener works outside

One house. Different specialists.
 

# Chapter 5 - socket()

Prototype:

``` cpp
socket(domain, type, protocol);
```

## AF_INET

Use IPv4 addresses.

## SOCK_STREAM

Request a reliable byte stream.

## Protocol

`0` means:

"Choose the default protocol."

For AF_INET + SOCK_STREAM the default is TCP.

Therefore these are equivalent:

``` cpp
socket(AF_INET, SOCK_STREAM, 0);
socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
```

# Chapter 6 - Socket Configuration

Prototype:

``` cpp
setsockopt(fd, level, option, value, size);
```

The important question is:

**Which subsystem owns this option?**

That is what `level` means.

## SOL_SOCKET

Means:

"The Socket subsystem owns this option."

Example:

``` cpp
setsockopt(fd,SOL_SOCKET,SO_REUSEADDR,&opt,sizeof(opt));
```

Conceptually:

    Socket settings:
    reuse_address = true

Nothing is sent over the network.

## Why SO_REUSEADDR belongs here

SO_REUSEADDR affects whether this socket may reuse a local address
during bind().

That is socket management.

It is not TCP reliability. It is not IP routing.

Therefore the socket subsystem owns it.

## TCP options

``` cpp
setsockopt(fd, IPPROTO_TCP, TCP_NODELAY,...);
```

TCP owns Nagle's algorithm.

## IP options

``` cpp
setsockopt(fd, IPPROTO_IP, IP_TTL,...);
```

IP owns packet lifetime.

# Chapter 7 - fcntl()

``` cpp
fcntl(fd,F_SETFL,O_NONBLOCK);
```
fcntl()
fcntl() belongs to the file descriptor (VFS/File) subsystem, not the networking subsystem.
F_SETFL
F_SETFL is a command to fcntl(), meaning 'set the file status flags'. O_NONBLOCK is the flag being applied.
Comparison
Function
Who handles it?
Meaning
setsockopt
Networking subsystem
Choose subsystem (SOL_SOCKET/IPPROTO_TCP/IPPROTO_IP) then set an option.
fcntl
File descriptor subsystem
Execute a command such as F_SETFL with a value like O_NONBLOCK.

Changes the file descriptor status flags.

O_NONBLOCK means operations return immediately instead of sleeping.

Essential with poll() because one slow client should not block the whole
server.

## 3. What fcntl(fd, F_SETFL, O_NONBLOCK) actually does

This call:

fcntl(fd, F_SETFL, O_NONBLOCK);

means:

Go to the kernel structure behind fd
Modify its status flags
Set O_NONBLOCK

So you are changing a property of the fd itself, not the socket protocol.

# 4. What changes when you set O_NONBLOCK

Before:

recv() / accept() / read()
→ may block (sleep the thread) until data exists

After:

recv() / accept() / read()
→ return immediately if no data
→ return -1 and errno = EAGAIN / EWOULDBLOCK

    # mental model

        User space
        ──────────
        fd = 3
        |
        v
        Kernel
        ──────────
        fd table (per process)
        |
        v
        struct file  ← THIS is the “file object”
        |
        v
        socket structure (sock)
        |
        v
        TCP state machine + buffers

        A file descriptor (fd) is an index into the process’s file descriptor table, and each entry points to a struct file.

    now whats the struct file
    struct file 
    {
        f_mode;            // read/write permissions
        f_flags;           // ⚠️ includes O_NONBLOCK, O_APPEND, etc.

        f_op;              // function table (read/write/open/etc.)

        private_data;      // pointer to socket OR inode OR pipe

        refcount;
    };  

        private_data : has our socket 

        f_flags : is what fcntl changes to non block 

# Why Non-Blocking Sockets Are Essential in an IRC Server

## Main Idea

An IRC server typically has one event loop:

while (true)
{
    poll(...);      // Wait for socket events

    // Handle ready sockets
}

## The philosophy is:

poll() is the only function allowed to wait.
accept(), recv(), and send() should never make the server sleep.
If they cannot complete immediately, they should return EAGAIN so the server can continue handling other clients.
## 1. recv() and the Receive Buffer
    What does recv() do?

    Each socket owns a receive buffer inside the kernel.

    When a client sends data:

    Client
    |
    |  "PRIVMSG #general :Hello"
    |
    V
    +----------------------+
    | Receive Buffer       |
    +----------------------+

    When the server executes:

    recv(client_fd, buffer, sizeof(buffer), 0);

    the kernel copies data from the receive buffer into your program.

    Blocking socket

    Suppose the receive buffer becomes empty.

    Receive Buffer

    +------------+
    |            |
    +------------+

    Now the server calls:

    recv(client_fd, ...);

    The kernel says:

    "There is no data available. I'll wait until the client sends more."

    The server sleeps.

    While it is sleeping:

    Another client sends a message.
    Another client joins a channel.
    Another client disconnects.

    None of these events are handled because the server is blocked waiting for one client.

    Non-blocking socket

    The same situation:

    Receive Buffer

    +------------+
    |            |
    +------------+

    Instead of sleeping:

    recv()
        ↓
    -1
    errno = EAGAIN

    The server immediately continues handling every other client.

## 2. send() and the Send Buffer
    What does send() do?

    Each socket also owns a send buffer.

    When the server executes:

    send(client_fd, message, length, 0);

    the kernel copies the message into the send buffer.

    Application
        |
        | send()
        V
    +----------------------+
    | Send Buffer          |
    +----------------------+
        |
        V
    Network

    TCP later transmits the data from the send buffer to the client.

    Blocking socket

    Imagine one client has a very slow internet connection.

    The client is not reading data quickly enough.

    Eventually the send buffer becomes full.

    Send Buffer

    +############+
    |    FULL    |
    +############+

    Now the server tries:

    send(client_fd, ...);

    The kernel says:

    "The send buffer is full. I'll wait until the client reads some data."

    The server sleeps.

    While waiting for this one slow client:

    Nobody receives new messages.
    Nobody can join channels.
    Nobody can authenticate.
    The entire IRC server is frozen.
    Non-blocking socket

    The send buffer is still full.

    Instead of sleeping:

    send()
        ↓
    -1
    errno = EAGAIN

    The server simply skips this client for now and continues serving every other client.

    Later, when poll() reports that the socket is writable again, the server retries the send.

## 3. accept()

The listening socket also follows the same philosophy.

If no connection is immediately available:

Blocking:

accept()
    ↓
wait for next connection

Non-blocking:

accept()
    ↓
-1
errno = EAGAIN

The server continues its event loop.

Summary
Function	What can cause blocking?	Non-blocking behavior
accept()	No connection available	Returns EAGAIN
recv()	Receive buffer is empty	Returns EAGAIN
send()	Send buffer is full	Returns EAGAIN
The philosophy of an IRC server

poll() is responsible for waiting for events.

Once poll() returns, every I/O operation (accept, recv, send) should return immediately.

If an operation cannot be completed because the receive buffer is empty or the send buffer is full, it returns EAGAIN instead of putting the entire server to sleep.

This allows a single-threaded IRC server to continue serving all connected clients without one slow or inactive client blocking everyone else.

## mental recap 
        user space
                |
                v
        file descriptor (fd)
                |
                v
        struct file  ← fcntl modifies THIS
                |
                v
        struct socket
                |
                v
        struct sock   ← setsockopt often modifies THIS
                |
                v
        TCP/IP stack
## now the relation to other IRC syscalls

    poll() is independent of O_NONBLOCK.

    poll() has its own blocking behavior, controlled by its timeout parameter.

    For example:

    poll(fds, nfds, -1);

    This means:

    "Sleep until at least one fd becomes ready."

    So poll() blocks, even if every socket is non-blocking.

    If you do:

    poll(fds, nfds, 0);

    then:

    Check all fds immediately and return.

    This is non-blocking.

    If you do:

    poll(fds, nfds, 5000);

    then:

    Wait up to 5 seconds for an event.

    Why doesn't O_NONBLOCK affect poll()?

    Because O_NONBLOCK is stored in the struct file and affects I/O operations like:

    accept()
    recv()
    send()
    read()
    write()

    poll() doesn't perform I/O. It asks the kernel:

    "Which of these file descriptors are ready for I/O?"

    It doesn't try to read or write anything, so it doesn't care about O_NONBLOCK.
## why non_block

    A concise answer you can use is:

    In an IRC server, sockets are made non-blocking so that no single client or socket operation can pause the entire server. poll() waits for sockets to become ready, and once it returns, accept(), recv(), and send() return immediately instead of blocking. If an operation can't proceed, it returns EAGAIN/EWOULDBLOCK, allowing the server to continue handling other clients.

    Or even shorter:

    Non-blocking sockets ensure that one client cannot block the event loop, allowing the IRC server to serve many clients concurrently with poll().
    
# Chapter 8 - Signals

``` cpp
volatile sig_atomic_t running = 1;
```

sig_atomic_t guarantees atomic access with respect to signals.

volatile tells the compiler the value may change asynchronously.

# Chapter 9 - Ports

Ports are 16-bit unsigned integers.

Valid range:

0-65535

Ports below 1024 are traditionally privileged on Unix systems, so
servers commonly require 1024-65535.
