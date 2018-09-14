#include "netmon.h"
#include "errors.h"
#include "ui.h"
#include "packet.h"

#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <linux/if_packet.h>
#include <net/ethernet.h> 
#include <time.h> 

// Needed to check for device index
#include <sys/ioctl.h>
#include <net/if.h>


#define MACLENGTH 17 // The length of a mac address (with colons)
#define IP4LENGTH 15 // The length of an IPv4 address (with periods)
#define IP6LENGTH 39 // The length of an IPv4 address (with periods)

// The base amount for dynamic arrays
#define CHUNK 8

typedef struct __attribute__((packed)) {
   int arp_count;     // ARP packet count
   int ip4_count;     // IPv4 packet count
   int ip6_count;     // IPv6 packet count
   int reply_count;   // ARP reply packet count
   int request_count; // ARP request packet count
   int igmp_count;    // IGMP packet count
   int icmp_count;    // ICMP packet count
   int tcp_count;     // TCP packet count
   int udp_count;     // UDP packet count
   time_t start_time; // The start time of the program
   int total_bytes;   // The total number of bytes seen
   char **ip_addrs;   // The list of all IP addresses seen
   int ip_len;
   int ip_capacity;
   char **mac_addrs;  // The list of all MAC addresses seen
   int mac_len;
   int mac_capacity;
} NETMON;

static NETMON netmon;

static void process_packet(char *packet_bytes, int len);
static void process_ip4_packet(char *packet_bytes, char *mac_dest, char *mac_src);
static void process_ip6_packet(char *packet_bytes, char *mac_dest, char *mac_src);
static void process_arp_packet(char *packet_bytes, char *mac_dest, char *mac_src);

static void ip4_to_string(unsigned char *ip, char *buffer);
static void ip6_to_string(unsigned short *ip, char *buffer);
static void mac_to_string(unsigned char *ma, char *buffer);
static void insert_ip_addr(char *addr);
static void insert_mac_addr(char *addr);

// Opens a raw socket and returns its file descriptor
int netmon_init(char *device_name)
{
    int sockfd;
    struct ifreq ifr;
    struct sockaddr_ll sockaddr;

    // Open a raw socket
    sockfd = socket(AF_PACKET, SOCK_RAW, htons(ETH_P_ALL));
    
    if(sockfd == -1) {
        sprintf(error_msg, "Unable to open raw socket (root privelidges required)");
        return -1;
    }

    // Ensure device_name is really a network device name and determine device index
    memset(&ifr, 0, sizeof(struct ifreq));
    strcpy(ifr.ifr_name, device_name);
    if(ioctl(sockfd, SIOCGIFINDEX, &ifr) < 0) {
        sprintf(error_msg, "Improper device name");
        return -1;
    }

    // Bind address to socket
    memset(&sockaddr, 0, sizeof(struct sockaddr_ll));
    sockaddr.sll_family = AF_PACKET;
    sockaddr.sll_protocol = htons(ETH_P_ALL);
    sockaddr.sll_ifindex = ifr.ifr_ifindex;
    if(bind(sockfd, (struct sockaddr *)(&sockaddr), sizeof(struct sockaddr_ll)) == -1) {
        sprintf(error_msg, "Unable to bind address to socket");
        return -1;
    }

    // Initialize the netmon structure
    memset(&netmon, 0, sizeof(NETMON));
    netmon.start_time = time(NULL);
    netmon.ip_capacity = CHUNK;
    netmon.ip_addrs = (char **)malloc(netmon.ip_capacity * sizeof(char *));
    netmon.mac_capacity = CHUNK;
    netmon.mac_addrs = (char **)malloc(netmon.mac_capacity * sizeof(char *));

    return sockfd;
}

int netmon_mainloop(int sockfd)
{
    struct sockaddr_ll from;
    unsigned int addrlen;
    int len;
    char buffer[4096];
    time_t current_time;

    error_init_log();
    ui_init();

    for(;;) {
        len = recvfrom(sockfd, buffer, 4096, MSG_DONTWAIT, (struct sockaddr *)(&from), &addrlen);
        if(len > 0) process_packet(buffer, len);

        ui_display_ether_types(netmon.arp_count, netmon.ip4_count, netmon.ip6_count);
        ui_display_ip_types(netmon.tcp_count, netmon.udp_count, netmon.igmp_count, netmon.icmp_count);
        ui_display_arp_types(netmon.reply_count, netmon.request_count);

        if(len > 0) netmon.total_bytes += len;
        current_time = time(NULL) - netmon.start_time;
        if(current_time > 0)
            ui_display_rate(netmon.total_bytes, current_time);
    }

    return 1;
}

static void process_packet(char *packet_bytes, int len)
{
    PACKET_ETH_HDR eth_hdr;
    char mac_src[MACLENGTH + 1];
    char mac_dest[MACLENGTH + 1];

    memcpy(&eth_hdr, packet_bytes, sizeof(PACKET_ETH_HDR));
    mac_to_string(eth_hdr.eth_mac_src, mac_src);
    mac_to_string(eth_hdr.eth_mac_dest, mac_dest);
    insert_mac_addr(mac_src);
    insert_mac_addr(mac_dest);

    switch(ntohs(eth_hdr.eth_type)) {
        case ETH_TYPE_IP4:
            process_ip4_packet(packet_bytes + sizeof(PACKET_ETH_HDR), mac_dest, mac_src);
            break;
        case ETH_TYPE_IP6:
            process_ip6_packet(packet_bytes + sizeof(PACKET_ETH_HDR), mac_dest, mac_src);
            break;
        case ETH_TYPE_ARP:
            process_arp_packet(packet_bytes + sizeof(PACKET_ETH_HDR), mac_dest, mac_src);
            break;
        default:
            break;
    }
}

static void process_ip4_packet(char *packet_bytes, char *mac_dest, char *mac_src)
{
    PACKET_IP4_HDR ip4_hdr;
    char ip4_src[IP4LENGTH + 1];
    char ip4_dest[IP4LENGTH + 1];

    memcpy(&ip4_hdr, packet_bytes, sizeof(PACKET_IP4_HDR));
    netmon.ip4_count++;
    switch(ip4_hdr.ip4_protocol) {
        case IP_PROTOCOL_ICMP: 
            ui_display_packet(mac_dest, mac_src, "IPv4", "ICMP");
            netmon.icmp_count++;
            break;
        case IP_PROTOCOL_IGMP: 
            ui_display_packet(mac_dest, mac_src, "IPv4", "IGMP");
            netmon.igmp_count++;
            break;
        case IP_PROTOCOL_TCP: 
            ui_display_packet(mac_dest, mac_src, "IPv4", "TCP");
            netmon.tcp_count++;
            break;
        case IP_PROTOCOL_UDP: 
            ui_display_packet(mac_dest, mac_src, "IPv4", "UDP");
            netmon.udp_count++;
            break;
        default:
            ui_display_packet(mac_dest, mac_src, "IPv4", "UNKNOWN");
            sprintf(error_msg, "Unkown IPv4 protocol: %02x", ip4_hdr.ip4_protocol);
            log_error();
            break;
    }

    ip4_to_string(ip4_hdr.ip4_src, ip4_src);
    ip4_to_string(ip4_hdr.ip4_dest, ip4_dest);
    insert_ip_addr(ip4_src);
    insert_ip_addr(ip4_dest);
}

static void process_ip6_packet(char *packet_bytes, char *mac_dest, char *mac_src)
{
    PACKET_IP6_HDR ip6_hdr;
    char ip6_src[IP6LENGTH + 1];
    char ip6_dest[IP6LENGTH + 1];

    memcpy(&ip6_hdr, packet_bytes, sizeof(PACKET_IP6_HDR));
    netmon.ip6_count++;
    switch(ip6_hdr.ip6_protocol) {
        case IP_PROTOCOL_IGMP: 
            ui_display_packet(mac_dest, mac_src, "IPv6", "IGMP");
            netmon.igmp_count++;
            break;
        case IP_PROTOCOL_TCP: 
            ui_display_packet(mac_dest, mac_src, "IPv6", "TCP");
            netmon.tcp_count++;
            break;
        case IP_PROTOCOL_UDP: 
            ui_display_packet(mac_dest, mac_src, "IPv6", "UDP");
            netmon.udp_count++;
            break;
        case IP_PROTOCOL_IP6ICMP: 
            ui_display_packet(mac_dest, mac_src, "IPv6", "ICMP");
            netmon.icmp_count++;
            break;
        default:
            ui_display_packet(mac_dest, mac_src, "IPv6", "UNKNOWN");
            sprintf(error_msg, "Unkown IPv6 protocol: %02x", ip6_hdr.ip6_protocol);
            log_error();
            break;
    }

    ip6_to_string(ip6_hdr.ip6_src, ip6_src);
    ip6_to_string(ip6_hdr.ip6_dest, ip6_dest);
    insert_ip_addr(ip6_src);
    insert_ip_addr(ip6_dest);
}

static void process_arp_packet(char *packet_bytes, char *mac_dest, char *mac_src)
{
    PACKET_ARP_HDR arp_hdr;

    memcpy(&arp_hdr, packet_bytes, sizeof(PACKET_ARP_HDR));
    netmon.arp_count++;
    switch(ntohs(arp_hdr.arp_oper)) {
        case ARP_OPER_REQUEST:
            ui_display_packet(mac_dest, mac_src, "ARP", "REQUEST");
            netmon.request_count++;
            break;
        case ARP_OPER_REPLY:
            ui_display_packet(mac_dest, mac_src, "ARP", "REPLY");
            netmon.reply_count++;
            break;
        default:
            ui_display_packet(mac_dest, mac_src, "ARP", "UNKNOWN");
            sprintf(error_msg, "Unkown ARP operation: %04x", arp_hdr.arp_oper);
            log_error();
            break;
    }
}

static void ip6_to_string(unsigned short *ip, char *buffer)
{
    sprintf(buffer, "%01x:%01x:%01x:%01x:%01x:%01x:%01x:%01x", 
            ntohs(ip[0]), ntohs(ip[1]), ntohs(ip[2]), ntohs(ip[3]), ntohs(ip[4]), ntohs(ip[5]), ntohs(ip[6]), ntohs(ip[7]));
    buffer[IP6LENGTH] = '\0';
}

static void ip4_to_string(unsigned char *ip, char *buffer)
{
    sprintf(buffer, "%d.%d.%d.%d", 
            ip[0], ip[1], ip[2], ip[3]);
    buffer[IP4LENGTH] = '\0';
}

static void mac_to_string(unsigned char *ma, char *buffer)
{
    sprintf(buffer, "%02x:%02x:%02x:%02x:%02x:%02x", 
            ma[0], ma[1], ma[2], ma[3], ma[4], ma[5]);
    buffer[MACLENGTH] = '\0';
}

static void insert_ip_addr(char *addr)
{
    for(int i = 0; i < netmon.ip_len; ++i)
        if(strcmp(netmon.ip_addrs[i], addr) == 0) return;
    if(netmon.ip_len >= netmon.ip_capacity - 1) {
        netmon.ip_capacity *= 2;
        netmon.ip_addrs = (char **)realloc(netmon.ip_addrs, netmon.ip_capacity * sizeof(char *));
    }
    netmon.ip_addrs[netmon.ip_len++] = strdup(addr);
    ui_display_ip_addr(addr);
}

static void insert_mac_addr(char *addr)
{
    for(int i = 0; i < netmon.mac_len; ++i)
        if(strcmp(netmon.mac_addrs[i], addr) == 0) return;
    if(netmon.mac_len >= netmon.mac_capacity - 1) {
        netmon.mac_capacity *= 2;
        netmon.mac_addrs = (char **)realloc(netmon.mac_addrs, netmon.mac_capacity * sizeof(char *));
    }
    netmon.mac_addrs[netmon.mac_len++] = strdup(addr);
    ui_display_mac_addr(addr);
}
