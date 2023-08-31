/*
Name:       Adilet Majit & Yernar Mukayev
Email:      adilet@nyu.edu
            ym2098@nyu.edu
Program:    Reliable Data Transfer - Sender
*/

#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h> 
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/time.h>
#include <assert.h>

#include "common.h"
#include "packet.h"

#define WINDOW_SIZE 10


tcp_packet *recvpkt;
tcp_packet *sndpkt;
tcp_packet *window[WINDOW_SIZE]; // buffer for storing out-of-bound packets

int nextPacketExpected = 0;
int lastPacketAcked = -1;
int lastPacketReceived = -1;

int cumulativeAck = 0;

int main(int argc, char **argv) {
    int sockfd; /* socket */
    int portno; /* port to listen on */
    int clientlen; /* byte size of client's address */
    struct sockaddr_in serveraddr; /* server's addr */
    struct sockaddr_in clientaddr; /* client addr */
    int optval; /* flag value for setsockopt */
    FILE *fp;
    char buffer[MSS_SIZE];
    struct timeval tp;

    /* 
     * check command line arguments 
     */
    if (argc != 3) {
        fprintf(stderr, "usage: %s <port> FILE_RECVD\n", argv[0]);
        exit(1);
    }
    portno = atoi(argv[1]);

    fp  = fopen(argv[2], "w");
    if (fp == NULL) {
        error(argv[2]);
    }

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
            (const void *)&optval , sizeof(int));

    /*
     * build the server's Internet address
     */
    bzero((char *) &serveraddr, sizeof(serveraddr));
    serveraddr.sin_family = AF_INET;
    serveraddr.sin_addr.s_addr = htonl(INADDR_ANY);
    serveraddr.sin_port = htons((unsigned short)portno);

    /* 
     * bind: associate the parent socket with a port 
     */
    if (bind(sockfd, (struct sockaddr *) &serveraddr, 
                sizeof(serveraddr)) < 0) 
        error("ERROR on binding");

    /* 
     * main loop: wait for a datagram, then echo it
     */
    VLOG(DEBUG, "epoch time, bytes received, sequence number");


    clientlen = sizeof(clientaddr);
    while (1) {
        /*
         * recvfrom: receive a UDP datagram from a client
         */
        if (recvfrom(sockfd, buffer, MSS_SIZE, 0,
                (struct sockaddr *) &clientaddr, (socklen_t *)&clientlen) < 0) {
            error("ERROR in recvfrom");
        }
        recvpkt = (tcp_packet *) buffer;
        assert(get_data_size(recvpkt) <= DATA_SIZE);

        VLOG(INFO, "Received packet %d from server", recvpkt->hdr.seqno);

        // need to send duplicate ACK
        if (recvpkt->hdr.seqno < nextPacketExpected) 
        {
            sndpkt = make_packet(0);

            VLOG(INFO, "Sending dupplicate ACK %d", nextPacketExpected);
            sndpkt->hdr.ackno = nextPacketExpected;
            sndpkt->hdr.ctr_flags = ACK;
            if (sendto(sockfd, sndpkt, TCP_HDR_SIZE, 0, (struct sockaddr *)&clientaddr, clientlen) < 0)
            {
                error("ERROR in sendto");
            }
        }
        // out-of-bound packets
        else if (recvpkt->hdr.seqno > nextPacketExpected) 
        {
            //buffering
            for (int i = 0; i < WINDOW_SIZE; i++)
            {
                if (window[i] == NULL) 
                {
                    window[i] = recvpkt;
                    break;
                }
            }
            // sending duplicate acks
            VLOG(INFO, "Sending duplicate ack %d after out-of-oprder buffering", nextPacketExpected);
            sndpkt = make_packet(0); 
            sndpkt->hdr.ackno = nextPacketExpected;
            sndpkt->hdr.ctr_flags = ACK;
            if (sendto(sockfd, sndpkt, TCP_HDR_SIZE, 0, (struct sockaddr *)&clientaddr, clientlen) < 0)
            {
                error("ERROR in sendto");
            }
            
        }
        // received expected packet
        else
        {
            nextPacketExpected += recvpkt->hdr.data_size; // update

            if (recvpkt->hdr.data_size == 0) // end of file sent
            {
                VLOG(INFO, "End Of File has been reached");
                fclose(fp);
                sndpkt = make_packet(0);
                sndpkt->hdr.ackno = recvpkt->hdr.seqno;
                sndpkt->hdr.ctr_flags = ACK;
                if (sendto(sockfd, sndpkt, TCP_HDR_SIZE, 0, (struct sockaddr *)&clientaddr, clientlen) < 0)
                {
                    error("ERROR in sendto");
                }
                break;
            }

            gettimeofday(&tp, NULL);
            VLOG(DEBUG, "%lu, %d, %d", tp.tv_sec, recvpkt->hdr.data_size, recvpkt->hdr.seqno);

            fseek(fp, recvpkt->hdr.seqno, SEEK_SET);
            fwrite(recvpkt->data, 1, recvpkt->hdr.data_size, fp);

            cumulativeAck = recvpkt->hdr.seqno + recvpkt->hdr.data_size;
            sndpkt = make_packet(0);
            sndpkt->hdr.ctr_flags = ACK;

            // oging over buffered packets to send cumulative ACK
            for (int i = 0; i < WINDOW_SIZE; i++) 
            {
                if (window[i] == NULL) break;

                // buffered packet already ACKed
                if (window[i]->hdr.seqno < nextPacketExpected)
                {
                    window[i] = NULL;
                    int j;
                    for (j = i; j < WINDOW_SIZE - 1; j++)
                    {
                        window[j] = window[j + 1];
                        if (window[j] == NULL && window[j + 1] == NULL)
                            break;
                    }
                    window[WINDOW_SIZE - 1] = NULL;
                    i -= 1;                                
                }
                else if (nextPacketExpected == window[i]->hdr.seqno)
                {
                    nextPacketExpected += window[i]->hdr.data_size; 
                    
                    cumulativeAck = window[i]->hdr.seqno + window[i]->hdr.data_size;

                    if (window[i]->hdr.data_size == 0) // end of file sent
                    {
                        VLOG(INFO, "End Of File has been reached");
                        fclose(fp);
                        break;
                    }
                    gettimeofday(&tp, NULL);
                    VLOG(DEBUG, "> %lu, %d, %d (recovered from buffer)", tp.tv_sec, window[i]->hdr.data_size, window[i]->hdr.seqno);
                    fseek(fp, window[i]->hdr.seqno, SEEK_SET);
                    fwrite(window[i]->data, 1, window[i]->hdr.data_size, fp);


                    free(window[i]);
                    for (int j = i; j < WINDOW_SIZE - 1; j++) 
                    {
                        window[j] = window[j + 1];
                        if (window[j] == NULL && window[j + 1] == NULL)
                            break;
                    }
                                                  
                    window[WINDOW_SIZE - 1] = NULL; 
                    i -= 1;
                }
                
            }
            sndpkt->hdr.ackno = cumulativeAck;

            if(sendto(sockfd, sndpkt, TCP_HDR_SIZE, 0,
                     (struct sockaddr *)&clientaddr, clientlen) < 0)
            {
                error("ERROR in sendto");
            }
            VLOG(INFO, "Sending cumulative ACK %d", cumulativeAck);
        }
    }

    VLOG(INFO, "\nTerminating protocol...");

    //freeing buffer
    for (int i = 0; i <WINDOW_SIZE; i++) {
        free(window[i]);
    }

    return 0;
}
