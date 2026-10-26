# Capstone-Project
# Bus Reservation System




# Problem Statement:

This project is a trip bus seat reservation system made in C. Passengers can connect to a server over TCP to:
- View trips and seat avilability
- Hold a seat then confirm the booking
- Cancel a booking
- Join a waitlist when a trip is full

The server stores all the trips in a GDM database so bookings survive restarts. It handles multiple clients at once and uses file locking to prevent two passengers from booking the same seat.

# Design & Architecture: 

File layout:
- common.h: Shared error codes and size limits used by every file
- seatmap.h / seatmap.c: Manages the seats on one bus (free, held, booked)
- waitlist.h / waitlist.c: A first-come, first-served waiting line for full trips
- trip.h / trip.c: One bus trip; combines the seats and waitlist and moves waitlisted people into freed seats
- db.h / db.c: Saves, loads, updates, and deletes trips in the GDBM database, with file locking
- protocol.h / protocol.c: Reads, sends, and understands the text commands between client and server
- server.c: The server; handles each connected client in its own process
- client.c: The program users type commands into
- Makefile: Builds the server and client with one make command

Key structures
- Seat: a seat number, a status (FREE, HELD, or BOOKED), the passenger name, and the time a hold expires.
- SeatMap: a fixed array of up to 56 seats.
- Waitlist: a circular queue of up to 20 names.
- Trip: an ID, origin, destination, departure time, one SeatMap, and one Waitlist.
# All structs use fixed-size arrays with no pointers. That way a whole Trip can be saved to and loaded from the database as one block.

Persistence strategy
- Storage: All trips are saved in one GDBM file, trips.gdbm. Each trip is stored under its trip ID.
- CRUD: db_create, db_read, db_update, db_delete, and db_list add, load, change, remove, and list trips.
- Safety checks: Records with the wrong size or impossible values are rejected, and new trips start fully zeroed.


Network protocol
- The client sends one command per line. The server replies with one or more lines and ends with END.
Commands:
- LIST: Shows all trips and how many seats are open
- SHOW <trip>: Shows every seat on one trip
- ADDTRIP <id> <from> <to> <YYYY-MM-DDTHH:MM> <seats>: Creates a new trip
- DELTRIP <id>: Deletes a trip
- HOLD <trip> <seat> <name>: Holds a seat for 2 minutes
- CONFIRM <trip> <seat> <name>: Books a seat you're holding
- CANCEL <trip> <seat> <name>: Cancels a seat; the next person on the waitlist gets it
- WAITLIST <trip> <name>: Joins the waitlist when a trip is full
- QUIT: Disconnects


# Pros & Cons of Solution:

Pros:
- Simple storage: each trip is saved as one block, so no conversion code is needed
- Each file has one job and can be tested on its own
- Locking prevents two people from booking the same seat
- The waitlist works automatically when a seat opens up
- Bad input, disconnects, and corrupt data return errors instead of crashing

Cons:
- No logins: anyone who knows a passenger's name can cancel their seat
- Every trip uses space for 56 seats and 20 waitlist names, even if smaller
- Changing struct sizes makes old database files unreadable
- One lock for the whole database, so bookings on different trips still wait for each other
- One process per client, with no limit on how many can connect
- The server reads one byte at a time, which is slow under heavy traffic
- LIST shows at most 128 trips
- macOS leaks reported 4 KB of unexplained memory per client process (cause not confirmed)

Future expansions: user accounts, per-trip locking, seat classes, and multi-stop routes

# AI Audit reflection

AI Architectural review prompt:


<img width="311" height="168" alt="image" src="https://github.com/user-attachments/assets/c10df3c2-8545-4af9-a39a-9a662de4d59a" />

Result:



<img width="387" height="562" alt="image" src="https://github.com/user-attachments/assets/babbaf02-6b0c-43ca-857d-12f69b6962af" />
<img width="387" height="535" alt="image" src="https://github.com/user-attachments/assets/232604cc-e5d8-4c8a-b8d6-9c91072a9c88" />


Bugs and leaks prompt:

<img width="938" height="125" alt="image" src="https://github.com/user-attachments/assets/2ce25a3f-a609-4891-9a79-566184db3e2e" />

Result:

<img width="1187" height="790" alt="image" src="https://github.com/user-attachments/assets/9b9a725d-f8e1-4a8d-a112-452ca1e45da4" />

<img width="982" height="202" alt="image" src="https://github.com/user-attachments/assets/e665d8e5-9f8d-44f7-8041-682452d53ab5" />



# Demo Video Link

https://mediaspace.umn.edu/media/t/1_06svoocl
