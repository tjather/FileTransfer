# FileTransfer  

## Overview  
This project implements a simple file transfer protocol using UDP sockets with a limited set of commands. The system consists of a **client** and a **server**, allowing users to transfer files, list server directory contents, and delete files.  

## Features  
- **File Transfer**  
  - `get [file_name]`: Client requests a file from the server.  
  - `put [file_name]`: Client uploads a file to the server.  
- **File Management**  
  - `delete [file_name]`: Removes a file from the server.  
  - `ls`: Lists all files in the server's directory.  
- **Session Management**  
  - `exit`: Terminates the client session.  

## Components  
### Client (`udp_client.c`)  
- Connects to the server using UDP.  
- Sends commands to the server and handles responses.  
- Implements file transmission using sliding window and acknowledgment mechanisms.  

### Server (`udp_server.c`)  
- Listens for client requests on a specified UDP port.  
- Processes file transfer, deletion, and directory listing commands.  
- Uses a sliding window protocol for efficient file transfers.  
