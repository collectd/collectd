/*
 * I cannot claim credit for this program; it was shamelessly stolen from:
 *
 * http://www.vttoth.com/tunnel.htm
 */
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <netdb.h>
#include <netinet/in.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <syslog.h>

int main(int argc, char *argv[]) {
    struct sockaddr_in saSRC, saDST1, saDST2, saRCV;
    char cBuf[1 << 16];
    int sSRC = 0, sDST1 = 0, sDST2 = 0;
    struct hostent *phs, *phd1, *phd2;
    int nLen;
    int bForeground = 0;
    int bOneHost = 0;
    //int on = 1;
    pid_t pid;
    unsigned int nAS;
    unsigned long s, d1, d2;
    unsigned long aDST1 = 0, aDST2 = 0;

    char *pszApp = *argv++;

    if (argc > 1 && !strcmp(*argv, "-f")) {
        bForeground = 1;
        argc--;
        argv++;
    }

    if (argc < 4) {
        printf("Usage: %s [-f] <port-number> <source-ip-address> <dest-ip-address1> [<dest-ip-address2>]\n", pszApp);
        return 1;
    }
    if (argc == 4) { bOneHost = 1; }

    phs = gethostbyname(argv[1]);
    if (phs == NULL) {
        printf("Invalid address %s\n", argv[1]);
        return 1;
    }
    s = *(unsigned long *)phs->h_addr;
    phd1 = gethostbyname(argv[2]);
    if (phd1 == NULL) {
        printf("Invalid address %s\n", argv[2]);
        return 1;
    }
    d1 = *(unsigned long *)phd1->h_addr;
    if (bOneHost == 0) {
        phd2 = gethostbyname(argv[3]);
        if (phd2 == NULL) {
            printf("Invalid address %s\n", argv[3]);
            return 1;
        }
        d2 = *(unsigned long *)phd2->h_addr;
    }
    saDST1.sin_family = AF_INET;
    saDST1.sin_port = htons(atoi(argv[0]));
    saDST1.sin_addr.s_addr = d1;

    if (bOneHost == 0) {
        saDST2.sin_family = AF_INET;
        saDST2.sin_port = htons(atoi(argv[0]));
        saDST2.sin_addr.s_addr = d2;
    }

    saSRC.sin_family = AF_INET;
    saSRC.sin_port = htons(atoi(argv[0]));
    saSRC.sin_addr.s_addr = s;

    sSRC = socket(AF_INET, SOCK_DGRAM, 0);
    sDST1 = socket(AF_INET, SOCK_DGRAM, 0);
    if (bOneHost == 0) {
        sDST2 = socket(AF_INET, SOCK_DGRAM, 0);
    }

    if (bind(sSRC, (struct sockaddr *)&saSRC, sizeof (saSRC))) {
        printf("Unable to bind to socket %s\n", argv[1]);
        return 1;
    }

    //setsockopt(sDST, SOL_SOCKET, SO_BROADCAST, &on, sizeof (on));

    if (!bForeground) {
        close(0);
        close(1);
        close(2);
        pid = fork();
        if (pid < 0) {
            syslog(LOG_ERR, "Could not go into background.");
        }
        if (pid > 0) {
            return 0;
        }
    }

    aDST1 = htonl(saDST1.sin_addr.s_addr);
    if (bOneHost == 0) {
        aDST2 = htonl(saDST2.sin_addr.s_addr);
    }

    while (1) {
        nAS = sizeof (saRCV);
        nLen = recvfrom(sSRC, cBuf, sizeof (cBuf), 0, (struct sockaddr *)&saRCV, &nAS);

        if (nLen > 0) {
            if (((unsigned char *)&saRCV.sin_addr.s_addr)[3] < 50) {
                saDST1.sin_port = htons(atoi(argv[0]) + 1);
                if (bOneHost == 0) {
                    saDST2.sin_port = htons(atoi(argv[0]) + 1);
                }
            } else if (((unsigned char *)&saRCV.sin_addr.s_addr)[3] < 101) {
                saDST1.sin_port = htons(atoi(argv[0]) + 2);
                if (bOneHost == 0) {
                    saDST2.sin_port = htons(atoi(argv[0]) + 2);
                }
            } else if (((unsigned char *)&saRCV.sin_addr.s_addr)[3] < 170) {
                saDST1.sin_port = htons(atoi(argv[0]) + 3);
                if (bOneHost == 0) {
                    saDST2.sin_port = htons(atoi(argv[0]) + 3);
                }
            } else {
                saDST1.sin_port = htons(atoi(argv[0]) + 4);
                if (bOneHost == 0) {
                    saDST2.sin_port = htons(atoi(argv[0]) + 4);
                }
            }
            sendto(sDST1, cBuf, nLen, 0, (struct sockaddr *)&saDST1, sizeof (saDST1));
            if (bOneHost == 0) {
                sendto(sDST2, cBuf, nLen, 0, (struct sockaddr *)&saDST2, sizeof (saDST2));
            }
        }
    }
}
