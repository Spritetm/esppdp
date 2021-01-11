/*
This is the source for wifid, a program that runs on 2.11BSD to send out broadcast
UDP packets that get picked up by the network layer on the ESP32. It can instruct the
ESP32 code to scan for WiFi networks and connect to them. The ESP32 will then 
communicate the IP/netmask/... it received so wifid can configure the 2.11bsd network
interface accordingly.

This is supposed to be crosscompiled with gcc. I'm decently sure the native compiler will
choke on the majority of lines of this code.
*/
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <netdb.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <stdint.h>
#include <syslog.h>

#define MAXLINE 1024
#define PERSIST_FILE "/etc/wifid.persist"

#include "wifid_iface.h"

//No clue what header this is hidden in here... should be in sys/socket.h but it's missing there?
int socket(int domain, int type, int protocol);
int recvfrom(int s, char *buf, int len, int flags, struct sockaddr *from, int *fromlen);
int setsockopt(int sockfd, int level, int optname, const void *optval, int optlen);
int bind(int sockfd, const struct sockaddr *addr, int addrlen);

int create_sock() {
	int sockfd; 
	if ((sockfd = socket(AF_INET, SOCK_DGRAM, 0)) < 0 ) { 
		perror("socket creation failed"); 
		exit(1); 
	} 
	int broadcastEnable=1;
	if (setsockopt(sockfd, SOL_SOCKET, SO_BROADCAST, &broadcastEnable, sizeof(broadcastEnable))==-1) {
		perror("SO_BROADCAST"); 
		exit(1); 
	}
	return sockfd;
}

void bind_sock(int sockfd) {
	struct sockaddr_in servaddr;
	memset(&servaddr, 0, sizeof(servaddr));
	servaddr.sin_family = AF_INET; // IPv4 
	servaddr.sin_addr.s_addr = INADDR_ANY; 
	servaddr.sin_port = htons(PORT_RECV); 
	if (bind(sockfd, (const struct sockaddr *)&servaddr, sizeof(servaddr)) < 0) {
		perror("bind failed"); 
		exit(EXIT_FAILURE); 
	}
}

void send_quit_packet() {
	int sockfd=create_sock();
	struct sockaddr_in servaddr; 
	memset(&servaddr, 0, sizeof(servaddr)); 
	servaddr.sin_family = AF_INET; 
	servaddr.sin_addr.s_addr = inet_addr("255.255.255.255");
	servaddr.sin_port = htons(PORT_RECV);
	wifid_event_t ev={.resp=EV_QUIT};
	int r=sendto(sockfd, (const char *)&ev, sizeof(ev), 0, (const struct sockaddr *) &servaddr, sizeof(servaddr)); 
	if (r<0) perror("sendto for quit message"); //error is not fatal
	close(sockfd);
	usleep(100000);
}

//trims crap like line endings from a string
void trim(char *s) {
	while (strlen(s)!=0 && s[strlen(s)-1]<32) s[strlen(s)-1]=0;
}

//Reads ssid and pass from persist file.
int read_persist_file(char **ssid, char **pass) {
	char *r_ssid=malloc(65);
	char *r_pass=malloc(65);
	FILE *f=fopen(PERSIST_FILE, "r");
	if (f==NULL) goto err;
	if (fgets(r_ssid, 65, f)==NULL) goto err;
	if (fgets(r_pass, 65, f)==NULL) goto err;
	trim(r_ssid);
	trim(r_pass);
	*ssid=r_ssid;
	*pass=r_pass;
	fclose(f);
	return 1;
err:
	fclose(f);
	free(r_ssid);
	free(r_pass);
	return 0;
}


void write_persist_file(char *ssid, char *pass) {
	FILE *f=fopen(PERSIST_FILE, "w");
	fprintf(f, "%s\n", ssid);
	fprintf(f, "%s\n", pass);
	fclose(f);
}

void printhex(uint8_t p) {
	char hexc[]="0123456789ABCDEF";
	putchar(hexc[(p>>4)&0xf]);
	putchar(hexc[(p)&0xf]);
	putchar(' ');
}

void recv_wifid_evt(int sockfd, wifid_event_t *ev) {
	int n, len;
	struct sockaddr_in servaddr; 
	n = recvfrom(sockfd, (char *)ev, sizeof(wifid_event_t),  
				0, (struct sockaddr *) &servaddr, 
				&len);
	if (n!=sizeof(wifid_event_t)) {
		printf("Huh? weirdly-sized event received. (Ex %d got %d)\n", sizeof(wifid_event_t), n);
		return;
	}
#if 0
	char *p=(char*)ev;
	for (int i=0; i<sizeof(wifid_event_t); i++) {
		printhex(*p++);
		if ((i&15)==15) printf("\n");
	}
	printf("\n");
#endif
} 

void send_wifid_cmd(int sockfd, wifid_cmd_t *cmd) {
	struct sockaddr_in servaddr; 
	memset(&servaddr, 0, sizeof(servaddr)); 
	servaddr.sin_family = AF_INET; 
	servaddr.sin_addr.s_addr = inet_addr("255.255.255.255");
	servaddr.sin_port = htons(PORT_SEND); 

	int r=sendto(sockfd, (const char *)cmd, sizeof(wifid_cmd_t), 0, (const struct sockaddr *) &servaddr, 
					sizeof(servaddr)); 
	if (r<0) {
		perror("sendto");
		exit(1);
	}
}

#define IP(x) (int)x[0]&0xff,(int)x[1]&0xff,(int)x[2]&0xff,(int)x[3]&0xff

void configure_interface(const char *interface, wifid_event_t *ev) {
	char buff[1024];
	uint8_t bcast[4];
	for (int i=0; i<4; i++) {
		bcast[i]=ev->connected.ip[i]|(ev->connected.netmask[i]^255);
	}
	sprintf(buff, "ifconfig %s inet %d.%d.%d.%d netmask %d.%d.%d.%d broadcast %d.%d.%d.%d up",
		interface, IP(ev->connected.ip), IP(ev->connected.netmask), IP(bcast));
	syslog(LOG_INFO, "Wifid: %s", buff);
	system(buff);
	FILE *rcf=fopen("/etc/resolv.conf", "w");
	if (rcf) {
		fprintf(rcf, "domain localdomain\n");
		for (int i=0; i<3; i++) {
			if (ev->connected.nameserver[i]) {
				fprintf(rcf, "nameserver %d.%d.%d.%d\n", IP(ev->connected.nameserver[i]));
				syslog(LOG_INFO, "Wifid: nameserver %d is  %d.%d.%d.%d", i, IP(ev->connected.nameserver[i]));
			}
		}
		fclose(rcf);
	} else {
		syslog(LOG_ERR, "wifid: Could not open resolv.conf!");
	}
	sprintf(buff, "route add default %d.%d.%d.%d 1", IP(ev->connected.gw));
	syslog(LOG_INFO, "Wifid: %s", buff);
	system(buff);
}

#define MODE_NONE 0
#define MODE_SCAN 1
#define MODE_CONNECT 2
#define MODE_AUTO 3
#define MODE_ERR -1

int main(int argc, char **argv) { 
	int mode=MODE_NONE;

	const char *interface="qe0";
	char *ssid="";
	char *pass="";

	for (int i=1; i<argc; i++) {
		if (strcmp(argv[i], "-scan")==0) {
			if (mode==MODE_NONE) mode=MODE_SCAN; else mode=MODE_ERR;
		} else if (strcmp(argv[i], "-connect")==0 && argc>i+2) {
			if (mode==MODE_NONE) mode=MODE_CONNECT; else mode=MODE_ERR;
			ssid=argv[i+1];
			pass=argv[i+2];
			i+=2;
		} else if (strcmp(argv[i], "-if")==0 && argc>i+1) {
			interface=argv[i+1];
			i+=1;
		} else if (strcmp(argv[i], "-auto")==0) {
			if (mode==MODE_NONE) mode=MODE_AUTO; else mode=MODE_ERR;
		} else {
			mode=MODE_ERR;
			break;
		}
	}
	if (mode==MODE_ERR || mode==MODE_NONE) {
		printf("Usage: %s [-if interface] [-scan|-connect ssid pass|-auto]\n", argv[0]);
		printf("  -if interface - network if to use, defaults to qe0\n");
		printf("  -scan - Scan for WiFi access points\n");
		printf("  -connect - Connect to given AP with give password and daemonize\n");
		printf("  -auto - Read /etc/wifid to connect to previous access point\n");
		printf("Note: any of these invocations will kill any currently running wifid.\n");
		exit(0);
	}

	//Make any running wifid instance quit so we're the only one.
	send_quit_packet();


	if (mode==MODE_AUTO) {
		if (read_persist_file(&ssid, &pass)) {
			printf("Auto-connecing to SSID '%s'.\n", ssid);
			mode=MODE_CONNECT;
		} else {
			printf("Couldn't read persist file. Please manually run %s -connect\n", argv[0]);
			exit(1);
		}
	}

	//Create a socket to communicate with the wifi logic running on the ESP32
	int sockfd=create_sock();
	bind_sock(sockfd);

	if (mode==MODE_SCAN) {
		printf("Scanning...\n");
		wifid_cmd_t cmd={
			.cmd=CMD_SCAN
		};
		send_wifid_cmd(sockfd, &cmd);
		while(1) {
			wifid_event_t ev;
			recv_wifid_evt(sockfd, &ev);
			if (ev.resp==EV_SCAN_RES || ev.resp==EV_SCAN_RES_END) {
				printf("Found network: %s\n", ev.scan_res.ssid);
				if (ev.resp==EV_SCAN_RES_END) {
					printf("Scan done.\n");
					break;
				}
			} else {
				printf("Huh? Unexpected response %d\n", ev.resp);
				exit(1);
			}
		}
	} else if (mode==MODE_CONNECT) {
		wifid_cmd_t cmd={
			.cmd=CMD_CONNECT
		};
		strcpy(cmd.connect.ssid, ssid);
		strcpy(cmd.connect.pass, pass);
		send_wifid_cmd(sockfd, &cmd);
		printf("Wifid: Wait for connect...\n");
		wifid_event_t ev;
		recv_wifid_evt(sockfd, &ev);
		if (ev.resp!=EV_GOT_IP) {
			printf("Wifid: Huh? Did not get connected event from WiFi driver.\n");
			exit(1);
		}
		printf("Wifid: Connected. Configuring interface and backgrounding.\n");
		//ToDo: ...at which IP?
		write_persist_file(ssid, pass);
		//Background ourselves.
		daemon(0,0);
		while(1) {
			//Note: when we're daemonized, we ignore not-recognized events instead of failing.
			if (ev.resp==EV_GOT_IP) {
				configure_interface(interface, &ev);
			} else if (ev.resp==EV_QUIT) {
				exit(0);
			} else {
				syslog(LOG_ERR, "Wifid: unrecognized event %d", ev.resp);
			}
			recv_wifid_evt(sockfd, &ev);
		}
	}
}
