# IRC Server Socket Notes

## 1. `socket(AF_INET, SOCK_STREAM, 0)`

Creates a kernel socket object and returns a **file descriptor (FD)**.

The FD is **not the socket itself**. It is an integer that indexes your
process's file descriptor table.

Conceptually:

    FD table

    3 ---> Kernel Socket Object

The kernel socket stores information such as:

-   Address family (IPv4)
-   Type (stream)
-   Protocol (TCP)
-   Socket options
-   State
-   Send/receive buffers
-   Local/remote addresses (later)

------------------------------------------------------------------------

## 2. `AF_INET`

Address family = IPv4.

It tells the kernel that this socket will use IPv4 addresses.

------------------------------------------------------------------------

## 3. `SOCK_STREAM`

A reliable byte-stream socket.

With `AF_INET`, this means TCP.

------------------------------------------------------------------------

## 4. Why the third argument is `0`

Prototype:

``` cpp
socket(domain, type, protocol);
```

`0` means:

> Choose the default protocol for this socket type.

For

``` cpp
socket(AF_INET, SOCK_STREAM, 0);
```

the default protocol is TCP.

Equivalent to:

``` cpp
socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
```

For normal TCP applications, these are effectively identical.

------------------------------------------------------------------------

## 5. `setsockopt()`

General form:

``` cpp
setsockopt(fd, level, option, value, size);
```

It **changes configuration fields inside the kernel socket object**.

It does **not** send packets or bind the socket.

Example:

``` cpp
int opt = 1;

setsockopt(server_fd,
           SOL_SOCKET,
           SO_REUSEADDR,
           &opt,
           sizeof(opt));
```

Conceptually:

    socket.reuse_address = true;

------------------------------------------------------------------------

## 6. `SOL_SOCKET`

Sockets have settings at different layers.

    Socket
     ├── General socket options
     ├── TCP options
     └── IP options

`SOL_SOCKET` means:

> Modify a general socket option.

Examples:

-   SO_REUSEADDR
-   SO_KEEPALIVE
-   SO_RCVBUF
-   SO_SNDBUF

Other levels include:

-   IPPROTO_TCP
-   IPPROTO_IP

------------------------------------------------------------------------

## 7. `SO_REUSEADDR`

Allows the socket to reuse an address/port under the operating system's
reuse rules.

It is stored as a configuration option and later consulted by `bind()`.

------------------------------------------------------------------------

## 8. `fcntl()` and `O_NONBLOCK`

``` cpp
if (fcntl(server_fd, F_SETFL, O_NONBLOCK) == -1)
{
    std::cout << "fcntl failed\n";
    close(server_fd);
    server_fd = -1;
    return;
}
```

`fcntl()` changes properties of the **file descriptor**.

`F_SETFL` means:

> Set file status flags.

`O_NONBLOCK` means:

> Make operations on this descriptor non-blocking.

Conceptually:

    FD 3
     |
     +--> Socket

    blocking = false

Now calls like:

-   accept()
-   recv()
-   send()

return immediately instead of waiting.

Example:

Blocking socket:

    accept()
        |
        +---- waits until a client connects

Non-blocking socket:

    accept()
        |
        +---- no client?
                  |
                  +--> returns immediately (-1, errno = EAGAIN/EWOULDBLOCK)

This is essential for servers using `poll()` because one slow client
should never freeze the whole server.

------------------------------------------------------------------------

## 9. `volatile sig_atomic_t`

``` cpp
volatile sig_atomic_t g_running = 1;
```

Used for communication between normal code and a signal handler.

`sig_atomic_t`

-   Reads and writes are guaranteed to be atomic with respect to
    signals.

`volatile`

-   Forces the compiler to reload the variable from memory because a
    signal handler may change it asynchronously.

Typical use:

``` cpp
void handler(int)
{
    g_running = 0;
}

while (g_running)
{
    poll(...);
}
```

Pressing Ctrl+C changes the flag and the main loop exits safely.

------------------------------------------------------------------------

## 10. Port validation

    1024 <= port <= 65535

Why?

-   Ports are 16-bit unsigned integers.
-   Valid range: 0--65535.
-   Ports below 1024 are traditionally privileged on many Unix systems
    and may require root privileges.

Rejecting ports below 1024 makes the server more portable and avoids
permission issues.
