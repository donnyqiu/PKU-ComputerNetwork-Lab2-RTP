#include "receiver_def.h"
#include "util.h"
#include "rtp.h"
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <string.h>
#include <sys/select.h>
#include <iostream>
#include <sys/time.h>
#include <fstream>

using namespace std;

static int sockfd;
static uint32_t seq_start;//期望收到的下一个数据包seq 即窗口的左端
static uint32_t window;//窗口大小
static bool* ack;
static char** buf;
static int* buflen;
static struct sockaddr_in raddr;
static struct sockaddr_in saddr;
static socklen_t len;


int initReceiver(uint16_t port, uint32_t window_size){
    window = window_size;
    seq_start = 0;
    sockfd = socket(AF_INET, SOCK_DGRAM, 0);

    bzero(&raddr, sizeof(raddr));
    bzero(&saddr, sizeof(saddr));
    raddr.sin_family = AF_INET;
    raddr.sin_port = htons(port);
    raddr.sin_addr.s_addr = htonl(INADDR_ANY);
    bind(sockfd, (struct sockaddr*)&raddr, sizeof(raddr));

    len = sizeof(saddr);
    
    char temp[PAYLOAD_SIZE];
    uint32_t seq;
    uint8_t type;

    rtp_recvfrom(sockfd, temp, &seq, 0, &type, (struct sockaddr*)&saddr, &len);

    if(type == RTP_START){
        rtp_sendto(sockfd, temp, 0, seq, 0, RTP_ACK, (struct sockaddr*)&saddr, len);
        buf = new char*[window];
        for(int i=0;i<window;i++){
            buf[i] = new char[PAYLOAD_SIZE];
        }
        buflen=new int[window];
        ack=new bool[window];
    }
    else{
        close(sockfd);
        return -1;
    }
    return 0;
}



void receiver_window(FILE *f){
    int num = 0;//为需要移动的格数

    //0~num-1需要写入文件
    for(int i = 0; i < window; i++){
        if(ack[i] == false)
            break;
        num++;
        fwrite(buf[i], 1, buflen[i], f);
        delete[] buf[i];
    }

    for(int i = 0; (i + num) < window; i++){
        buf[i] = buf[i+num];
        buflen[i] = buflen[i+num];
        ack[i] = ack[i+num];
    }

    for(int i = 0; i < num; i++){
        int j = window - 1- i;
        buf[j] = NULL;
        buflen[j] = 0;
        ack[j] = false;
    }

    seq_start += num;
}



int recvMessage(char* filename){
    FILE* f = fopen(filename, "wb");
    char temp[PAYLOAD_SIZE];
    uint32_t seq;
    uint8_t type;

    while(1) {
        int resp = rtp_recvfrom(sockfd, temp, &seq, 0, &type, NULL, NULL);
        
        if(type == RTP_END){
            if(resp >= 0){
                fclose(f);
                rtp_sendto(sockfd, temp, 0, seq, 0, RTP_ACK, (struct sockaddr*)&saddr, len);
                return 0;
            }
        }
        else if(type == RTP_DATA){
            if(resp >= 0){//checksum没有出错
                if(seq == seq_start){//收到期望的数据包
                    ack[0] = true;
                    buflen[0] = resp;
                    if(buf[0] == NULL){
                        buf[0] = new char[PAYLOAD_SIZE];
                    }
                    memcpy(buf[0], temp, resp);
                    receiver_window(f);
                    rtp_sendto(sockfd, temp, 0, seq_start, 0, RTP_ACK, (struct sockaddr*)&saddr, len);
                }
                else {//收到窗口内的其他数据包
                    if(seq > seq_start && seq < seq_start+window){
                        int i=seq-seq_start;
                        if(ack[i]==false){
                            ack[i]=true;
                            buflen[i]=resp;
                            if(buf[i] == NULL){
                                buf[i] = new char[PAYLOAD_SIZE];
                            }
                            memcpy(buf[i], temp, resp);
                        }
                        rtp_sendto(sockfd, temp, 0, seq_start, 0, RTP_ACK, (struct sockaddr*)&saddr, len);
                    }
                    if(seq < seq_start && seq >= seq_start-window){
                        rtp_sendto(sockfd, temp, 0, seq_start, 0, RTP_ACK, (struct sockaddr*)&saddr, len);
                    }
                }
                
            }
        }
    }
    return 0;
}



void terminateReceiver(){
    close(sockfd);
    delete[] ack;
    delete[] buf;
    delete[] buflen;
}



int recvMessageOpt(char* filename){
    FILE* f = fopen(filename, "wb");
    char temp[PAYLOAD_SIZE];
    uint32_t seq;
    uint8_t type;

    while(1) {
        int resp = rtp_recvfrom(sockfd, temp, &seq, 0, &type, NULL, NULL);
        if(type == RTP_END){
            if(resp >= 0){
                fclose(f);
                rtp_sendto(sockfd, temp, 0, seq, 0, RTP_ACK, (struct sockaddr*)&saddr, len);
                return 0;
            }
        }
        else if(type == RTP_DATA){
            if(resp >= 0){//checksum没有出错
                if(seq == seq_start){//收到期望的数据包
                    ack[0] = true;
                    buflen[0] = resp;
                    if(buf[0] == NULL){
                        buf[0] = new char[PAYLOAD_SIZE];
                    }
                    memcpy(buf[0], temp, resp);
                    receiver_window(f);
                    rtp_sendto(sockfd, temp, 0, seq, 0, RTP_ACK, (struct sockaddr*)&saddr, len);
                }
                else {//收到窗口内的其他数据包
                    if(seq > seq_start && seq < seq_start+window){
                        int i=seq-seq_start;
                        if(ack[i]==false){
                            ack[i]=true;
                            buflen[i]=resp;
                            if(buf[i] == NULL){
                                buf[i] = new char[PAYLOAD_SIZE];
                            }
                            memcpy(buf[i], temp, resp);
                        }
                        rtp_sendto(sockfd, temp, 0, seq, 0, RTP_ACK, (struct sockaddr*)&saddr, len);
                    }
                    if(seq < seq_start){
                        rtp_sendto(sockfd, temp, 0, seq, 0, RTP_ACK, (struct sockaddr*)&saddr, len);
                    }
                }
                
            }
        }
    }
    return 0;
}