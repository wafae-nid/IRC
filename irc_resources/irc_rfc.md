
# Internet Relay Chat (IRC)

---

# Table of Contents

1. What is IRC?
2. What is an IRC Server?
3. IRC Architecture
4. Who Defines How IRC Works?
5. What is an RFC?
6. Why Does IRC Need an RFC?
7. What is RFC 1459?
8. What is RFC 2810/2811/2812?
9. What is Netcat (nc)?
10. Why is nc Used for IRC Projects?
11. Putting Everything Together

---

# What is IRC?

**Internet Relay Chat (IRC)** is one of the oldest Internet **protocols** for real-time text communication.

HTTP → used for websites
SMTP → used for email
FTP → used for file transfer
IRC → used for live text chatting


IRC = the protocol (the rules computers follow).
RFC 1459 / RFC 2812 = documents that describe those rules.


It allows many users to communicate through:

- Public chat rooms (channels)
- Private messages
- Server-to-server networks

Unlike modern messaging applications, IRC is extremely simple.

Everything exchanged between the client and server is just **plain text commands**.

Example:

```

NICK wafae
USER wafae 0 * :Wafae
JOIN #42
PRIVMSG #42 :Hello everyone!
QUIT
```

There is no JSON.

There is no HTTP.

There is no XML.

Just text lines ending with:

```
\r\n
```

---

# What is an IRC Server?

An IRC server is simply a program that waits for clients to connect using TCP.

Its responsibilities include:

- accepting client connections
- authenticating users
- storing connected clients
- creating channels
- routing messages
- enforcing IRC protocol rules
- disconnecting clients

Think of it as the central coordinator.

```
          Client A
             │
             │
             ▼
        IRC Server
       /     |     \
      /      |      \
     ▼       ▼       ▼
Client B  Client C Client D
```

The clients never communicate directly.

Everything always goes through the server.

---

# Example

Alice wants to send:

```
Hello
```

She types:

```
PRIVMSG #general :Hello
```

The server receives:

```
PRIVMSG #general :Hello
```

The server:

- checks Alice exists
- checks channel exists
- checks Alice joined it
- finds every member
- sends the message to everyone

```
Alice
   │
   ▼
IRC Server
   │
 ┌─┴─────────────┐
 ▼               ▼
Bob            Charlie
```

---

# IRC Architecture

```
+----------------------+
|      IRC Client      |
+----------------------+
          │
      TCP Socket
          │
          ▼
+----------------------+
|      IRC Server      |
+----------------------+
          │
          ▼
  Client Database

  Channel Database

  Message Routing
```

The client only sends commands.

The server performs all the work.

---

# Who Defines How IRC Works?

Imagine everyone wrote their own version of IRC.

One client sends:

```
LOGIN wafae
```

Another server expects:

```
CONNECT wafae
```

Another expects:

```
USER wafae
```

Nothing would work together.

There must be one shared language.

That language is defined by an **RFC**.

---

# What is an RFC?

RFC stands for:

> **Request For Comments**

Despite the name, an RFC is usually the official technical specification for an Internet protocol.

It explains:

- message formats
- commands
- replies
- errors
- syntax
- protocol behavior

Many Internet technologies are defined by RFCs.

Examples:

| Technology | RFC |
|------------|-----|
| TCP | RFC 793 |
| IP | RFC 791 |
| HTTP/1.1 | RFC 2616 |
| SMTP | RFC 5321 |
| IRC | RFC 1459 / RFC 2812 |

---

# Why Does IRC Need an RFC?

Suppose Alice uses client A.

Bob uses client B.

Charlie uses client C.

All of them should be able to connect to the same server.

That only works if everyone agrees on:

- command names
- parameters
- reply numbers
- message format

The RFC provides this agreement.

Think of it like grammar for a language.

Without grammar:

```
Person A:
HELLO

Person B:
START TALK

Person C:
SAY HELLO

Everyone speaks differently.
```

With grammar:

Everyone speaks the same language.

---

# What is RFC 1459?

RFC 1459 is the original IRC specification.

Published in:

```
May 1993
```

It introduced:

- NICK
- USER
- JOIN
- PART
- PRIVMSG
- MODE
- KICK
- TOPIC
- QUIT
- PING
- PONG

Many IRC servers still support nearly all of these commands today.

---

# What is RFC 2810 / RFC 2811 / RFC 2812?

As IRC evolved, the protocol was updated.

The newer specifications are:

### RFC 2810

Architecture of IRC networks.

---

### RFC 2811

Channels.

How channels work.

Permissions.

Modes.

---

### RFC 2812

Client protocol.

This is the document most IRC projects follow.

It explains exactly:

- every command
- parameters
- reply codes
- error codes

Example:

```
NICK wafae
```

```
USER wafae 0 * :Real Name
```

```
JOIN #42
```

```
PRIVMSG #42 :Hello
```

---

# Why Does 42 Use RFC 2812?

The IRC project at 42 asks you to build an IRC server compatible with standard IRC clients.

That means clients like:

- irssi
- HexChat
- WeeChat

already know how IRC works.

They simply send commands defined by RFC 2812.

Your server must understand those commands.

Otherwise the client cannot communicate with it.

---

# What is Netcat (nc)?

Netcat is a very small networking tool.

Its nickname is:

> "The TCP/IP Swiss Army Knife."

It can:

- open TCP connections
- send data
- receive data
- act like a client
- act like a server

For IRC, we mainly use it as a very simple client.

---

# Why Use nc?

Normally an IRC client has:

- windows
- buttons
- colors
- parsing
- command history

Netcat has none of these.

It simply lets you type raw bytes directly to the server.

Example:

```
$ nc localhost 6667
```
What is Netcat (nc)?

Netcat (nc) is a simple networking tool that can create TCP or UDP connections and send or receive raw data.

For the ft_irc project, it is commonly used as a basic TCP client to test an IRC server.

When you run:

nc localhost 6667

Netcat:

Creates a TCP socket.
Connects that socket to the server using TCP.
Sends exactly the bytes you type through the TCP connection.
Receives whatever bytes the server sends back and displays them on your terminal.

Netcat does not understand the IRC protocol. It does not parse IRC commands or implement IRC behavior. It simply provides a TCP connection and transports raw bytes between you and the server.

This makes Netcat an excellent tool for testing because it lets you communicate directly with your IRC server without any protocol-specific processing.Everything you type is sent directly over TCP.

Example:

```
PASS password
NICK wafae
USER wafae 0 * :Wafae
JOIN #42
PRIVMSG #42 :Hello
```

No formatting.

No hidden processing.

Just raw text.

---

# Why is nc Useful for Development?

Suppose your server has a bug.

A graphical IRC client might hide what it actually sends.

Netcat lets you see everything.

You control every command yourself.

You can test:

- malformed commands
- missing parameters
- invalid syntax
- multiple commands
- edge cases

Example:

```
NICK
```

or

```
JOIN
```

or

```
PRIVMSG
```

These help verify your server correctly handles protocol errors.

---

# Relationship Between nc and the IRC Server

```
           You
            │
            ▼
       Netcat (nc)
            │
      TCP Connection
            │
            ▼
       IRC Server
            │
      Parses Commands
            │
      Executes Actions
            │
      Sends Responses
            │
            ▼
       Netcat Displays
```

Netcat does not understand IRC.

It simply sends bytes.

Your IRC server interprets those bytes according to the IRC protocol (RFC 2812).

---

# Putting Everything Together

```
            RFC 2812
                │
                │
Defines the IRC protocol
                │
                ▼
        IRC Server implements it
                │
                ▼
      Listens on a TCP socket
                │
                ▼
      Client connects via TCP
                │
                ▼
       nc sends plain text
                │
                ▼
 Server parses the commands
                │
                ▼
 Server updates users/channels
                │
                ▼
 Server sends replies back
                │
                ▼
         nc displays them
```

---

# Summary

- **IRC** is a text-based protocol for real-time chat over TCP.
- An **IRC server** manages clients, channels, authentication, and message routing.
- Clients and servers communicate using plain text commands terminated by `\r\n`.
- An **RFC (Request For Comments)** is the official specification that defines how a protocol works.
- **RFC 1459** introduced the original IRC protocol, while **RFC 2810–2812** refined and standardized it.
- Following the RFC ensures that different IRC clients and servers can interoperate correctly.
- **Netcat (`nc`)** is a simple TCP client that sends and receives raw bytes.
- During development, `nc` is invaluable because it lets you manually test exactly what your IRC server receives and how it responds.
