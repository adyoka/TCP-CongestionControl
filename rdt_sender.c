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
#include <signal.h>
#include <sys/time.h>
#include <time.h>
#include <assert.h>
#include <math.h>
#include "packet.h"
#include "common.h"

#define STDIN_FD 0
#define RETRY 50 // millisecond
#define WINDOW_SIZE 64

float cwnd = 1;
int ssthresh = 64;

float RTT = 0;
float RTT_dev = 0;
float alpha = 0.125;
float beta = 0.25;
float RTO = RETRY;   // ms
float maxRTO = 100; // ms

int next_seqno = 0;
int send_base = 0;

int sockfd, serverlen;
struct sockaddr_in serveraddr;
struct itimerval timer;
tcp_packet *sndpkt;
tcp_packet *recvpkt;
sigset_t sigmask;

tcp_packet *sendBuffer[WINDOW_SIZE];
struct timeval timeSent[WINDOW_SIZE];

int lastPacketAvailable = 0;
int lastPacketAcked = 0;
int lastPacketSent = 0;

int lastAckedSeq = 0;

int dupACKcount = 0;
int packetsPending = 0;
int lastDuplicatedSeq = -1;

int flag = 0;

FILE *csv;
struct timeval time_init; // for intial time of prgram.
struct timeval t1;

int max(int a, int b)
{
    return (a > b) ? a : b;
}
int min(int a, int b)
{
    return (a < b) ? a : b;
}

float timedifference_msec(struct timeval t0, struct timeval t1)
{
    return fabs((t1.tv_sec - t0.tv_sec) * 1000.0f + (t1.tv_usec - t0.tv_usec) / 1000.0f);
}

void start_timer()
{
    sigprocmask(SIG_UNBLOCK, &sigmask, NULL);
    setitimer(ITIMER_REAL, &timer, NULL);
}

void resend_packets(int sig)
{
    // resending the unacked packet with the smallest sequence number
    if (sig == SIGALRM && sndpkt != NULL)
    {
        VLOG(INFO, "Timeout: resending packet %d", sndpkt->hdr.seqno);
        if (sendto(sockfd, sndpkt, TCP_HDR_SIZE + get_data_size(sndpkt), 0,
                   (const struct sockaddr *)&serveraddr, serverlen) < 0)
        {
            error("sendto");
        }
        // restarting timer again after resedning
        start_timer();
        flag = 1;
        // changing cwnd
        ssthresh = max(cwnd / 2, 2);
        cwnd = 1;
        dupACKcount = 0;
        // lastDuplicatedSeq = sndpkt->hdr.seqno;

        gettimeofday(&t1, 0);
        fprintf(csv, "%f,%f,%d\n", timedifference_msec(time_init, t1), cwnd, ssthresh);
        return;
    }
    VLOG(DEBUG, "Timeout. Sndpkt not found");
}

void stop_timer()
{
    sigprocmask(SIG_BLOCK, &sigmask, NULL);
}

/*
 * init_timer: Initialize timer
 * delay: delay in milliseconds
 * sig_handler: signal handler function for re-sending unACKed packets
 */
void init_timer(int delay, void (*sig_handler)(int))
{
    signal(SIGALRM, sig_handler);
    timer.it_interval.tv_sec = delay / 1000; // sets an interval of the timer
    timer.it_interval.tv_usec = (delay % 1000) * 1000;
    timer.it_value.tv_sec = delay / 1000; // sets an initial value
    timer.it_value.tv_usec = (delay % 1000) * 1000;

    sigemptyset(&sigmask);
    sigaddset(&sigmask, SIGALRM);
}

int main(int argc, char *argv[])
{
    int portno, len;
    // int next_seqno;
    char *hostname;
    char buffer[DATA_SIZE];
    FILE *fp;

    /* check command line arguments */
    if (argc != 4)
    {
        fprintf(stderr, "usage: %s <hostname> <port> <FILE>\n", argv[0]);
        exit(0);
    }
    hostname = argv[1];
    portno = atoi(argv[2]);
    fp = fopen(argv[3], "r");
    if (fp == NULL)
    {
        error(argv[3]);
    }

    /* socket: create the socket */
    sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    if (sockfd < 0)
        error("ERROR opening socket");

    /* initialize server server details */
    bzero((char *)&serveraddr, sizeof(serveraddr));
    serverlen = sizeof(serveraddr);

    /* covert host into network byte order */
    if (inet_aton(hostname, &serveraddr.sin_addr) == 0)
    {
        fprintf(stderr, "ERROR, invalid host %s\n", hostname);
        exit(0);
    }

    /* build the server's Internet address */
    serveraddr.sin_family = AF_INET;
    serveraddr.sin_port = htons(portno);

    assert(MSS_SIZE - TCP_HDR_SIZE > 0);

    // Stop and wait protocol

    init_timer((int)RTO, resend_packets);
    for (int i = 0; i < WINDOW_SIZE; i++)
    {
        sendBuffer[i] = NULL;
    }

    csv = fopen("cwnd.csv", "w");
    if (csv == NULL)
    {
        printf("Error opening csv\n");
        return 1;
    }
    // start timer
    gettimeofday(&time_init, 0);

    gettimeofday(&t1, 0);
    fprintf(csv, "%f,%f,%d\n", timedifference_msec(time_init, t1), cwnd, ssthresh);

    while (1)
    {
        while (packetsPending < cwnd)
        {
            // find empty slot in a send buffer
            int i = 0;
            for (i = 0; i < WINDOW_SIZE; i++)
            {
                if (sendBuffer[i] == NULL)
                    break;
            }

            bzero(buffer, DATA_SIZE);
            len = fread(buffer, 1, DATA_SIZE, fp);
            if (len <= 0)
            {
                // end of file - the whole file was sent
                // final empty message
                VLOG(INFO, "End Of File has been reached");
                sendBuffer[i] = make_packet(0);
                sendBuffer[i]->hdr.seqno = next_seqno;
                // send_base = next_seqno;
                lastPacketAvailable = next_seqno; // last seqno of file
                // break;
            }
            else
            {
                sendBuffer[i] = make_packet(len);
                memcpy(sendBuffer[i]->data, buffer, len);
                sendBuffer[i]->hdr.seqno = next_seqno;
                lastPacketSent = next_seqno;
                // send_base = next_seqno;
                next_seqno += len;
            }

            VLOG(INFO, "Sending packet %d to %s",
                 sendBuffer[i]->hdr.seqno, inet_ntoa(serveraddr.sin_addr));
            if (sendto(sockfd, sendBuffer[i], TCP_HDR_SIZE + get_data_size(sendBuffer[i]), 0,
                       (const struct sockaddr *)&serveraddr, serverlen) < 0)
            {
                error("sendto");
            }
            gettimeofday(&timeSent[i], 0);

            if (flag == 0)
            {
                start_timer(); // restarting time after first packet sent
                sndpkt = sendBuffer[i];
                send_base = sendBuffer[i]->hdr.seqno;
                flag = 1;
            }
            packetsPending++;
        }

        // receiving ACK
 
            bzero(buffer, DATA_SIZE);
            if (recvfrom(sockfd, buffer, MSS_SIZE, 0,
                         (struct sockaddr *)&serveraddr, (socklen_t *)&serverlen) < 0)
            {
                error("recvfrom");
            }

            recvpkt = (tcp_packet *)buffer;
            assert(get_data_size(recvpkt) <= DATA_SIZE);

            VLOG(INFO, "ACK %d received from server", recvpkt->hdr.ackno);
            
            if (lastPacketAvailable == recvpkt->hdr.ackno) // if we received ACK for last packet we need to send it
            {
                sndpkt = make_packet(0);
                sndpkt->hdr.seqno = next_seqno;

                if (sendto(sockfd, sndpkt, TCP_HDR_SIZE + get_data_size(sndpkt), 0,
                       (const struct sockaddr *)&serveraddr, serverlen) < 0)
                    {
                        error("sendto");
                    }
                bzero(buffer, DATA_SIZE); // receiving ACK for empty packet
                if (recvfrom(sockfd, buffer, MSS_SIZE, 0,
                            (struct sockaddr *)&serveraddr, (socklen_t *)&serverlen) < 0)
                {
                    error("recvfrom");
                }

                recvpkt = (tcp_packet *)buffer;
                VLOG(INFO, "Last packet ACK recevied %d = %d\n", lastPacketAvailable ,recvpkt->hdr.ackno);
                break;
                
                    
            }

            if (recvpkt->hdr.ackno <= lastAckedSeq)
            {
                if (++dupACKcount >= 3)
                {
                    stop_timer();
                    resend_packets(SIGALRM);
                }
            }


        if (recvpkt->hdr.ackno > lastAckedSeq)
        {
            // stop timer to process sliding window
            stop_timer();
            flag = 0;

            lastAckedSeq = recvpkt->hdr.ackno; // update
            dupACKcount = 0;

            for (int idx = 0; idx < WINDOW_SIZE; idx++)
            {
                if (sendBuffer[idx] != NULL && sendBuffer[idx]->hdr.seqno < recvpkt->hdr.ackno) // remove all packets that been cumulatively ACKed
                {

                    // if (sendBuffer[idx]->hdr.seqno + recvpkt->hdr.data_size == recvpkt->hdr.ackno)
                    // {
                        // update RTT
                        gettimeofday(&t1, 0);
                        float rtt_sample = timedifference_msec(t1, timeSent[idx]);
                        RTT_dev = (1 - beta) * RTT_dev + beta * fabs(rtt_sample - RTT);
                        RTT = (1 - alpha) * RTT + alpha * rtt_sample;
                        RTO = RTT + 4 * RTT_dev;
                        RTO = min(maxRTO, RTO); // we limit RTO to a small RTO just to quickly send file
                        printf("---- RTO: %f -----\n", RTO);
                        printf("---- Sample RTT: %f -----\n", rtt_sample);
                        init_timer(RTO, resend_packets); // init time with new RTO

                    // }

                    free(sendBuffer[idx]);
                    sendBuffer[idx] = NULL;

                    gettimeofday(&timeSent[idx], 0);

                    packetsPending--;
                    // slow start
                    if ((int)cwnd < ssthresh)
                    {
                        cwnd++;
                    }
                    // congestion avoidance
                    else
                    {
                        cwnd += (1.0f / (int)(cwnd));
                    }
                    gettimeofday(&t1, 0);
                    fprintf(csv, "%f,%f,%d\n", timedifference_msec(time_init, t1), cwnd, ssthresh);

                    cwnd = cwnd > WINDOW_SIZE ? WINDOW_SIZE : cwnd; // we cannot send and buffer more than a sendBuffer window
                }
                // else if (sendBuffer[idx] != NULL && sendBuffer[idx]->hdr.seqno == recvpkt->hdr.ackno)
                // {
                //     printf("here\n");
                //     sndpkt = sendBuffer[idx];
                //     send_base = sendBuffer[idx]->hdr.seqno;
                //     start_timer();
                //     flag = 1;

                //     // VLOG(INFO, "Sending packet %d to %s",
                //     //      sendBuffer[i]->hdr.seqno, inet_ntoa(serveraddr.sin_addr));
                //     // if (sendto(sockfd, sendBuffer[i], TCP_HDR_SIZE + get_data_size(sendBuffer[i]), 0,
                //     //            (const struct sockaddr *)&serveraddr, serverlen) < 0)
                //     // {
                //     //     error("sendto");
                //     // }

                // }
            }


            if (lastPacketSent >= lastAckedSeq)
            {

                for (int i = 0; i < WINDOW_SIZE; i++)
                {
                    if (sendBuffer[i] != NULL && sendBuffer[i]->hdr.seqno == recvpkt->hdr.ackno) //update
                    {
                        sndpkt = sendBuffer[i];
                        send_base = sendBuffer[i]->hdr.seqno;
                        start_timer();
                        flag = 1;
                        break;
                    }
                }
            }
        }
      
    }

    VLOG(INFO, "File has been sent fully."); // done

    // freeing packets
    for (int i = 0; i < WINDOW_SIZE; ++i)
    {
        free(sendBuffer[i]);
    }
    fclose(csv);
    fclose(fp);
    return 0;
}