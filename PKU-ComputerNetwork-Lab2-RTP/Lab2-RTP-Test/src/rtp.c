#include "rtp.h"
#include "util.h"
#include <string.h>

// 发送消息 msg是payload len是payload长度
void rtp_sendto(int sockfd, const void *msg, int len, int seq, int flags, int type, struct sockaddr *to, socklen_t tolen)
{
    rtp_packet_t packet;
    packet.rtp.length = len;
    packet.rtp.checksum = 0;
    packet.rtp.seq_num = (uint32_t)seq;
    packet.rtp.type = type;

    if(len)
        memcpy(packet.payload, msg, len);

    packet.rtp.checksum = compute_checksum(&packet, sizeof(rtp_header_t)+len);//注意校验和

    sendto(sockfd, &packet, sizeof(rtp_header_t)+len, flags, to, tolen);
}

// 接受消息 buf用来接收payload 返回接收的payload长度 有检查checksum的功能 返回-1表示checksum不对
int rtp_recvfrom(int sockfd, void *buf, uint32_t* seq, int flags, uint8_t* type, struct sockaddr *from, socklen_t *fromlen)
{
    rtp_packet_t packet;
    recvfrom(sockfd, &packet, sizeof(rtp_packet_t), flags, from, fromlen);

    uint32_t pkt_checksum = packet.rtp.checksum;

    packet.rtp.checksum = 0;
    uint32_t computed_checksum = compute_checksum(&packet, sizeof(rtp_header_t)+packet.rtp.length);//注意校验和

    if (pkt_checksum != computed_checksum)
        return -1;

    *seq = (int)packet.rtp.seq_num;
    *type = (int)packet.rtp.type;

    if(packet.rtp.length)
        memcpy(buf, (void*)(&packet) + sizeof(rtp_header_t), packet.rtp.length);

    return packet.rtp.length;
}
