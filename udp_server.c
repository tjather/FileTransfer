/*
 * udpserver.c - A simple UDP echo server
 * usage: udpserver <port>
 */

#include <arpa/inet.h>
#include <dirent.h> // directory traversing
#include <netdb.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

#define BUFSIZE 1024

/*
 * packet struct
 */
typedef struct {
    int sequenceNum;
    char frame[BUFSIZE];
    int type; // 1 for Data 2 for ack
} packet;

/*
 * ackHandler handles acknoledgments and returns the sequence number of the last recieved acknolwedgemnet
 * based on Synchronous I/O Multiplexing in Beej's Guide to Network Programming
 */
int ackHandler(int sockfd, int ackNumber, struct sockaddr_in clientaddr, int clientlen)
{ // based on Synchronous I/O Multiplexing in Beej's Guide to Network Programming
    int n; /* message byte size */
    packet ack;
    struct timeval time;
    fd_set readfds;

    // Initialize the file descriptor set
    FD_ZERO(&readfds);
    FD_SET(sockfd, &readfds);

    // Wait 5 seconds before timing out
    time.tv_sec = 5;
    time.tv_usec = 0;

    bzero(&ack, BUFSIZE);

    // strcpy(ack.frame, "testframe");

    if (select(sockfd + 1, &readfds, NULL, NULL, &time) > 0) {
        fflush(stdout);

        n = recvfrom(sockfd, &ack, sizeof(ack), 0, (struct sockaddr*)&clientaddr, &clientlen);
        if (n < 0)
            error("ERROR in recvfrom");

        // printf("ack tupe %d\n", ack.type);
        // printf("ack num %d\n", ack.sequenceNum);
        // printf("ack data %s\n", ack.frame);
        // printf("ack num %d\n", ack.sequenceNum);
        // printf("ack type %d\n", ack.type);

        if (ack.sequenceNum == ackNumber + 1 && ack.type == 2) {
            ackNumber++;
            // printf("ACK received\n");
        }
    } // else {
    //     // printf("Timeout in ackHandler\n");
    // }
    // printf("number: %d\n", ackNumber);
    return ackNumber;
}

/*
 * error - wrapper for perror
 */
void error(char* msg)
{
    perror(msg);
    exit(1);
}

int main(int argc, char** argv)
{
    int sockfd; /* socket */
    int portno; /* port to listen on */
    int clientlen; /* byte size of client's address */
    struct sockaddr_in serveraddr; /* server's addr */
    struct sockaddr_in clientaddr; /* client addr */
    struct hostent* hostp; /* client host info */
    char buf[BUFSIZE]; /* message buf */
    char* hostaddrp; /* dotted decimal host addr string */
    int optval; /* flag value for setsockopt */
    int n; /* message byte size */
    char command[BUFSIZE];
    char fileName[BUFSIZE];
    int SWS = 5;
    int RWS = 5;

    /*
     * check command line arguments
     */
    if (argc != 2) {
        fprintf(stderr, "usage: %s <port>\n", argv[0]);
        exit(1);
    }
    portno = atoi(argv[1]);

    /*
     * socket: create the parent socket
     */
    sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    if (sockfd < 0)
        error("ERROR opening socket");

    /* setsockopt: Handy debugging trick that lets
     * us rerun the server immediately after we kill it;
     * otherwise we have to wait about 20 secs.
     * Eliminates "ERROR on binding: Address already in use" error.
     */
    optval = 1;
    setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR,
        (const void*)&optval, sizeof(int));

    /*
     * build the server's Internet address
     */
    bzero((char*)&serveraddr, sizeof(serveraddr));
    serveraddr.sin_family = AF_INET;
    serveraddr.sin_addr.s_addr = htonl(INADDR_ANY);
    serveraddr.sin_port = htons((unsigned short)portno);

    /*
     * bind: associate the parent socket with a port
     */
    if (bind(sockfd, (struct sockaddr*)&serveraddr,
            sizeof(serveraddr))
        < 0)
        error("ERROR on binding");

    /*
     * main loop: wait for a datagram, then echo it
     */
    clientlen = sizeof(clientaddr);
    while (1) {

        /*
         * recvfrom: receive a UDP datagram from a client
         */
        bzero(buf, BUFSIZE);
        bzero(command, BUFSIZE);
        bzero(fileName, BUFSIZE);

        n = recvfrom(sockfd, buf, BUFSIZE, 0, (struct sockaddr*)&clientaddr, &clientlen);
        printf("buf data %s\n", buf);
        if (n < 0)
            error("ERROR in recvfrom");

        /*
         * gethostbyaddr: determine who sent the datagram
         */
        // hostp = gethostbyaddr((const char*)&clientaddr.sin_addr.s_addr,
        //     sizeof(clientaddr.sin_addr.s_addr), AF_INET);
        // if (hostp == NULL)
        //     error("ERROR on gethostbyaddr");
        hostaddrp = inet_ntoa(clientaddr.sin_addr);
        if (hostaddrp == NULL)
            error("ERROR on inet_ntoa\n");
        // printf("server received datagram from %s (%s)\n",
        //     hostp->h_name, hostaddrp);
        printf("server received %d/%d bytes: %s\n", strlen(buf), n, buf);

        sscanf(buf, "%s %s", command, fileName);

        if (!strncmp(command, "get", 3)) {
            packet dataPacket;
            dataPacket.type = 1;

            bzero(buf, BUFSIZE);
            FILE* file = fopen(fileName, "rb");
            if (file == NULL) {
                printf("File %s does not exsit.\n", fileName);
                strcpy(dataPacket.frame, "File does not exist");
                dataPacket.type = 1;

                n = sendto(sockfd, &dataPacket, sizeof(dataPacket), 0, (struct sockaddr*)&clientaddr, clientlen);
                if (n < 0)
                    error("ERROR in sendto");
            } else {
                // Declare variables needed for sender sliding window
                int LAR = -1;
                int LFS = 0;
                double timeout = 10;
                int bytes = 1;

                char frameBuffer[BUFSIZE][BUFSIZE];

                clock_t timeLAR = clock();

                n = sendto(sockfd, &dataPacket, sizeof(dataPacket), 0, (struct sockaddr*)&clientaddr, clientlen);
                if (n < 0)
                    error("ERROR in sendto");

                while (1) {
                    // printf("time LAR1: %d\n", timeLAR);
                    // If a timeout doens't occur then send each frame that has been buffered
                    if (((double)(clock() - timeLAR) / CLOCKS_PER_SEC) > timeout) {
                        for (int i = LAR + 1; i < LAR + 1 + SWS; i++) {
                            dataPacket.type = 1;
                            n = sendto(sockfd, frameBuffer[i], sizeof(dataPacket), 0, (struct sockaddr*)&clientaddr, clientlen);
                            if (n < 0)
                                error("ERROR in sendto");
                        }

                        timeLAR = clock();
                        LFS = LAR + 1 + SWS;
                    }

                    // If the frame is in the window then accept it and buffer the frame
                    if ((LFS - LAR) < SWS) {
                        bzero(&dataPacket, sizeof(dataPacket));
                        dataPacket.sequenceNum = LFS;
                        dataPacket.type = 1;
                        bytes = fread(dataPacket.frame, 1, BUFSIZE, file);

                        // printf("Test:%s\n", dataPacket.frame);
                        // printf("Bytes: %d\n", bytes);

                        if (bytes > 0) {
                            int totalSize = bytes + sizeof(dataPacket.sequenceNum) + sizeof(dataPacket.type);
                            n = sendto(sockfd, &dataPacket, totalSize, 0, (struct sockaddr*)&clientaddr, clientlen);
                            if (n < 0)
                                error("ERROR in sendto");

                            strncpy(frameBuffer[LFS % SWS], &dataPacket, totalSize);
                            LFS++;
                        }
                    }

                    // printf("time LFS: %d\n", LFS);
                    // printf("time LAR: %d\n", LAR);
                    // If there is no more data exit the loop
                    if (bytes <= 0 && (LFS == LAR + 1)) {
                        break;
                    }

                    // When the data has been buffer send an acknolwedgement
                    if (ackHandler(sockfd, LAR, clientaddr, clientlen) == LAR + 1) {
                        LAR++;
                        timeLAR = clock();
                    } else {
                        break;
                    }
                    // printf("time LAR2: %d\n", timeLAR);
                }

                bzero(dataPacket.frame, BUFSIZE);
                strncpy(dataPacket.frame, "EOF", 3);

                n = sendto(sockfd, &dataPacket, sizeof(dataPacket), 0, (struct sockaddr*)&clientaddr, clientlen);
                if (n < 0)
                    error("ERROR in sendto");

                fflush(file);
                fclose(file);
            }
        } else if (!strncmp(command, "put", 3)) {
            // Create Variables for the Reciver for Get Back N Sliding Window
            packet dataPacket;
            dataPacket.type = 1;

            int LFR = -1;
            int LAF = 0;
            char frameBuffer[BUFSIZE][BUFSIZE];

            struct timeval time;
            time.tv_sec = 10;
            time.tv_usec = 0;

            fd_set readfds;

            FILE* file = fopen(fileName, "wb");
            printf("Got file %s \n", fileName);
            if (file == NULL)
                error("ERROR in fopen");

            while (1) {
                FD_ZERO(&readfds);
                FD_SET(sockfd, &readfds);
                // printf("sadkfjksjdafl\n");

                int selectReturnValue = select(sockfd + 1, &readfds, NULL, NULL, &time);
                // printf("select return: %d\n", selectReturnValue);

                if (selectReturnValue == 0) {
                    printf("Timeout occured\n");
                    fclose(file);
                    break;
                }

                bzero(&dataPacket, BUFSIZE);

                n = recvfrom(sockfd, &dataPacket, sizeof(dataPacket), 0, (struct sockaddr*)&clientaddr, &clientlen);
                if (n < 0)
                    error("ERROR in recvfrom");

                // printf("%s", dataPacket.frame);
                // LAF = dataPacket.sequenceNum;

                if (!strncmp(dataPacket.frame, "EOF", 3)) {
                    fflush(file);
                    fclose(file);
                    break;
                }
                // printf("LAF: %d\n", LAF);

                LAF = dataPacket.sequenceNum;

                // printf("data packet: %d\n", dataPacket.sequenceNum);
                // printf("LAF: %d\n", LAF);
                // printf("LFR: %d\n", LFR);
                // printf("LOOK: %s", dataPacket.frame);

                // If the frame is in the window accept it and only write the frames in order
                if (LAF - LFR < RWS) {
                    if (LFR + 1 == LAF) {
                        printf("data: %s", dataPacket.frame);
                        fwrite(dataPacket.frame, 1, n - sizeof(dataPacket.type) - sizeof(dataPacket.sequenceNum), file);
                        LFR++;
                    }

                    // When the data has been written to the file send an acknolwedgement
                    packet ack;
                    bzero(&ack, BUFSIZE);
                    ack.sequenceNum = LAF; // ACK the last received packet
                    strcpy(ack.frame, "acknowledgement");
                    ack.type = 2; // Set packet to ACK type

                    // printf("sending ack back\n");

                    // printf("ack num %d\n", ack.sequenceNum);
                    // printf("ack frame %s\n", ack.frame);

                    n = sendto(sockfd, &ack, sizeof(ack), 0, (struct sockaddr*)&clientaddr, clientlen);

                    // printf("ack frame num %d\n", ack.sequenceNum);

                    if (n < 0)
                        error("ERROR in sendto");
                    // printf("ack tupe %d\n", ack.type);

                    // printf("sending ack back\n");
                }
            }

        } else if (!strncmp(command, "delete", 6)) {
            if (access(fileName, F_OK) != 0) {
                printf("File %s does not exsit.\n", fileName);
                strcpy(buf, "Not found");
                n = sendto(sockfd, buf, strlen(buf), 0,
                    (struct sockaddr*)&clientaddr, clientlen);
                if (n < 0)
                    error("ERROR in sendto");
                continue;
            }

            if (remove(fileName) < 0) {
                printf("File %s does not exsit\n", fileName);
                strcpy(buf, "Error");

                n = sendto(sockfd, buf, strlen(buf), 0,
                    (struct sockaddr*)&clientaddr, clientlen);
                continue;
            }

            // printf("Deleted file %s.\n", fileName);
            bzero(buf, BUFSIZE);
            strcpy(buf, "Deleted");
            n = sendto(sockfd, buf, 14, 0, (struct sockaddr*)&clientaddr, clientlen);
            if (n < 0)
                error("ERROR in sendto");
        } else if (!strncmp(command, "ls", 2)) {

            DIR* directory = opendir(".");
            struct dirent* dir;
            if (directory) {
                while ((dir = readdir(directory)) != NULL) {
                    snprintf(buf, BUFSIZE, "%s\n", dir->d_name);
                    /*
                     * sendto: echo the input back to the client
                     */
                    // printf("%s\n", dir->d_name);

                    n = sendto(sockfd, buf, BUFSIZE, 0, (struct sockaddr*)&clientaddr, clientlen);
                    if (n < 0)
                        error("ERROR in sendto");
                }
                closedir(directory);

                strcpy(buf, "LS done");
                printf("%s\n", buf);
                printf("TEST\n");

                n = sendto(sockfd, buf, BUFSIZE, 0, (struct sockaddr*)&clientaddr, clientlen);
                if (n < 0)
                    error("ERROR in sendto");
            }
        } else if (!strncmp(command, "exit", 4)) {
            /*
             * sendto: echo the input back to the client
             */
            n = sendto(sockfd, buf, strlen(buf), 0,
                (struct sockaddr*)&clientaddr, clientlen);
            if (n < 0)
                error("ERROR in sendto");
            printf("Exiting the program\n");
            fflush(NULL);
            break;
        } else {
            printf("Impossible command given to server!\n");
        }
    }
}
