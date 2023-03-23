#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <sys/time.h>
#include <stdint.h>

#define NTP_SERVER "time.stdtime.gov.tw"
#define NTP_SERVER "118.163.81.61"
#define NTP_PORT 123
#define OFFSET 2208988800U

#define frac_sub(a_frac, b_frac, c, res_frac) {if(b_frac > a_frac) c = -1; else c = 0; res_frac = a_frac - b_frac;}
#define frac2double(frac) (frac / (double)UINT32_MAX)
#define frac2us(frac) ((uint32_t)(frac2double(frac) * 1000000))

#define tv_add(tv1, tv2, res) {(res).tv_sec = tv1.tv_sec + tv2.tv_sec; (res).tv_usec = tv1.tv_usec + tv2.tv_usec; if((res).tv_usec >= 1000000) {(res).tv_sec += 1; (res).tv_usec -= 1000000;}}
#define tv_sub(tv1, tv2, res) {if(tv2.tv_usec > tv1.tv_usec) {tv1.tv_usec += 1000000; tv1.tv_sec -= 1;} (res).tv_sec = tv1.tv_sec - tv2.tv_sec; (res).tv_usec = tv1.tv_usec - tv2.tv_usec;}
#define tv_over2(tv) {if((tv).tv_sec & 1) {(tv).tv_sec -= 1; (tv).tv_usec += 1000000;} (tv).tv_sec >>= 1; (tv).tv_usec >>= 1;}
#define def_set(type, name, src) type name; memcpy(&name, &src, sizeof(src));


struct ntp_packet
{
    uint8_t li_vn_mode;      // leap indicator(2 bits), version(3 bits), mode(3 bits)
    uint8_t stratum;         // stratum level of the local clock
    uint8_t poll;            // maximum polling interval
    uint8_t precision;       // clock precision
    uint32_t root_delay;     // root delay between local clock and server
    uint32_t root_dispersion;// root dispersion between local clock and server
    char ref_id[4];          // reference clock identifier
    uint32_t ref_ts_sec;     // reference timestamp (seconds)
    uint32_t ref_ts_frac;    // reference timestamp (fractions of a second)
    uint32_t orig_ts_sec;    // originate timestamp (seconds)
    uint32_t orig_ts_frac;   // originate timestamp (fractions of a second)
    uint32_t recv_ts_sec;    // receive timestamp (seconds)
    uint32_t recv_ts_frac;   // receive timestamp (fractions of a second)
    uint32_t tx_ts_sec;      // transmit timestamp (seconds)
    uint32_t tx_ts_frac;     // transmit timestamp (fractions of a second)
};
typedef struct timeval timeval_t;

int main()
{
    timeval_t offset;
    int code = sync(&offset);
    printf("offset: %lds + %ld us\n", offset.tv_sec, offset.tv_usec);
    return 0;

    timeval_t now;
    gettimeofday(&now, NULL);
    tv_over2(offset);
    tv_add(offset, now, now);
    code = settimeofday(&now, NULL);
    printf("%d\n", code);


    return 0;
}
int sync(timeval_t* offset)
{
    int sockfd = 0, n;
    sockfd = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if(sockfd < 0)
    {
        perror("socket creation failed");
        return n;
    }
    timeval_t timeout = {0.5, 0};
    setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, (const char*)&timeout, sizeof(timeout));

    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = inet_addr(NTP_SERVER);
    server_addr.sin_port = htons(NTP_PORT);


    struct ntp_packet packet;
    memset(&packet, 0, sizeof(packet));
    packet.li_vn_mode = 0b11 << 6 | 0b100 << 3 | 0b011;

    // get timestamp before request
    timeval_t t0;
    gettimeofday(&t0, NULL);
    packet.orig_ts_sec = htonl(t0.tv_sec + OFFSET);

    n = sendto(sockfd, &packet, sizeof(packet), 0, (struct sockaddr *)&server_addr, sizeof(server_addr));
    if(n < 0)
    {
        perror("sendto failed");
        return n;
    }


    n = recv(sockfd, &packet, sizeof(packet), 0);

    // get timestamp when receive response
    timeval_t t3;
    gettimeofday(&t3, NULL);

    if(n < 0)
    {
        perror("recv failed");
        return n;
    }

    close(sockfd);

    uint32_t ts1_sec = ntohl(packet.recv_ts_sec) - OFFSET;
    uint32_t ts1_frac = ntohl(packet.recv_ts_frac);
    timeval_t ts1 = {ts1_sec, frac2us(ts1_frac)};

    uint32_t ts2_sec = ntohl(packet.tx_ts_sec) - OFFSET;
    uint32_t ts2_frac = ntohl(packet.tx_ts_frac);
    timeval_t ts2 = {ts2_sec, frac2us(ts2_frac)};

    // calc delta
    timeval_t delta;
    delta.tv_sec = (t3.tv_sec - t0.tv_sec) - (ts2_sec - ts1_sec);
    int carry;
    uint32_t ts2_to_ts3_frac;
    frac_sub(ts2_frac, ts1_frac, carry, ts2_to_ts3_frac);
    delta.tv_sec += carry;
    delta.tv_usec = (t3.tv_usec - t0.tv_usec) - frac2us(ts2_to_ts3_frac);

    printf("delta: %lu s + %lu us\n", delta.tv_sec, delta.tv_usec);

    // calc offset
    def_set(timeval_t, half_delta, delta);
    tv_over2(half_delta);

    // correct by delta
    def_set(timeval_t, t1, ts1);
    def_set(timeval_t, t2, ts2);

    tv_sub(ts1, half_delta, t1);
    tv_add(ts2, half_delta, t2);

    timeval_t t0_2_t1;
    tv_sub(t1, t0, t0_2_t1);
    timeval_t t2_2_t3;
    tv_sub(t2, t3, t2_2_t3);

    tv_add(t0_2_t1, t2_2_t3, *offset);
    tv_over2(*offset);

    return 0;
}