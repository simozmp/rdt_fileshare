# #> Rdt_fileshare: RDT over UDP client/server CLI app for filesharing

This is a simple client/server application written in C, which implements Go Back N protocol in its own `read()`, `write()` functions to reliably send data with a UDP socket.

## Dependencies

To compile the application, gcc and make pkgs are required (both can be found in apt and pacman). On some tested systems (linux mint), libboost-all-dev is also required.

## Installation

To install the application (to work in localhost) you must clone the repository and compile from source. It is easily done by executing the following.

`$ git clone https://github.com/simozmp/rdt_fileshare && cd rdt_fileshare`

`$ make main`

After that, the executables can be found in the bin/ directory. You can execute them by giving:

`$ bin/server_main`

and, on a separate terminal

`$ bin/client_main`

You can also pass as argument the working directory you wanna work in. Eg:

`$ bin/server_main /home/username/Desktop` to make the server share files from your Desktop directory

`$ bin/client_main /home/username/Download` to make the client save files in your Download directory

## Configuration

You can find some application parameters to config in `src/gbn/config.h` such as transmission window size, packet loss simulation probability, default timeout. To make changes to these file effective, you must compile again the application with `make main`.

## Usage

The client CLI supports the following commands:
- `help`             To print usage string from server
- `list`             To list files in server
- `get file.ext`     To download _file.ext_ from server
- `put file.ext`     To upload _file.ext_ to server
- `disconnect`       To disconnect from server
- `connect`          To connect to server after a disconnect
- `close`            To disconnect from server and close the application

Both client and server write logs into the log/ folder
