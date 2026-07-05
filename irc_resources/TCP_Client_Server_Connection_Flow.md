# TCP Client/Server Connection Flow

## Client Side

1.  `socket()`

-   Kernel creates:
    -   `struct socket`
    -   `struct sock`
    -   `struct tcp_sock`
-   TCP state: `CLOSED`

2.  `connect()`

-   Validates the socket and destination address.
-   If no `bind()` was performed:
    -   Chooses a local IP based on the routing table.
    -   Chooses a free ephemeral (temporary) local port.
-   Creates TCP connection state.
-   Changes state to `SYN_SENT`.
-   Sends a SYN packet.

3.  Waits for reply

-   If SYN+ACK arrives:
    -   Sends ACK.
    -   State becomes `ESTABLISHED`.
    -   `connect()` returns 0.
-   If RST arrives:
    -   `connect()` fails (`ECONNREFUSED`).
-   If no reply after retries:
    -   `connect()` fails (`ETIMEDOUT`).

The client keeps the SAME socket created by `socket()`. No new socket is
created.

------------------------------------------------------------------------

## Server Side

1.  `socket()`

-   Creates:
    -   `struct socket`
    -   `struct sock`
    -   `struct tcp_sock`

2.  `bind()`

-   Associates the socket with a local IP and port.

3.  `listen()`

-   Changes TCP state to `LISTEN`.
-   This socket is the listening socket.
-   It never becomes the communication socket.

4.  Client sends SYN

-   The listening socket receives the SYN.
-   The kernel allocates a NEW:
    -   `struct socket`
    -   `struct sock`
    -   `struct tcp_sock`
-   Initial state: `SYN_RECEIVED`.

5.  Handshake completes

-   Server sends SYN+ACK.
-   Receives client's ACK.
-   New socket state becomes `ESTABLISHED`.

6.  Accept queue

-   The NEW connected socket is placed in the accept queue.
-   The listening socket remains in `LISTEN`.

7.  `accept()`

-   Removes one connected socket from the accept queue.
-   Returns a new file descriptor referring to that connected socket.

------------------------------------------------------------------------

## Important

The client socket is NEVER transferred to the server.

The server creates its own socket for each connection.

The two sockets communicate over TCP but are independent kernel objects.

    Client Process                 Server Process

    fd=3                           listen_fd=3
       |                               |
    client socket                  listening socket (LISTEN)
       |                               |
    client sock                    listening sock
       |                               |
    client tcp_sock                listening tcp_sock

                                       |
                                  Incoming SYN
                                       |
                            Kernel creates NEW socket
                                       |
                                 accepted socket
                                       |
                                 accepted sock
                                       |
                               accepted tcp_sock
                                       |
                               accept() -> client_fd=4

## Summary

Client: - One socket created by `socket()`. - Same socket goes through
CLOSED -\> SYN_SENT -\> ESTABLISHED.

Server: - One listening socket. - One NEW socket created for every
incoming connection. - `accept()` returns a file descriptor for the NEW
socket, never for the listening socket.
