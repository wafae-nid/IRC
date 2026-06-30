# `poll()` — Definition

`poll()` is a Linux/Unix **I/O multiplexing system call** that allows a program to monitor **multiple file descriptors** simultaneously and **sleep efficiently** until one or more of them become **ready for I/O operations**.

Instead of blocking on a single file descriptor (such as `accept()`, `recv()`, or `send()`), `poll()` blocks while watching many file descriptors at once. The kernel wakes the process only when an event occurs, such as:

# Kernel-Side Flow of `poll()`

When your program calls `poll()`, execution switches from **user space** to the **kernel**. The kernel is responsible for checking every file descriptor, putting the process to sleep if necessary, and waking it up when an event occurs.

---

## 1. Copy the `pollfd[]` Array

The first thing the kernel does is copy the user-provided `pollfd[]` array into kernel space.

Why?

- The kernel cannot directly trust or manipulate user-space memory.
- It creates its own internal copy that it can safely work with.

```
User Space
+--------------------+
| struct pollfd[]    |
+--------------------+
          |
          | poll()
          V
Kernel Space
+--------------------+
| Internal copy      |
+--------------------+
```

---

## 2. Allocate Internal Structures

The kernel allocates temporary structures that exist only during this `poll()` call.

### `poll_wqueues`

Responsible for managing:

- wait queues
- callbacks
- cleanup when `poll()` finishes

# `struct poll_wqueues`

The actual Linux kernel structure contains more fields than the simplified version. One important optimization is that it **stores a small number of `poll_table_entry` objects directly inside the structure** before allocating additional pages.

A conceptual view is:

```cpp
struct poll_wqueues
{
    poll_table pt;

    struct task_struct *polling_task;

    int error;

    poll_table_page *table;

    struct poll_table_entry inline_entries[N_INLINE_ENTRIES];

    int inline_index;
};
```

> **Note:** The exact field names and layout may differ slightly between Linux kernel versions, but these are the important components involved in how `poll()` works.

---

# Members

## `poll_table pt`

```cpp
poll_table pt;
```

Passed to every driver's `poll()` function.

It provides the callback (`qproc`) that drivers use to register the current thread on a file descriptor's wait queue.

---

## `task_struct *polling_task`

```cpp
struct task_struct *polling_task;
```

Points to the thread that called `poll()`.

When an event occurs, `poll_wake()` uses this pointer to wake the correct sleeping thread.

```
poll_wake()
      │
      ▼
polling_task
      │
      ▼
wake_up_process()
```

---

## `error`

```cpp
int error;
```

Stores any internal errors encountered while setting up polling.

Examples include:

- allocation failures
- wait queue registration failures

Normally it remains:

```
0
```

---

## `poll_table_page *table`

```cpp
poll_table_page *table;
```

Points to a linked list of dynamically allocated pages.

Each page contains multiple `poll_table_entry` objects.

## `struct poll_table_entry`

Each time a driver discovers that a file descriptor is **not ready**, it creates (or uses) a `poll_table_entry`.

A `poll_table_entry` represents **one registration of the calling thread in one wait queue**.

For example, if you're polling 100 sockets and none are ready, there will eventually be **100 `poll_table_entry` objects**—one for each socket's wait queue.

---

## Structure

Conceptually, a `poll_table_entry` looks like this:

```cpp
struct poll_table_entry
{
    struct file *filp;

    wait_queue_head_t *wait_address;

    wait_queue_entry_t wait;

    unsigned long key;
};
```

---

## Members

## `struct file *filp`

```cpp
struct file *filp;
```

Points to the kernel's `struct file` associated with the file descriptor.

Remember that a file descriptor is only an integer.

```
User Space

fd = 5
```

The kernel first converts it into:

```
struct file
```

The `poll_table_entry` keeps this pointer so it knows **which file this registration belongs to**.

```
fd
 │
 ▼
struct file
 │
 ▼
poll_table_entry
```

---

## `wait_queue_head_t *wait_address`

```cpp
wait_queue_head_t *wait_address;
```

Points to the wait queue of the resource.

Examples:

```
Socket
   │
   ▼
Wait Queue
```

or

```
Pipe
   │
   ▼
Wait Queue
```

When the resource becomes ready, this wait queue is traversed to wake any sleeping threads.

---

## `wait_queue_entry_t wait`

```cpp
wait_queue_entry_t wait;
```

Represents **this thread's node inside the wait queue**.

When the driver registers the thread, this object is inserted into the wait queue.

```
Socket Wait Queue

+----------------------+
| wait_queue_entry     |
+----------------------+
| wait_queue_entry     |
+----------------------+
| wait_queue_entry     | ← this thread
+----------------------+
| wait_queue_entry     |
+----------------------+
```

One important field inside `wait_queue_entry_t` is a function pointer:

```
func
```

For `poll()`, this function is:

```
poll_wake()
```

When the socket becomes ready:

```
Packet arrives
      │
      ▼
Socket driver
      │
      ▼
Walk wait queue
      │
      ▼
wait.func()
      │
      ▼
poll_wake()
      │
      ▼
Wake polling thread
```

---

## `unsigned long key`

```cpp
unsigned long key;
```

Stores the events that the thread is interested in.

Examples:

```
POLLIN
```

```
POLLOUT
```

```
POLLERR
```

When the driver wakes waiting threads, it compares the event that occurred with this key.

Example:

```
Thread wants:

POLLIN

Socket becomes writable:

POLLOUT

↓

No wake-up
```

But if:

```
Thread wants:

POLLIN

Socket receives data:

POLLIN

↓

Wake thread
```

This avoids waking processes for events they don't care about.

---

# Where Is `poll_table_entry` Stored?

Each `poll_table_entry` lives inside the `poll_wqueues` created for the current `poll()` call.

For a small number of monitored file descriptors, it is stored directly in:

```cpp
inline_entries[N_INLINE_ENTRIES]
```

If more entries are needed, the kernel allocates one or more:

```cpp
poll_table_page
```

objects to store additional `poll_table_entry` structures.

```
poll_wqueues
      │
      ├── inline_entries[0]
      ├── inline_entries[1]
      ├── inline_entries[2]
      ├── ...
      │
      └── poll_table_page
              │
              ├── poll_table_entry
              ├── poll_table_entry
              ├── poll_table_entry
              └── ...
```

---

# Life Cycle

```
Driver finds FD not ready
        │
        ▼
Create/use poll_table_entry
        │
        ▼
Insert wait_queue_entry into
the FD's wait queue
        │
        ▼
Thread sleeps
        │
        ▼
Resource becomes ready
        │
        ▼
Driver walks wait queue
        │
        ▼
poll_wake()
        │
        ▼
Wake thread
        │
        ▼
poll() returns
        │
        ▼
poll_table_entry destroyed
```

---

# Summary

| Member | Purpose |
|---------|---------|
| `struct file *filp` | The kernel file object being monitored. |
| `wait_queue_head_t *wait_address` | Points to the wait queue of that file. |
| `wait_queue_entry_t wait` | Represents this polling thread inside the wait queue and contains the `poll_wake()` callback. |
| `unsigned long key` | Records the events (`POLLIN`, `POLLOUT`, etc.) that should wake this thread. |These pages are only allocated when the inline storage is full.

```
poll_wqueues
      │
      ▼
+----------------------+
| poll_table_page      |
+----------------------+
| Entry                |
| Entry                |
| Entry                |
| Entry                |
+----------------------+
      │
      ▼
+----------------------+
| poll_table_page      |
+----------------------+
| Entry                |
| Entry                |
| Entry                |
+----------------------+
```

When `poll()` finishes, every page is freed.

---

## `poll_table_entry inline_entries[N_INLINE_ENTRIES]`

```cpp
struct poll_table_entry inline_entries[N_INLINE_ENTRIES];
```

To avoid allocating memory for every small `poll()` call, Linux keeps a small array of entries directly inside `poll_wqueues`.

The first few file descriptors use these entries.

```
poll_wqueues

inline_entries

+---------+
| Entry 0 |
+---------+
| Entry 1 |
+---------+
| Entry 2 |
+---------+
| Entry 3 |
+---------+
      ...
```

This optimization avoids heap allocations for the common case where applications monitor only a small number of file descriptors.

Only after these entries are exhausted does the kernel allocate `poll_table_page` structures.

---

## `inline_index`

```cpp
int inline_index;
```

Tracks how many inline entries have already been used.

Example:

```
inline_entries size = 32

FD 1  -> inline_entries[0]
FD 2  -> inline_entries[1]
FD 3  -> inline_entries[2]
...
FD 32 -> inline_entries[31]

FD 33
   │
   ▼
Allocate first poll_table_page
```

---

# How It Works

```
poll()
      │
      ▼
Create poll_wqueues
      │
      ▼
Use inline_entries
      │
      ├── Space available?
      │       │
      │       ├── Yes
      │       │      ▼
      │       │ Store poll_table_entry
      │       │
      │       └── No
      │              ▼
      │      Allocate poll_table_page
      │              │
      │              ▼
      │      Store additional entries
      │
      ▼
Sleep
      │
      ▼
Wake up
      │
      ▼
Free all pages
      │
      ▼
Destroy poll_wqueues
```

---

# Why Both Inline Entries and Pages?

Linux is optimized for the common case.

- **Small `poll()` calls** (a few file descriptors) use only `inline_entries`, avoiding dynamic memory allocation.
- **Large `poll()` calls** automatically allocate one or more `poll_table_page` structures to hold additional `poll_table_entry` objects.

This design minimizes allocation overhead while still allowing `poll()` to monitor thousands of file descriptors if needed.
### `poll_table`

Passed to every file descriptor so the driver can register the current thread if the FD is not yet ready.

```
poll()
   │
   ▼
Allocate
 ├── poll_wqueues
 └── poll_table
```

---

# 3. Scan Every File Descriptor (O(n))

The kernel now loops over every entry in the `pollfd[]` array.

```
for (i = 0; i < nfds; i++)
```

For each file descriptor:

---

## Step 1 — Get the File Object

The integer file descriptor is translated into the kernel's internal `struct file`.

```
fd
 │
 ▼
fget(fd)
 │
 ▼
struct file *
```

---

## Step 2 — Call the Driver's `poll()` Function

Every file type implements its own polling logic.

Examples:

- TCP socket
- UDP socket
- Pipe
- Character device
- Terminal

The kernel calls:

```cpp
file->f_op->poll(...)
```

The driver checks whether the resource is ready.

---

## Step 3 — If Ready

If data is already available (or writing is possible), the driver immediately returns an event mask.

Example:

```
POLLIN
```

or

```
POLLOUT
```

The kernel stores this inside

```
revents
```

for that `pollfd`.

No sleeping is needed for this FD.

---

## Step 4 — If Not Ready

If the resource is not ready:

The driver registers the current thread inside the file descriptor's wait queue.

This registration happens through the provided `poll_table`.

The kernel internally installs a callback:

```
poll_wake()
```

This callback will later wake the sleeping thread.

```
Current Thread
      │
      ▼
Wait Queue
      │
      ▼
poll_wake()
```

---

# 4. Thread Parking

After every FD has been checked:

### If at least one FD is ready

The kernel immediately returns to user space.

No sleeping occurs.

---

### If none are ready

If

```
timeout > 0
```

the kernel parks the current thread.

The thread enters an **interruptible sleep state**.

```
Running
   │
   ▼
Sleeping
(waiting inside poll())
```

The CPU is now free to run other processes.

---

# 5. Wake-Up

Later, suppose a packet arrives on a socket.

```
Network Card
      │
      ▼
TCP Stack
      │
      ▼
Socket becomes ready
```

The socket driver walks its wait queue and invokes the registered callback.

```
poll_wake()
```

The callback wakes the sleeping thread.

```
Socket Ready
      │
      ▼
poll_wake()
      │
      ▼
Sleeping Thread
      │
      ▼
Runnable Again
```

---

# 6. Cleanup

Before returning, the kernel removes everything it registered earlier.

It:

- unregisters callbacks
- removes the thread from wait queues
- destroys the temporary `poll_wqueues`

Everything allocated for this call disappears.

---

# 7. Copy Results Back

The kernel updates every `pollfd.revents` field that became ready.

Then it copies the modified array back to user space.

```
Kernel pollfd[]
        │
        ▼
User pollfd[]
```

---

# 8. Return

Finally,

```cpp
poll()
```

returns the number of ready file descriptors.

Example:

```cpp
int ready = poll(fds, nfds, timeout);

if (ready > 0)
{
    // One or more descriptors are ready
}
```

---

# Why `poll()` Is Still O(n)

Although `poll()` improves upon `select()`, it still suffers from a fundamental scalability problem.

The kernel must inspect **every monitored file descriptor** on every call, whether it has activity or not.

```
10,000 FDs

poll()
   │
   ▼
FD 1
FD 2
FD 3
...
FD 9999
FD 10000
```

Even if only **one** socket has received data, the kernel still scans all 10,000 entries.

Time complexity:

```
O(n)
```

---

# Core Performance Bottlenecks

### 1. O(n) Rescanning

After the thread wakes up, the kernel scans the entire `pollfd[]` array again.

It has no shortcut to jump directly to the ready descriptors.

---

### 2. Linear Memory Copy

Every call copies the complete `pollfd[]` array:

- User space → Kernel space
- Kernel space → User space

This happens on every invocation.

---

### 3. No Event Persistence

`poll()` does **not** remember the monitored file descriptors between calls.

Every time you call `poll()`:

- the array is copied again,
- callbacks are registered again,
- wait queues are rebuilt,
- everything is destroyed afterward.

The kernel repeats the entire setup from scratch.

---

# The Scalability Problem

Example:

```
10,000 connections
```

The kernel must:

- scan 10,000 `pollfd` structures,
- rebuild wait queues,
- register callbacks,
- copy the array,
- remove callbacks,
- copy results back.

Approximate memory traffic:

```
10,000 pollfd structures
≈ 12 bytes each

≈ 120 KB copied
```

per `poll()` call.

If `poll()` runs frequently, this results in **megabytes of unnecessary memory traffic per second**, making it inefficient for very large numbers of concurrent connections.

---

# Summary

```
poll()
   │
   ▼
Copy pollfd[] into kernel
   │
   ▼
Allocate poll_wqueues + poll_table
   │
   ▼
Scan every FD (O(n))
   │
   ├── Ready → set revents
   │
   └── Not Ready
         │
         ▼
   Register wait queue + poll_wake()
   │
   ▼
Any ready?
   │
   ├── Yes → return immediately
   │
   └── No
         │
         ▼
     Sleep (interruptible)
         │
         ▼
   Event occurs
         │
         ▼
    poll_wake()
         │
         ▼
     Wake thread
         │
         ▼
 Remove callbacks & wait queues
         │
         ▼
Copy revents back to user space
         │
         ▼
Return number of ready FDs
```
