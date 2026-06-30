
# Understanding `bind()` in TCP Servers

## What is `bind()`?

`bind()` associates a socket with a specific IP address and port.

Until a socket is bound, it has **no network address**, so the operating system has no way to deliver incoming packets to it.

```cpp
int bind(int sockfd, const struct sockaddr *addr, socklen_t addrlen);
```

---

# Where `bind()` fits

A typical TCP server follows this sequence:

```
socket()
    │
    ▼
Create a socket object

bind()
    │
    ▼
Assign an IP address and port

listen()
    │
    ▼
Tell the kernel this socket accepts connections

accept()
    │
    ▼
Create a new socket for each client
```

---

# Before `bind()`

After creating the socket:

```cpp
int server_fd = socket(AF_INET, SOCK_STREAM, 0);
```

the socket exists inside the kernel but has **no address**.

```
server_fd
    │
    ▼
+----------------+
| Socket Object  |
+----------------+

IP   = none
Port = none
```

No client can reach it because the kernel doesn't know where this socket should receive traffic.

---

# The address structure

Before calling `bind()`, we fill a `sockaddr_in` structure.

```cpp
sockaddr_in addr;

addr.sin_family = AF_INET;
addr.sin_port = htons(6667);
addr.sin_addr.s_addr = INADDR_ANY;
```

Each field has a specific purpose.

---

## 1. Address Family

```cpp
addr.sin_family = AF_INET;
```

This tells the kernel:

> This socket uses IPv4 addresses.

---

## 2. Port Number

```cpp
addr.sin_port = htons(6667);
```

This selects the port the server will listen on.

`htons()` converts the number from the computer's byte order into network byte order.

Without this conversion, computers with different CPU architectures could interpret the port incorrectly.

---

## 3. IP Address

```cpp
addr.sin_addr.s_addr = INADDR_ANY;
```

This means:

> Listen on every network interface.

Suppose your computer has:

```
127.0.0.1
192.168.1.15
10.0.0.5
```

Using `INADDR_ANY` means the server accepts connections on:

```
127.0.0.1:6667
192.168.1.15:6667
10.0.0.5:6667
```

all using the same socket.

---

# Calling `bind()`

```cpp
bind(server_fd, (struct sockaddr *)&addr, sizeof(addr));
```

This tells the kernel:

> Associate this socket with this IP address and this port.

After success:

```
server_fd
    │
    ▼
+-------------------------+
| Socket                  |
| IP   = INADDR_ANY       |
| Port = 6667             |
+-------------------------+
```

Now the socket has an address.

---

# What does the kernel do?

Internally, the kernel keeps a table that maps addresses to sockets.

Conceptually:

```
Port      Socket
------------------------
80    ---> socket A
443   ---> socket B
6667  ---> server_fd
```

When a TCP packet arrives:

```
Destination IP
Destination Port
```

the TCP subsystem checks its table.

If the destination port is 6667:

```
Packet
   │
   ▼
TCP subsystem
   │
   ▼
Port 6667
   │
   ▼
server_fd
```

The packet is delivered to your socket.

---

# Why is `bind()` required?

Without `bind()`:

```
socket()
listen()
```

would fail because the socket has no address.

The operating system has no idea which incoming packets belong to it.

A server must have an address before clients can connect.

---

# What happens if another program already uses the port?

Example:

```
Program A
Port 6667
```

Now Program B tries:

```cpp
bind(..., 6667);
```

The kernel sees that port 6667 is already owned.

Result:

```
bind()
    ↓
EADDRINUSE
```

`bind()` fails.

This prevents two programs from listening on the same address and port.

---

# Does `bind()` communicate over the network?

No.

`bind()` is completely local.

It does **not**:

- send packets
- contact another computer
- establish a connection

It only tells the operating system how to identify this socket.

---

# Relationship with the TCP subsystem

Think of the responsibilities like this:

```
socket()
        │
        ▼
Socket subsystem

Creates a socket object.

-----------------------------

bind()
        │
        ▼
Socket subsystem

Registers an IP and port for the socket.

-----------------------------

listen()
        │
        ▼
TCP subsystem

Marks the socket as accepting incoming TCP connections.

-----------------------------

accept()
        │
        ▼
TCP subsystem

Returns a new connected socket when a client completes the TCP handshake.
```

---

# Visual summary

```
socket()

        │

        ▼

+----------------+
| Socket Object  |
+----------------+

        │

      bind()

        │

        ▼

+-------------------------------+
| Socket                        |
| IP   = INADDR_ANY             |
| Port = 6667                   |
+-------------------------------+

        │

     listen()

        │

        ▼

Waiting for TCP connections

        │

     accept()

        │

        ▼

Client connected
```

---

# Key points to remember

- `socket()` creates a socket object.
- `bind()` assigns an IP address and port.
- Without `bind()`, clients cannot reach the server.
- `INADDR_ANY` means "listen on all local interfaces."
- `bind()` is a local kernel operation; it does not send any network traffic.
- The kernel records which socket owns which address and port.
- Incoming TCP packets are delivered to sockets based on this mapping.


# Understanding `listen()` in TCP Servers

# What is `listen()`?

`listen()` tells the kernel:

> "This bound TCP socket is a **server socket** that will accept incoming connection requests."

Before `listen()`, your socket is simply a socket that has been assigned an IP address and port.

After `listen()`, the TCP subsystem begins accepting connection requests for that socket.

```cpp
int listen(int sockfd, int backlog);
```

---

# Where `listen()` fits

A typical TCP server follows this sequence:

```
socket()
    │
    ▼
Create a socket

bind()
    │
    ▼
Assign an IP address and port

listen()
    │
    ▼
Become a listening socket

accept()
    │
    ▼
Receive connected clients
```

---

# Before `listen()`

Suppose you've already done:

```cpp
int server_fd = socket(AF_INET, SOCK_STREAM, 0);

bind(server_fd, ...);
```

The socket now has an address:

```
server_fd
    │
    ▼
+------------------------+
| Socket                 |
| Port = 6667            |
+------------------------+
```

But it is **not yet accepting TCP connections**.

If a client tries to connect now:

```
Client
   │
connect()
   │
   ▼
Kernel
```

the kernel rejects the connection because the socket isn't marked as a listening socket.

---

# What `listen()` actually does

When you call:

```cpp
listen(server_fd, SOMAXCONN);
```

the kernel changes the socket's state.

Before:

```
Socket

Port = 6667

State:
Not listening
```

After:

```
Socket

Port = 6667

State:
Listening
```

Now the TCP subsystem knows:

> "Incoming connection requests for this port should be accepted."

---

# What changes inside the kernel?

Conceptually, the kernel stores something like:

```
Port 6667

↓

Listening Socket
```

The socket is now registered as a **listening endpoint**.

Whenever a TCP SYN packet arrives for port 6667:

```
Incoming SYN
      │
      ▼
TCP subsystem
      │
      ▼
Listening socket
```

the TCP subsystem begins handling the TCP handshake.

# What happens when an incoming SYN reaches a listening socket?

Suppose your server has already executed:

```cpp
socket();
bind();
listen();
```

Now the kernel has something conceptually like this:

```
TCP Listening Table

Port 6667
      │
      ▼
Listening Socket (server_fd)
```

The socket is now waiting for connection requests.

---

# Step 1: A client calls `connect()`

On another machine (or the same one), a client executes:

```cpp
connect(sock, ...);
```

The client TCP subsystem builds the first TCP packet.

```
TCP Packet

Flags:
SYN = 1

Destination IP:
192.168.1.20

Destination Port:
6667
```

The SYN packet means:

> "I'd like to establish a TCP connection."

This packet travels through the network until it reaches your server.

---

# Step 2: The packet arrives at the server

The network card receives the Ethernet frame.

```
Internet

↓

Network Card (NIC)

↓

Kernel
```

The NIC raises an interrupt (or uses polling with modern drivers).

The kernel copies the packet into kernel memory.

---

# Step 3: IP subsystem

The IP layer examines:

```
Destination IP

Protocol
```

Suppose:

```
Destination IP = 192.168.1.20

Protocol = TCP
```

Since the protocol is TCP:

```
IP subsystem

↓

TCP subsystem
```

The IP layer doesn't care about ports.

Its only job is:

> "This is a TCP packet."

---

# Step 4: TCP subsystem examines the packet

The TCP header contains:

```
Source Port

Destination Port

Flags
```

Example:

```
Source Port = 51000

Destination Port = 6667

Flags = SYN
```

The TCP subsystem now asks:

> "Which socket owns port 6667?"

---

# Step 5: Lookup in the listening table

Conceptually, the kernel maintains a table like:

```
Listening Table

80
    ↓
Socket A

443
    ↓
Socket B

6667
    ↓
server_fd
```

The destination port is:

```
6667
```

The TCP subsystem immediately finds:

```
server_fd
```

---

# Step 6: Is this socket listening?

The kernel checks the socket state.

```
Socket

State:
LISTEN
```

If it isn't listening:

```
RST packet

↓

Client receives "Connection refused"
```

If it **is** listening:

```
TCP handshake begins.
```

---

# Step 7: Create a temporary connection

The listening socket itself is **never modified**.

Instead, the kernel allocates a brand new internal TCP connection object.

Conceptually:

```
Listening Socket
        │
        ▼
+--------------------+
| Port = 6667        |
| State = LISTEN     |
+--------------------+

        │

Incoming SYN

        │

        ▼

Create TCP connection object

+-----------------------------+
| Client IP                   |
| Client Port                 |
| Server IP                   |
| Server Port                 |
| TCP Sequence Numbers         |
| Current State               |
+-----------------------------+
```

Notice:

The listening socket stays exactly the same.

The new object stores information about **this particular client**.

---

# Step 8: Send SYN-ACK

The kernel automatically builds another TCP packet.

```
Flags

SYN = 1

ACK = 1
```

This packet is sent back to the client.

```
Server

SYN+ACK

↓

Internet

↓

Client
```

Your program does absolutely nothing here.

The kernel performs this automatically.

---

# Step 9: Wait for the final ACK

The client receives:

```
SYN+ACK
```

and immediately replies:

```
ACK
```

```
Client

ACK

↓

Server
```

Again, your program is unaware.

Everything happens inside the TCP subsystem.

---

# Step 10: TCP connection established

Once the ACK arrives:

```
TCP State

ESTABLISHED
```

The kernel now has:

```
Listening Socket

        │

Completed Connection
```

---

# Step 11: Place it into the accept queue

Conceptually:

```
Listening Socket

        │

        ▼

Accept Queue

+----------------------+
| Client A Connection  |
+----------------------+
```

Notice:

This is **not** a socket yet.

It is a completed TCP connection waiting for your program.

---

# Step 12: Your program calls `accept()`

Eventually your server executes:

```cpp
accept(server_fd, ...);
```

Now the kernel:

1. Removes the first completed connection from the queue.
2. Creates a brand new socket descriptor.
3. Associates that descriptor with the completed TCP connection.

Example:

```
Listening Socket

fd = 3

↓

accept()

↓

Client Socket

fd = 5
```

Now:

```
fd 3

Still listening

------------

fd 5

Connected to Client A
```

---

# Why create a new socket?

Imagine only one socket existed.

```
Listening Socket

↓

Client connects

↓

Socket becomes connected
```

Now how could another client connect?

It couldn't.

Instead:

```
Listening Socket

↓

Client A

↓

Socket 5

Listening socket still free

↓

Client B

↓

Socket 6

Listening socket still free

↓

Client C

↓

Socket 7
```

One listening socket can therefore serve thousands of clients.

---

# Complete flow

```
Client

connect()

        │

        ▼

SYN

        │

        ▼

Network

        │

        ▼

NIC

        │

        ▼

Kernel

        │

        ▼

IP subsystem

        │

        ▼

TCP subsystem

        │

        ▼

Find listening socket (Port 6667)

        │

        ▼

Create temporary TCP connection

        │

        ▼

Send SYN+ACK

        │

        ▼

Receive ACK

        │

        ▼

Connection ESTABLISHED

        │

        ▼

Put connection into accept queue

        │

        ▼

accept()

        │

        ▼

Return a NEW client socket
```

---

# The key insight

Your server program **does not perform the TCP handshake**.

The kernel's TCP subsystem does all of this automatically:

- Receives the SYN.
- Finds the correct listening socket.
- Allocates an internal TCP connection object.
- Tracks sequence numbers.
- Sends the SYN+ACK.
- Receives the final ACK.
- Marks the connection as ESTABLISHED.
- Places it into the accept queue.
- Waits until your program calls `accept()`.

By the time `accept()` returns, the TCP connection is **already fully established**. Your program's job begins only after the kernel has finished the entire handshake.---

# `listen()` does NOT accept clients

Many beginners think:

```
listen()

↓

Client connected
```

This is incorrect.

`listen()` only prepares the socket.

It does **not** return a client.

Instead:

```
listen()

↓

Kernel waits for clients

↓

accept()

↓

Returns one connected client
```

---

# What happens when a client connects?

Suppose a client calls:

```cpp
connect(...)
```

The TCP handshake begins.

```
Client                     Server

SYN --------------------->

        <----------------- SYN+ACK

ACK --------------------->
```

After the handshake completes:

```
Listening socket
        │
        ▼
Kernel creates a completed connection
```

The listening socket remains listening.

The completed connection waits until your program calls:

```cpp
accept()
```

---

# The listening socket never becomes the client socket

This is one of the most important concepts.

```
server_fd
```

always stays the listening socket.

It is never used to communicate with clients.

Instead:

```
Listening Socket

        │

accept()

        │

        ▼

New Client Socket
```

Example:

```
server_fd = 3

accept()

↓

client_fd = 5
```

Now:

```
Socket 3
Listening forever

Socket 5
Connected to Client A
```

If another client connects:

```
accept()

↓

client_fd = 6
```

Now:

```
Socket 3
Listening

Socket 5
Client A

Socket 6
Client B
```

The listening socket never changes.

---

# What is the backlog?

The second argument is:

```cpp
listen(server_fd, backlog);
```

Example:

```cpp
listen(server_fd, 10);
```

The backlog tells the kernel approximately:

> "How many completed connection requests can wait before my program calls `accept()`?"

Conceptually:

```
Client A

Client B

Client C

↓

Kernel Queue

↓

accept()
```

If your server is busy, the kernel stores completed connections in this queue.

---

# Example

Suppose:

```cpp
listen(server_fd, 3);
```

Clients connect:

```
Client A

Client B

Client C
```

Queue:

```
+---------+
| ClientA |
| ClientB |
| ClientC |
+---------+
```

Now your server calls:

```cpp
accept();
```

The kernel removes Client A:

```
+---------+
| ClientB |
| ClientC |
+---------+
```

and returns Client A's socket.

---

# What if the queue becomes full?

Suppose backlog is:

```cpp
listen(server_fd, 2);
```

Queue:

```
Client A

Client B
```

Another client arrives:

```
Client C
```

Depending on the operating system:

- the connection may be refused
- the SYN may be dropped
- the client may wait and retry

Modern Linux kernels treat backlog as a hint and also apply internal limits, so the exact behavior depends on the system.

---

# Does `listen()` block?

No.

`listen()` returns immediately.

It does not wait for clients.

```
listen()

↓

Immediately returns
```

The waiting happens later in:

```
accept()
```

or

```
poll()
```

---

# Relationship with the TCP subsystem

Remember the responsibilities:

```
socket()

↓

Socket subsystem

Creates a socket object.

----------------------------

bind()

↓

Socket subsystem

Assigns an address.

----------------------------

listen()

↓

TCP subsystem

Marks the socket as accepting TCP connections.

Creates the kernel's listening state.

----------------------------

accept()

↓

TCP subsystem

Returns connected clients.
```

---

# Visual summary

```
socket()

        │

        ▼

Create socket

        │

        ▼

bind()

        │

        ▼

Assign address

        │

        ▼

listen()

        │

        ▼

Listening socket

        │

Client connects

        │

        ▼

Kernel finishes TCP handshake

        │

        ▼

Completed connection queue

        │

        ▼

accept()

        │

        ▼

New connected socket
```

---

# Common misconception

Many people think:

```
listen()

↓

Creates client sockets
```

Wrong.

The correct flow is:

```
listen()

↓

Marks socket as listening

↓

TCP handshake occurs

↓

Kernel stores completed connection

↓

accept()

↓

Returns a NEW socket
```

---

# Key points to remember

- `listen()` only works on TCP (`SOCK_STREAM`) sockets.
- It must be called **after** `bind()`.
- It marks the socket as a **listening socket**.
- It does **not** communicate with clients directly.
- It does **not** block.
- It does **not** return client sockets.
- The listening socket remains open for the server's lifetime.
- Every successful `accept()` returns a **new connected socket**, while the original listening socket continues accepting future connections.

# Understanding `accept()` in TCP Servers

# What is `accept()`?

`accept()` removes a completed TCP connection from the kernel's **accept queue** and returns a **new connected socket** that your program uses to communicate with that client.

```cpp
int accept(int sockfd, struct sockaddr *addr, socklen_t *addrlen);
```

The original listening socket **does not change**.

---

# Where `accept()` fits

A TCP server follows this sequence:

```
socket()
    │
    ▼
Create socket

bind()
    │
    ▼
Assign IP and port

listen()
    │
    ▼
Become a listening socket

accept()
    │
    ▼
Receive one connected client
```

---

# Before `accept()`

Suppose you've already executed:

```cpp
socket();
bind();
listen();
```

Your listening socket looks conceptually like this:

```
server_fd

        │

        ▼

+-----------------------+
| Listening Socket      |
| Port = 6667           |
| State = LISTEN        |
+-----------------------+
```

The socket is waiting for incoming connections.

---

# What happens before `accept()`?

Suppose a client calls:

```cpp
connect(...)
```

The TCP subsystem performs the three-way handshake.

```
Client                    Server

SYN --------------------->

       <------------------ SYN+ACK

ACK --------------------->
```

Once the handshake completes:

```
Connection State

ESTABLISHED
```

Now the kernel stores the completed connection.

```
Listening Socket

        │

        ▼

Accept Queue

+----------------------+
| Client A             |
+----------------------+
```

Your program has **not** received anything yet.

The connection exists entirely inside the kernel.

---

# What does `accept()` actually do?

When your server executes:

```cpp
int client_fd = accept(server_fd, NULL, NULL);
```

the kernel:

1. Removes the first completed connection from the accept queue.
2. Creates a brand new socket descriptor.
3. Associates that socket with the TCP connection.
4. Returns the new file descriptor.

Conceptually:

Before:

```
Accept Queue

+----------------+
| Client A       |
+----------------+
```

After:

```
Accept Queue

(empty)

↓

client_fd = 5
```

---

# The listening socket stays alive

One of the biggest misconceptions is that `accept()` changes the listening socket.

It does not.

Before:

```
server_fd

Listening Socket
```

After:

```
server_fd

Listening Socket

------------------------

client_fd

Connected Socket
```

The listening socket continues waiting for new clients forever.

---

# Why return a new socket?

Imagine there were only one socket.

```
Listening Socket

↓

Client A connects

↓

Socket becomes connected
```

Now another client arrives.

Where would that client connect?

Nowhere.

The server could only handle one client.

Instead:

```
Listening Socket

↓

accept()

↓

Socket 5

(Client A)

Listening socket still exists

↓

accept()

↓

Socket 6

(Client B)

Listening socket still exists

↓

accept()

↓

Socket 7

(Client C)
```

The listening socket never becomes a client socket.

---

# The kernel's internal view

Conceptually:

```
Listening Socket

fd = 3

State = LISTEN

----------------------------

Client Socket

fd = 5

State = ESTABLISHED

----------------------------

Client Socket

fd = 6

State = ESTABLISHED
```

One listening socket.

Many connected sockets.

---

# Does `accept()` create the TCP connection?

No.

The TCP connection already exists.

This is extremely important.

By the time `accept()` returns:

```
TCP Handshake

Completed
```

The kernel has already:

- received the SYN
- sent the SYN-ACK
- received the ACK
- created the TCP connection
- marked it ESTABLISHED

`accept()` simply gives your program access to that already-established connection.

---

# What does `accept()` return?

Example:

```cpp
int client_fd = accept(server_fd, NULL, NULL);
```

Suppose:

```
Listening socket

fd = 3
```

After `accept()`:

```
Listening Socket

fd = 3

Still listening

-----------------------

Connected Socket

fd = 5
```

Now:

```
recv(client_fd, ...);

send(client_fd, ...);
```

communicate with that client.

You **never** call `send()` or `recv()` on the listening socket.

---

# Client information

You can also ask the kernel who connected.

```cpp
sockaddr_in client_addr;
socklen_t len = sizeof(client_addr);

int client_fd =
    accept(server_fd,
           (sockaddr *)&client_addr,
           &len);
```

Now the kernel fills:

```
client_addr.sin_addr

↓

Client IP

-------------------

client_addr.sin_port

↓

Client Port
```

This allows the server to know:

```
192.168.1.42

Port 51000
```

---

# Blocking behavior

Normally:

```cpp
accept(server_fd, ...);
```

is **blocking**.

If there are no completed connections:

```
accept()

↓

Sleep
```

The process sleeps inside the kernel.

When a client completes the TCP handshake:

```
Client connects

↓

Kernel wakes process

↓

accept() returns
```

---

# Non-blocking `accept()`

Suppose:

```cpp
fcntl(server_fd, F_SETFL, O_NONBLOCK);
```

Now:

```
accept()
```

behaves differently.

If no clients are waiting:

```
accept()

↓

Immediately returns

-1

errno = EAGAIN
```

The process never sleeps.

This is exactly why servers using `poll()` or `select()` almost always make the listening socket non-blocking.

---

# Relationship with `poll()`

Suppose your server uses:

```cpp
poll();
```

When a new client completes the handshake:

```
Client

↓

TCP Handshake

↓

Accept Queue

↓

Listening socket becomes readable

↓

poll() wakes up
```

Then your server executes:

```cpp
accept();
```

Since `poll()` already told you a client is waiting:

```
accept()

↓

Returns immediately
```

No blocking occurs.

---

# Multiple waiting clients

Suppose three clients connect before your server calls `accept()`.

```
Accept Queue

+----------------+
| Client A       |
| Client B       |
| Client C       |
+----------------+
```

Your server calls:

```cpp
accept();
```

Kernel returns:

```
Client A
```

Queue becomes:

```
+----------------+
| Client B       |
| Client C       |
+----------------+
```

Another call:

```cpp
accept();
```

returns:

```
Client B
```

The queue continues shrinking.

---

# What if the queue is empty?

Blocking socket:

```
accept()

↓

Sleep
```

Non-blocking socket:

```
accept()

↓

-1

errno = EAGAIN
```

No sleeping occurs.

---

# Complete flow

```
Client

connect()

        │

        ▼

TCP Handshake

        │

        ▼

Kernel creates TCP connection

        │

        ▼

Connection enters Accept Queue

        │

        ▼

accept()

        │

        ▼

Kernel creates NEW socket

        │

        ▼

Returns client_fd

        │

        ▼

recv()

send()
```

---

# Visual summary

```
Listening Socket (fd = 3)

        │

Client connects

        │

        ▼

TCP Handshake

        │

        ▼

Accept Queue

+----------------+
| Client A       |
| Client B       |
+----------------+

        │

accept()

        │

        ▼

Client Socket (fd = 5)

Accept Queue

+----------------+
| Client B       |
+----------------+

Listening Socket (fd = 3)

Still waiting for more clients
```

---

# Key points to remember

- `accept()` works only on a **listening socket**.
- It does **not** perform the TCP handshake; the kernel already completed it.
- It removes one connection from the kernel's accept queue.
- It creates and returns a **new connected socket**.
- The original listening socket remains in the `LISTEN` state and can accept more clients.
- Use the returned `client_fd` with `send()`, `recv()`, `poll()`, or `close()`.
- On a blocking socket, `accept()` sleeps if no clients are waiting.
- On a non-blocking socket, `accept()` returns `-1` with `errno == EAGAIN` (or `EWOULDBLOCK`) if the accept queue is empty.


# Understanding `send()` and `recv()` in TCP

# Introduction

Once a client has connected and your server has called `accept()`, both sides now have a **connected socket**.

Communication no longer happens through the listening socket.

Instead:

```
Server

client_fd

        │
        │
TCP Connection
        │
        │

Client

socket_fd
```

`send()` writes data into the TCP connection.

`recv()` reads data from the TCP connection.

---

# The function prototypes

```cpp
ssize_t send(int sockfd, const void *buf, size_t len, int flags);

ssize_t recv(int sockfd, void *buf, size_t len, int flags);
```

Normally you'll use:

```cpp
send(fd, message, length, 0);

recv(fd, buffer, sizeof(buffer), 0);
```

---

# Important idea

Neither function talks directly to the network card.

Instead they communicate with the **kernel's TCP subsystem**.

Think of your program and the kernel as separate worlds.

```
Your Program

↓

send()

↓

Kernel TCP subsystem

↓

Network Card

↓

Internet
```

and

```
Internet

↓

Network Card

↓

Kernel TCP subsystem

↓

recv()

↓

Your Program
```

---

# Understanding `send()`

Suppose you execute

```cpp
send(client_fd, "Hello", 5, 0);
```

Many beginners think this happens:

```
Program

↓

Internet
```

That is **not** what happens.

---

# What actually happens?

Step 1

Your program enters the kernel.

```
Program

↓

send()
```

---

Step 2

The kernel copies your bytes into the **TCP send buffer**.

```
Program

"Hello"

↓

TCP Send Buffer

+----------------+
| H e l l o      |
+----------------+
```

At this moment:

Your program's job is finished.

The bytes are safely stored inside the kernel.

---

Step 3

Later...

The TCP subsystem decides when to transmit them.

```
TCP Send Buffer

↓

TCP packets

↓

Network Card

↓

Internet
```

Your program does **not** control exactly when packets leave the machine.

The TCP subsystem handles:

- segmentation
- retransmissions
- acknowledgements
- congestion control
- flow control

---

# The send buffer

Every connected socket has its own send buffer.

Example:

```
Socket A

Send Buffer

+------------------+

Socket B

Send Buffer

+------------------+
```

The send buffer belongs to the kernel.

Not your program.

---

# Why have a send buffer?

Imagine there were no buffer.

```
Program

↓

Internet
```

The CPU would have to wait for every byte to be physically transmitted.

Networks are much slower than CPUs.

Instead:

```
Program

↓

Copy into buffer

↓

Continue executing

↓

Kernel transmits later
```

This makes communication much faster.

---

# When does `send()` block?

Suppose the send buffer becomes full.

```
TCP Send Buffer

+------------------+
|##################|
|##################|
|##################|
+------------------+

Full
```

Now your program calls

```cpp
send(...)
```

The kernel has nowhere to store more data.

Blocking socket:

```
send()

↓

Sleep

↓

Buffer gets space

↓

Copy data

↓

Return
```

Your process sleeps.

---

# Non-blocking send()

If you've set

```cpp
fcntl(fd, F_SETFL, O_NONBLOCK);
```

Then

```
send()

↓

Buffer Full

↓

Immediately returns

-1

errno = EAGAIN
```

The process never sleeps.

Your server can continue doing other work.

---

# Understanding `recv()`

Suppose another computer sends:

```
Hello
```

The data arrives at your machine.

```
Internet

↓

Network Card
```

The network card gives the packets to the kernel.

---

# What does the TCP subsystem do?

The TCP subsystem:

- verifies checksums
- reorders packets
- removes duplicates
- acknowledges packets
- reconstructs the original byte stream

Then it places the bytes into the **receive buffer**.

```
Receive Buffer

+----------------+
| H e l l o      |
+----------------+
```

Nothing has reached your program yet.

---

# The receive buffer

Every connected socket also has a receive buffer.

```
Socket

Receive Buffer

+----------------+
|Incoming bytes  |
+----------------+
```

The kernel stores incoming bytes here until your program asks for them.

---

# What does `recv()` do?

Suppose:

```
Receive Buffer

+----------------+
|Hello World     |
+----------------+
```

You execute:

```cpp
char buffer[1024];

recv(fd, buffer, sizeof(buffer), 0);
```

The kernel copies:

```
Receive Buffer

↓

Program Buffer
```

Conceptually:

Before:

```
Receive Buffer

Hello World
```

After:

```
Program Buffer

Hello World

Receive Buffer

(empty)
```

---

# Does `recv()` return complete messages?

No.

TCP is a **byte stream**, not a message protocol.

Example:

Sender:

```cpp
send(fd, "Hello", 5, 0);

send(fd, "World", 5, 0);
```

Receiver might get:

```
recv()

↓

HelloWorld
```

or

```
Hello
```

then later

```
World
```

or

```
Hel

loWo

rld
```

TCP guarantees only:

- bytes stay in order
- no bytes are lost (unless the connection fails)

It does **not** preserve message boundaries.

---

# When does `recv()` block?

Suppose the receive buffer is empty.

```
Receive Buffer

(empty)
```

Blocking socket:

```
recv()

↓

Sleep

↓

New data arrives

↓

Copy bytes

↓

Return
```

---

# Non-blocking recv()

If

```cpp
O_NONBLOCK
```

is enabled:

```
recv()

↓

Receive buffer empty

↓

Immediately returns

-1

errno = EAGAIN
```

Again, the process never sleeps.

---

# What does recv() return?

Suppose

```
Hello
```

arrives.

```
recv(...)
```

returns

```
5
```

because five bytes were copied.

---

If the peer closes the connection:

```
recv()

↓

0
```

This means:

> The connection has been closed gracefully.

---

If an error occurs:

```
recv()

↓

-1
```

Check

```
errno
```

for the reason.

---

# Relationship with `poll()`

Suppose:

```
Receive Buffer

(empty)
```

Your process waits in

```cpp
poll();
```

When data arrives:

```
Internet

↓

Receive Buffer

↓

Socket becomes readable

↓

poll() wakes

↓

recv()

↓

Copies bytes
```

Since `poll()` already knows data is waiting,

```
recv()
```

returns immediately.

---

# Complete send path

```
Program

send()

        │

        ▼

Kernel

Send Buffer

        │

        ▼

TCP subsystem

        │

        ▼

Packets

        │

        ▼

Network Card

        │

        ▼

Internet
```

---

# Complete receive path

```
Internet

        │

        ▼

Network Card

        │

        ▼

TCP subsystem

        │

        ▼

Receive Buffer

        │

        ▼

recv()

        │

        ▼

Program Buffer
```

---

# Buffer ownership

Your program owns:

```
char buffer[1024];
```

The kernel owns:

```
TCP Send Buffer

TCP Receive Buffer
```

Data is copied between them.

---

# Common misconceptions

### "send() sends immediately."

False.

It copies into the kernel's send buffer.

Transmission happens later.

---

### "recv() reads directly from the network."

False.

It reads from the kernel's receive buffer.

---

### "One send equals one recv."

False.

TCP is a byte stream.

One `send()` may require several `recv()` calls, and one `recv()` may return data from multiple `send()` calls.

---

### "recv() waits for the entire message."

False.

It returns whatever bytes are currently available (up to the requested size).

Your application protocol (for example, IRC using `\r\n`) determines where messages end.

---

# Visual summary

```
SENDER

Program
    │
send()
    │
    ▼
Kernel Send Buffer
    │
TCP subsystem
    │
Network
    │
──────────────────────────────────────────────
    │
Receiver NIC
    │
TCP subsystem
    │
Kernel Receive Buffer
    │
recv()
    │
Receiver Program
```

---

# Key points to remember

- `send()` copies bytes into the kernel's **send buffer**.
- `recv()` copies bytes out of the kernel's **receive buffer**.
- The TCP subsystem is responsible for packet creation, retransmissions, acknowledgements, ordering, and reliability.
- Each connected socket has its own send and receive buffers.
- On blocking sockets, `send()` blocks when the send buffer is full, and `recv()` blocks when the receive buffer is empty.
- On non-blocking sockets, both return `-1` with `errno == EAGAIN` (or `EWOULDBLOCK`) instead of sleeping.
- TCP is a **reliable byte stream**, not a message protocol, so your application must define message boundaries (e.g., `\r\n` in IRC).
