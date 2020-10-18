#include <stdint.h>
#include <linux/if.h>
#include <linux/if_tun.h>
#include <stdio.h>
#include <sys/select.h>
#include "hexdump.h"
#include "wifi_if.h"
#include <unistd.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>

int openTun(const char *name) {
	struct ifreq ifr;
	int fd, r;
	memset(&ifr, 0, sizeof(ifr));
	ifr.ifr_flags=IFF_TAP|IFF_NO_PI;
	strcpy(ifr.ifr_name, name);
	fd=open("/dev/net/tun", O_RDWR);
	if (fd<=0) {
		perror("/dev/net/tun");
		exit(1);
	}
	r=ioctl(fd, TUNSETIFF, (void*)&ifr);
	if (r) {
		perror("TUNSETIFF");
		exit(1);
	}
	if(ioctl(fd, TUNSETPERSIST, 1) < 0){
		perror("enabling TUNSETPERSIST");
		exit(1);
	}
	fcntl(fd, F_SETFL, fcntl(fd, F_GETFL) | O_NONBLOCK);
	return fd;
}

static int tapfd=0;

void wifi_if_open() {
	tapfd=openTun("pdptap");
	printf("Tap device opened.\n");
}

void wifi_if_close() {
	close(tapfd);
}

int wifi_if_write(uint8_t *packet, int len) {
	printf("Writing to tap:\n");
	hexdump(packet, len);
	return write(tapfd, packet, len);
}

/*
00000000  00 00 08 06 ff ff ff ff  ff ff a6 0f 06 0c 67 94  |..............g.|
00000010  08 06 00 01 08 00 06 04  00 01 a6 0f 06 0c 67 94  |..............g.|
00000020  ce 8b ca 01 00 00 00 00  00 00 ce 8b ca c9        |..............|
0000002e
*/

int wifi_if_read(uint8_t *packet, int maxlen) {
	fd_set rfds;
	FD_ZERO(&rfds);
	FD_SET(tapfd, &rfds);
	struct timeval timeout={};
	int r=select(tapfd+1, &rfds, NULL, NULL, &timeout);
	if (r) {
		int len=read(tapfd, packet, maxlen);
		printf("Read from tap:\n");
		hexdump(packet, len);
		return len;
	}
	return 0;
}

void wifi_if_get_mac(char *txtmac) {
	sprintf(txtmac, "11:22:33:44:55:66");
}
