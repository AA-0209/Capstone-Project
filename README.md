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
