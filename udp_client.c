/*
 * udpclient.c - A simple UDP client
 * usage: udpclient <host> <port>
 */
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
int ackHandler(int sockfd, int ackNumber, struct sockaddr_in serveraddr, int serverlen)
{
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

        n = recvfrom(sockfd, &ack, sizeof(ack), 0, &serveraddr, &serverlen);
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
 * displayMenu - Menu
 */
void displayMenu()
{
    printf("\n=======Enter one of the following commands=======\n get [file_name]\n put [file_name]\n delete [file_name]\n ls\n exit\n");
    printf("Please enter a command: ");
}

/*
 * error - wrapper for perror
 */
void error(char* msg)
{
    perror(msg);
    exit(0);
}

int main(int argc, char** argv)
{
    int sockfd, portno, n;
    int serverlen;
    struct sockaddr_in serveraddr;
    struct hostent* server;
    char* hostname;
    char buf[BUFSIZE];
    char command[BUFSIZE];
    char fileName[BUFSIZE];
    int SWS = 5;
    int RWS = 5;

    /* check command line arguments */
    if (argc != 3) {
        fprintf(stderr, "usage: %s <hostname> <port>\n", argv[0]);
        exit(0);
    }
    hostname = argv[1];
    portno = atoi(argv[2]);

    /* socket: create the socket */
    sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    if (sockfd < 0)
        error("ERROR opening socket");

    /* gethostbyname: get the server's DNS entry */
    server = gethostbyname(hostname);
    if (server == NULL) {
        fprintf(stderr, "ERROR, no such host as %s\n", hostname);
        exit(0);
    }

    /* build the server's Internet address */
    bzero((char*)&serveraddr, sizeof(serveraddr));
    serveraddr.sin_family = AF_INET;
    bcopy((char*)server->h_addr, (char*)&serveraddr.sin_addr.s_addr, server->h_length);
    serveraddr.sin_port = htons(portno);

    while (1) {
        bzero(buf, BUFSIZE);
        bzero(command, BUFSIZE);
        bzero(fileName, BUFSIZE);

        /* get a command from the user */
        displayMenu();
        fgets(buf, BUFSIZE, stdin);
        sscanf(buf, "%s %s", command, fileName);

        if (!strncmp(command, "get", 4)) {
            packet dataPacket;
            dataPacket.type = 1;

            serverlen = sizeof(serveraddr);

            n = sendto(sockfd, buf, strlen(buf), 0, &serveraddr, serverlen);
            if (n < 0)
                error("ERROR in sendto");

            n = recvfrom(sockfd, &dataPacket, sizeof(dataPacket), 0, &serveraddr, &serverlen);
            if (n < 0)
                error("ERROR in recvfrom");

            if (!strncmp(dataPacket.frame, "File does not exist", 19)) {
                printf("The server doesn't have file %s\n", fileName);

            } else {
                // Create Variables for the Reciver for Get Back N Sliding Window
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

                    int selectReturnValue = select(sockfd + 1, &readfds, NULL, NULL, &time);
                    // printf("select return: %d\n", selectReturnValue);
                    if (selectReturnValue == 0) {
                        printf("Timeout occured\n");
                        fclose(file);
                        break;
                    }

                    bzero(&dataPacket, BUFSIZE);

                    n = recvfrom(sockfd, &dataPacket, sizeof(dataPacket), 0, &serveraddr, &serverlen);
                    if (n < 0)
                        error("ERROR in recvfrom");

                    // printf("%s", dataPacket.frame);
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
                            fwrite(dataPacket.frame, 1, n - sizeof(dataPacket.type) - sizeof(dataPacket.sequenceNum), file);
                            LFR++;
                        }

                        // When the data has been written to the file send an acknolwedgement
                        packet ack;
                        bzero(&ack, BUFSIZE);
                        ack.sequenceNum = LAF; // ACK the last received packet
                        strcpy(ack.frame, "acknowledgement");
                        ack.type = 2; // Set packet to ACK type

                        // printf("ack num %d\n", ack.sequenceNum);
                        // printf("ack frame %s\n", ack.frame);

                        n = sendto(sockfd, &ack, sizeof(ack), 0, &serveraddr, serverlen);

                        // printf("ack frame num %d\n", ack.sequenceNum);

                        if (n < 0)
                            error("ERROR in sendto");
                        // printf("ack tupe %d\n", ack.type);

                        // printf("sending ack back\n");
                    }
                }
            }
        } else if (!strncmp(command, "put", 3)) {
            packet dataPacket;
            dataPacket.type = 1;
            int bytes = 1;

            serverlen = sizeof(serveraddr);
            // printf("%s", buf);
            n = sendto(sockfd, buf, strlen(buf), 0, &serveraddr, serverlen);
            if (n < 0)
                error("ERROR in sendto");

            FILE* file = fopen(fileName, "rb");

            if (file == NULL) {
                printf("File %s does not exsit.\n", fileName);
            } else {
                // Declare variables needed for sender sliding window
                int LAR = -1;
                int LFS = 0;
                double timeout = 10;
                int bytes = 1;

                char frameBuffer[BUFSIZE][BUFSIZE];

                clock_t timeLAR = clock();
                while (1) {
                    // printf("time LAR1: %d\n", timeLAR);
                    // If a timeout doens't occur then send each frame that has been buffered
                    if (((double)(clock() - timeLAR) / CLOCKS_PER_SEC) > timeout) {
                        for (int i = LAR + 1; i < LAR + 1 + SWS; i++) {
                            dataPacket.type = 1;
                            printf("ajsdlfjlkdsaf\n");
                            n = sendto(sockfd, frameBuffer[i], sizeof(dataPacket), 0, &serveraddr, serverlen);
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
                            n = sendto(sockfd, &dataPacket, totalSize, 0, &serveraddr, serverlen);
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
                    if (ackHandler(sockfd, LAR, serveraddr, serverlen) == LAR + 1) {
                        LAR++;
                        timeLAR = clock();
                    } else {
                        break;
                    }

                    // printf("time LAR2: %d\n", timeLAR);
                }

                bzero(dataPacket.frame, BUFSIZE);
                strncpy(dataPacket.frame, "EOF", 3);

                n = sendto(sockfd, &dataPacket, sizeof(dataPacket), 0, &serveraddr, serverlen);
                if (n < 0)
                    error("ERROR in sendto");

                fflush(file);
                fclose(file);
            }

            printf("Put file %s \n", fileName);

        } else if (!strncmp(command, "delete", 6)) {
            serverlen = sizeof(serveraddr);
            n = sendto(sockfd, buf, strlen(buf), 0, &serveraddr, serverlen);
            if (n < 0)
                error("ERROR in sendto");

            /* print the server's reply */
            n = recvfrom(sockfd, buf, BUFSIZE, 0, &serveraddr, &serverlen);
            if (n < 0)
                error("ERROR in recvfrom");

            if (!strncmp(buf, "Not found", 9)) {
                printf("The server doesn't have file %s\n", fileName);
                continue;
            } else if (!strncmp(buf, "Error", 6)) {
                printf("Unable to delete file %s\n", fileName);
                continue;
            } else {
                printf("%s %s\n", buf, fileName);
            }

            printf("Deleted file %s \n", fileName);

        } else if (!strncmp(command, "ls", 2)) {
            // packet dataPacket;
            // int sequenceNumber = 0;
            // serverlen = sizeof(serveraddr);
            // n = sendto(sockfd, buf, strlen(buf), 0, &serveraddr, serverlen);
            // if (n < 0)
            //     error("ERROR in sendto");

            // while (1) {
            //     /* print the server's reply */
            //     struct timeval time;
            //     time.tv_sec = 5;
            //     time.tv_usec = 0;
            //     fd_set readfds;
            //     FD_ZERO(&readfds);
            //     FD_SET(sockfd, &readfds);

            //     int returnValue = select(sockfd + 1, &readfds, NULL, NULL, &time);
            //     if (returnValue == 0) {
            //         printf("Timeout occured\n");
            //         break;
            //     } else if (returnValue == -1) {
            //         printf("Error in select\n");
            //     } else {
            //         n = recvfrom(sockfd, &dataPacket, sizeof(dataPacket), 0, &serveraddr, &serverlen);
            //         if (n < 0)
            //             error("ERROR in recvfrom");

            //         if (!strncmp(dataPacket.frame, "LS done", 7)) {
            //             break;
            //         }

            //         if (dataPacket.sequenceNum == sequenceNumber) {
            //             printf("%s", dataPacket.frame);
            //             sequenceNumber++;
            //         }

            //         packet ack;
            //         ack.sequenceNum = sequenceNumber - 1; // ACK the last received packet
            //         ack.type = 2; // Set packet to ACK type
            //         n = sendto(sockfd, &ack, sizeof(ack), 0, &serveraddr, serverlen);
            //         if (n < 0)
            //             error("ERROR in sendto");
            //     }
            // }

            serverlen = sizeof(serveraddr);
            n = sendto(sockfd, buf, strlen(buf), 0, &serveraddr, serverlen);
            if (n < 0)
                error("ERROR in sendto");

            while (1) {
                /* print the server's reply */
                n = recvfrom(sockfd, buf, BUFSIZE, 0, &serveraddr, &serverlen);
                if (n < 0)
                    error("ERROR in recvfrom");

                if (!strncmp(buf, "LS done", 7))
                    break;

                printf("%s", buf);
            }
        } else if (!strncmp(command, "exit", 4)) {
            /* send the message to the server */
            serverlen = sizeof(serveraddr);
            n = sendto(sockfd, buf, strlen(buf), 0, &serveraddr, serverlen);
            if (n < 0)
                error("ERROR in sendto");

            if (!strncmp(buf, "exit", 4)) {
                printf("Goodbye\n");
                fflush(NULL);

                close(sockfd);
                break;
            } else {
                printf("Please try again\n");
                continue;
            }
        } else {
            printf("Invalid command format!!!\n");
        }
    }

    // /* get a message from the user */
    // bzero(buf, BUFSIZE);
    // printf("Please enter msg: ");
    // fgets(buf, BUFSIZE, stdin);

    // /* send the message to the server */
    // serverlen = sizeof(serveraddr);
    // n = sendto(sockfd, commnand, strlen(commnand), 0, &serveraddr, serverlen);
    // if (n < 0)
    //   error("ERROR in sendto");

    return 0;
}
