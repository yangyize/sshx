#include <stdio.h>
#include <getopt.h>
#include <stdlib.h>

void usage();

int main(int argc, char *argv[]) {
    int optc;
    long port = 22;
    char *tmp;

    if (argc <= 1) {
        usage();
    } else {
        while ((optc = getopt(argc, argv, "p::s::c::l::r")) != -1) {
            printf("opt = %c\t\t", optc);
            printf("optarg = %s\t\t", optarg);
            printf("optind = %d\t\t", optind);
            printf("argv[optind] = %s\n", argv[optind]);
            switch (optc) {
                case 0:
                    break;
                case 'p':
                    if (optarg != NULL) {
                        port = strtol(optarg, &tmp, 10);
                    } else if (argv[optind] != NULL) {
                        port = strtol(argv[optind], &tmp, 10);
                    }
                    if (port > 65535) {
                        fprintf(stderr, "Error: port is too big\n");
                        exit(1);
                    }
                    printf("port %d\n", port);
            }
        }
    }
    return 0;
}

void usage() {
    printf("usage: sshx [user@host] [-p port] [-s password]\n");
    printf("-c\tconnect to index");
    printf("-l\tlist saved ssh record\n");
    printf("-r\tremove index");
}