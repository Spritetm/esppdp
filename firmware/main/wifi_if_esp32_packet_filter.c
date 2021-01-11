//WiFi packet filter. The routines here decide where a certain packet needs to be
//sent to. (pdp11 or lwip).
/*
 * ----------------------------------------------------------------------------
 * "THE BEER-WARE LICENSE" (Revision 42):
 * Jeroen Domburg <jeroen@spritesmods.com> wrote this file. As long as you retain 
 * this notice you can do whatever you want with this stuff. If we meet some day, 
 * and you think this stuff is worth it, you can buy me a beer in return. 
 * ----------------------------------------------------------------------------
 */

#include "wifi_if_esp32_packet_filter.h"
#include "lwip/prot/ethernet.h"
#include "lwip/prot/ieee.h"
#include "lwip/prot/etharp.h"
#include "lwip/prot/ip.h"
#include "lwip/prot/ip4.h"
#include "lwip/prot/udp.h"
#include "wifi_if.h"
#include "wifid_iface.h"
#include "wifid.h"
#include "hexdump.h"

#define PACKET_DEST_PDP11 1
#define PACKET_DEST_LWIP 2

//Because we have a split personality TCP/IP stack (both the PDP11 and the LWIP
//stack think that they own the interface), we need to nicely divy up where we send
//received packets.
int wifi_if_filter_find_packet_dest(uint8_t *buffer, uint16_t len) {
	struct eth_hdr *eth=(struct eth_hdr*)buffer;
	if (ntohs(eth->type)==ETHTYPE_ARP) {
		//Arp packet. We only let one stack respond to requests, but we send responses to
		//all stacks.
		struct etharp_hdr *arp=(struct etharp_hdr*)(buffer+sizeof(struct eth_hdr));
		if (ntohs(arp->opcode)==ARP_REQUEST) {
			return PACKET_DEST_PDP11;
		} else {
			return PACKET_DEST_PDP11|PACKET_DEST_LWIP;
		}
	}
	if (ntohs(eth->type)==ETHTYPE_IP || ntohs(eth->type)<1500) {
		struct ip_hdr *iphdr=(struct ip_hdr*)(buffer+sizeof(struct eth_hdr));
		if (IP_HDR_GET_VERSION(iphdr)==4) {
			if (IPH_PROTO(iphdr)==IP_PROTO_UDP) {
				//Forward all bootp/dhcp packets to lwip, all others to the PDP11.
				struct udp_hdr *udphdr=(struct udp_hdr*)(buffer+sizeof(struct eth_hdr)+IPH_HL_BYTES(iphdr));
				//printf("udp type off eth %x ip %x src %x dest %x\n", sizeof(struct eth_hdr),IPH_HL_BYTES(iphdr) ,ntohs(udphdr->src), ntohs(udphdr->dest));
				if (ntohs(udphdr->src)==67 || ntohs(udphdr->dest)==67) return PACKET_DEST_LWIP;
				if (ntohs(udphdr->src)==68 || ntohs(udphdr->dest)==68) return PACKET_DEST_LWIP;
			}
		}
	}
	//default: pdp11
	return PACKET_DEST_PDP11;
}

//The wifid daemon on the PDP11 will use broadcast packets to port 67/68 to communicate with the WiFi
//part of the simulator. Returns 1 if the packet should not be forwarded to the WiFi interface,
//0 otherwise.
int wifi_if_filter_pdp11_packet(uint8_t *buffer, uint16_t len) {
	struct eth_hdr *eth=(struct eth_hdr*)buffer;
	if (ntohs(eth->type)==ETHTYPE_IP || ntohs(eth->type)<1500) {
		struct ip_hdr *iphdr=(struct ip_hdr*)(buffer+sizeof(struct eth_hdr));
		if (IP_HDR_GET_VERSION(iphdr)==4) {
			if (IPH_PROTO(iphdr)==IP_PROTO_UDP) {
				struct udp_hdr *udphdr=(struct udp_hdr*)(buffer+sizeof(struct eth_hdr)+IPH_HL_BYTES(iphdr));
				if (ntohs(udphdr->dest)==67 || ntohs(udphdr->dest)==68) {
					int hdrlen=sizeof(struct eth_hdr)+IPH_HL_BYTES(iphdr)+sizeof(struct udp_hdr);
					wifid_parse_packet(buffer+hdrlen, len-hdrlen);
					return 1;
				}
			}
		}
	}
	return 0;
}
