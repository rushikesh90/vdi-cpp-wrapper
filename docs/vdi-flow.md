### Actors

1. SQL Server
   - Drives backup/restore
   - Issues VDI commands

2. VDI Client (this project)
   - Receives commands
   - Processes data
   - Signals completion

### Lifecycle

1. Create device set
2. Open virtual devices
3. Enter command loop
4. Process commands
5. Complete each command
6. Exit on Close

### Command Loop

VDI uses a blocking command loop:

- Client calls GetCommand()
- SQL Server provides a command
- Client processes it
- Client must call CompleteCommand()

### Command Types

- VDC_Read
  SQL Server expects data from client (restore scenario)

- VDC_Write
  SQL Server sends backup data to client

- VDC_Flush
  Ensure data durability

- VDC_Close

### Data Flow

Data is not pulled arbitrarily.

Instead:

- SQL Server provides buffer via command
- Client reads/writes using that buffer
- Client completes command

Key point:
Buffers are owned by VDI until CompleteCommand is called.

### Constraints

- Commands must be processed in order
- Each command must be completed exactly once
- Blocking behavior is expected (GetCommand)
- Incorrect handling causes deadlocks
