#include "netmon.h"
#include "errors.h"
#include "ui.h"
#include "packet.h"

#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <linux/if_packet.h>
#include <net/ethernet.h> 

// Needed to check for device index
#include <sys/ioctl.h>
#include <net/if.h>

// The length of a macaddress
#define MACLENGTH 17

static void process_packet(char *packet_bytes, int len);
static void mac_to_string(unsigned char *ma, char *buffer);

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

    return sockfd;
}

int netmon_mainloop(int sockfd)
{
    struct sockaddr_ll from;
    unsigned int len, addrlen;
    char buffer[4096];

    ui_init();

    for(;;) {
        len = recvfrom(sockfd, buffer, 4096, 0, (struct sockaddr *)(&from), &addrlen);
        process_packet(buffer, len);
    }

    return 1;
}

static void process_packet(char *packet_bytes, int len)
{
    char mac_src[MACLENGTH + 1];
    char mac_dest[MACLENGTH + 1];
    PACKET_ETH_HDR eth_hdr;
    PACKET_ARP_HDR arp_hdr;
    PACKET_IP4_HDR ip4_hdr;

    memcpy(&eth_hdr, packet_bytes, sizeof(PACKET_ETH_HDR));
    mac_to_string(eth_hdr.eth_mac_src, mac_src);
    mac_to_string(eth_hdr.eth_mac_dest, mac_dest);

    switch(ntohs(eth_hdr.eth_type)) {
        case ETH_TYPE_IP4:
            memcpy(&ip4_hdr, packet_bytes + sizeof(PACKET_ETH_HDR), sizeof(PACKET_IP4_HDR));
            switch(ip4_hdr.ip4_protocol) {
                case IP_PROTOCOL_TCP: 
                    ui_display_packet(mac_dest, mac_src, "IPv4", "TCP");
                    break;
                case IP_PROTOCOL_UDP: 
                    ui_display_packet(mac_dest, mac_src, "IPv4", "UDP");
                    break;
                default:
                    ui_display_packet(mac_dest, mac_src, "IPv4", "UNKNOWN");
                    break;
            }
            break;
        case ETH_TYPE_IP6:
            ui_display_packet(mac_dest, mac_src, "IPv6", "UNKNOWN");
            break;
        case ETH_TYPE_ARP:
            memcpy(&arp_hdr, packet_bytes + sizeof(PACKET_ETH_HDR), sizeof(PACKET_ARP_HDR));
            switch(ntohs(arp_hdr.arp_oper)) {
                case ARP_OPER_REQUEST:
                    ui_display_packet(mac_dest, mac_src, "ARP", "REQUEST");
                    break;
                case ARP_OPER_REPLY:
                    ui_display_packet(mac_dest, mac_src, "ARP", "REPLY");
                    break;
                default:
                    ui_display_packet(mac_dest, mac_src, "ARP", "UNKNOWN");
                    break;
            }
            break;
        default:
            break;
    }
}

static void mac_to_string(unsigned char *ma, char *buffer)
{
    sprintf(buffer, "%02x:%02x:%02x:%02x:%02x:%02x", 
            ma[0], ma[1], ma[2], ma[3], ma[4], ma[5]);
    buffer[MACLENGTH] = '\0';
}
