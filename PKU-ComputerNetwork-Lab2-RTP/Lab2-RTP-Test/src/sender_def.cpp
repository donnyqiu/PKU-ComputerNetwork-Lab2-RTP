#include "sender_def.h"
#include "util.h"
#include "rtp.h"
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <string.h>
#include <iostream>
#include <sys/time.h>
#include <sys/epoll.h>

using namespace std;

static uint32_t seq_num;//下一个发送的seq 窗口右边+1
static uint32_t seq_start;//等待ack的最小的seq 窗口的左端 
static int sockfd;
static uint32_t window;
static char** buf;
static int* buflen;
static bool* bufack;
static struct sockaddr_in addr;

int initSender(const char* receiver_ip, uint16_t receiver_port, uint32_t window_size){
    window = window_size;
    seq_start = 0;
    seq_num = 0;

    sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    bzero(&addr, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(receiver_port);
    inet_pton(AF_INET, receiver_ip, &addr.sin_addr);

    srand(time(NULL));
    uint32_t ran = rand();


    char temp[PAYLOAD_SIZE];
    uint32_t seq;
    uint8_t type;


    rtp_sendto(sockfd, temp, 0, ran, 0, RTP_START, (struct sockaddr *)&addr, sizeof(struct sockaddr));
    
    
    //接收START ACK
    if(rtp_recvfrom(sockfd, temp, &seq, 0, &type, NULL, NULL)>=0){
        buf = new char*[window];
        buflen = new int[window];
        
        for(int i=0;i<window;i++)
            buf[i]=new char[PAYLOAD_SIZE];
        return 0;
    }


    //错误
    rtp_sendto(sockfd, buf, 0, ++ran, 0, RTP_END, (struct sockaddr *)&addr, sizeof(struct sockaddr));
    rtp_recvfrom(sockfd, temp, &seq, 0, &type, NULL, NULL);
    close(sockfd);
    return -1;
}



//window-num~window-1 buf填充文件中读取的内容 返回填充的数据包个数
int file2buf(FILE *f, int num){
    int start = window - num;
    for(int i=0; i<num; i++) {
        char temp[PAYLOAD_SIZE];
        int count = fread(temp, 1, PAYLOAD_SIZE, f);
        if(count == 0)
            return i;

        if(buf[start+i] == NULL)
            buf[start+i]=new char[PAYLOAD_SIZE];

        memcpy(buf[start+i], temp, count);
        buflen[start+i]=count;
    }
    return num;
}



//将窗口前移num
void senderWindow(int num){
    if(num == 0)
        return;
    
    for(int i=0;i<num;i++)
        delete[] buf[i];

    for(int i=0; (i+num)<window; i++){
        buf[i] = buf[i+num];
        buflen[i] = buflen[i+num];
    }

    for(int i=0; i<num; i++){
        int j = window - 1 - i;
        buf[j] = NULL;
        buflen[j] = 0;
    }

    seq_start += num;
}



void resend(){
    for(int i=0;i<window;i++){
        if(buf[i] == NULL)
            break;
        rtp_sendto(sockfd, buf[i], buflen[i], seq_start+i, 0, RTP_DATA, (struct sockaddr *)&addr, sizeof(struct sockaddr));
        usleep(10);
    }
}



int sendMessage(const char* message){
    FILE* f = fopen(message, "rb");

    //全部填充然后发送
    int num = file2buf(f, window);
    for(int i=0;i<num;i++){
        rtp_sendto(sockfd, buf[i], buflen[i], seq_num, 0, RTP_DATA, (struct sockaddr *)&addr, sizeof(struct sockaddr));//先发再加
        seq_num++;
        usleep(10);
    }

    int epfd = epoll_create(128);
    struct epoll_event evt;
    evt.events = EPOLLIN;
    evt.data.fd=sockfd;
    epoll_ctl(epfd, EPOLL_CTL_ADD, sockfd, &evt);
    struct epoll_event events[8];

    //接受和发送
    while(num){
        char temp[PAYLOAD_SIZE];
        uint32_t seq;
        uint8_t type;

        int nevents = epoll_wait(epfd, events, 8, 100);//超时剩余时间？
        if(nevents == 0){
            resend();
        }
        else{
            if(rtp_recvfrom(sockfd, temp, &seq, 0, &type, NULL, NULL)>=0){//没有损坏 seq为需要的下一个
                if(seq > seq_start && seq <= seq_start+window){//可以移动窗口
                    int n = seq-seq_start;
                    senderWindow(n);
                    
                    int cnt = file2buf(f,n);//实际装入
                    if(cnt == 0){
                        break;
                    }

                    for(int i=0;i<cnt;i++){
                        int j = window-n+i;
                        rtp_sendto(sockfd, buf[j], buflen[j], seq_num, 0, RTP_DATA, (struct sockaddr *)&addr, sizeof(struct sockaddr));
                        seq_num++;
                        usleep(10);
                    }

                }
            }
        }
    }



    //处理末尾几个
    while(num){
        char temp[PAYLOAD_SIZE];
        uint32_t seq;
        uint8_t type;

        int nevents = epoll_wait(epfd, events, 8, 100);
        if(nevents == 0){
            resend();
        }
        else{
            if(rtp_recvfrom(sockfd, temp, &seq, 0, &type, NULL, NULL)>=0){
                if(seq > seq_start && seq <= seq_num){//可以移动窗口
                    int n = seq-seq_start;
                    senderWindow(n);

                    if(seq == seq_num)
                        break;

                }
            }
        }
    }


    fclose(f);    
    return 0;
}



void terminateSender() {
    struct timeval tv;
    tv = {0, 100000};
    fd_set rdfds;
    FD_ZERO(&rdfds);
    FD_SET(sockfd, &rdfds);

    char temp[PAYLOAD_SIZE];
    uint32_t seq;
    uint8_t type;

    rtp_sendto(sockfd, temp, 0, seq_num, 0, RTP_END, (struct sockaddr *)&addr, sizeof(struct sockaddr));
    if(select(sockfd+1, &rdfds, NULL, NULL, &tv)){
        rtp_recvfrom(sockfd, temp, &seq, 0, &type, NULL, NULL);
    }

    delete[] buf;
    delete[] buflen;

    close(sockfd);
}



int file2bufOpt(FILE *f, int num){
    int start = window - num;
    for(int i=0; i<num; i++) {
        char temp[PAYLOAD_SIZE];
        int count = fread(temp, 1, PAYLOAD_SIZE, f);
        if(count == 0)
            return i;

        if(buf[start+i] == NULL)
            buf[start+i]=new char[PAYLOAD_SIZE];

        memcpy(buf[start+i], temp, count);
        buflen[start+i]=count;
        bufack[start+i]=false;
    }
    return num;
}



int senderWindowOpt(){
    int num=0;

    for(int i=0;i<window;i++){
        if(buf[i] && bufack[i]){
            delete[] buf[i];
            num++;
        }
        else
            break;
    }

    for(int i=0; (i+num)<window; i++){
        buf[i] = buf[i+num];
        buflen[i] = buflen[i+num];
        bufack[i] = bufack[i+num];
    }

    for(int i=0; i<num; i++){
        int j = window - 1 - i;
        buf[j] = NULL;
        buflen[j] = 0;
        bufack[j] = false;
    }

    seq_start += num;
    return num;
}



void resendOpt(){
    for(int i=0;i<window;i++){
        if(buf[i] == NULL)
            break;
        // cout << "timeout " << seq_start+i << endl;
        if(!bufack[i]){
            rtp_sendto(sockfd, buf[i], buflen[i], seq_start+i, 0, RTP_DATA, (struct sockaddr *)&addr, sizeof(struct sockaddr));
            usleep(10);
        }
    }
}



int sendMessageOpt(const char* message){
    FILE* f = fopen(message, "rb");

    bufack = new bool[window];
    for(int i=0;i<window;i++){
        bufack[i]=false;
    }

    int num = file2bufOpt(f, window);
    for(int i=0;i<num;i++){
        rtp_sendto(sockfd, buf[i], buflen[i], seq_num, 0, RTP_DATA, (struct sockaddr *)&addr, sizeof(struct sockaddr));//先发再加
        seq_num++;
        usleep(10);
    }

    int epfd = epoll_create(128);
    struct epoll_event evt;
    evt.events = EPOLLIN;
    evt.data.fd=sockfd;
    epoll_ctl(epfd, EPOLL_CTL_ADD, sockfd, &evt);
    struct epoll_event events[8];

    while(num){
        char temp[PAYLOAD_SIZE];
        uint32_t seq;
        uint8_t type;

        int nevents = epoll_wait(epfd, events, 8, 100);//超时剩余时间？
        if(nevents == 0){
            resend();
        }
        else{
            if(rtp_recvfrom(sockfd, temp, &seq, 0, &type, NULL, NULL)>=0){
                if(seq == seq_start){//可以移动窗口
                    bufack[0] = true;

                    int n = senderWindowOpt();
                    int cnt = file2bufOpt(f,n);//实际装入
                    if(cnt == 0){
                        break;
                    }

                    for(int i=0;i<cnt;i++){
                        int j = window-n+i;
                        rtp_sendto(sockfd, buf[j], buflen[j], seq_num, 0, RTP_DATA, (struct sockaddr *)&addr, sizeof(struct sockaddr));
                        seq_num++;
                        usleep(10);
                    }

                }
                else if(seq > seq_start && seq <= seq_num){
                    bufack[seq-seq_start] = true;
                }
            }
        }
    }


    while(num){
        char temp[PAYLOAD_SIZE];
        uint32_t seq;
        uint8_t type;

        int nevents = epoll_wait(epfd, events, 8, 100);//超时剩余时间？
        if(nevents == 0){
            resend();
        }
        else{
            if(rtp_recvfrom(sockfd, temp, &seq, 0, &type, NULL, NULL)>=0){
                if(seq == seq_start){//可以移动窗口
                    bufack[0] = true;

                    senderWindowOpt();
                    if(seq_start == seq_num){
                        break;
                    }

                }
                else if(seq > seq_start && seq <= seq_num){
                    bufack[seq-seq_start] = true;
                }
            }
        }
    }

    fclose(f);
    delete[] bufack;
    return 0;
}
