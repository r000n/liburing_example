about
--------
Example of usage liburing library for async write to file

This is TCP server that accepts connections on target port
and records received data into file with name *port*.txt

Client receives *ACCEPTED* message after 3s delay

requirements
--------
You need build liburing and put headers and compiled library here

- liburing.a  
- libburing  
  - barrier.h  
  - compat.h  
  - io_uring.h  
  - io_uring_version.h  

build
--------
gcc main.c liburing.a -o srv

run
--------
./srv *port*

