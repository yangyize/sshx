#include <stdio.h>
#include <getopt.h>
#include <stdlib.h>
#include <string.h>
#include <zconf.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <sys/ioctl.h>

void usage(int status);

char *opt_str(char *optarg, char *argv_optind);

void child(char *user, char *address, long port, char *name, char *password);

char *makestr(char *buf);

void sigchild_handler(int signum);

void winch_handler(int signum);
git
int handleoutput(int fd);

struct option long_options[] = {
        {"address",  required_argument, NULL, 'a'},
        {"help",     no_argument,       NULL, 'h'},
        {"id",       required_argument, NULL, 'i'},
        {"name",     required_argument, NULL, 'n'},
        {"password", required_argument, NULL, 'P'},
        {"port",     required_argument, NULL, 'p'},
        {"user",     required_argument, NULL, 'u'},
        {0, 0, 0,                             0}
};

static int master_pt_fd;
static int tty_fd;
static char *pt_name;
char password[1024] = {'\0'};

int main(int argc, char *argv[]) {
    int newpid;
    int optc;
    struct winsize wbuf;

    char user[1024] = "root";
    char address[1024] = {'\0'};
    char name[1024] = {'\0'};
    long port = 22;
    char *port_p;

    char *tmp;
    int optionindex = -1;


    if (argc <= 1) {
        usage(1);
    } else {
        while ((optc = getopt_long(argc, argv, "a:hi:n:P:p:u:", long_options, &optionindex)) != -1) {
            printf("opt = %c\t\t", optc);
            printf("optarg = %s\t\t", optarg);
            printf("optind = %d\t\t", optind);
            printf("argv[optind] = %s\n", argv[optind]);
            switch (optc) {
                case 0:
                    break;
                case 'a':
                    strcpy(address, opt_str(optarg, argv[optind]));
                    break;
                case 'n':
                    strcpy(name, opt_str(optarg, argv[optind]));
                    break;
                case 'p':
                    port_p = opt_str(optarg, argv[optind]);
                    if (port_p != NULL) {
                        port = strtol(port_p, &tmp, 10);
                    }
                    if (port > 65535) {
                        fprintf(stderr, "Error: port is too big\n");
                        exit(1);
                    }
                    break;
                case 'P':
                    strcpy(password, opt_str(optarg, argv[optind]));
                    break;
                case 'u':
                    strcpy(user, opt_str(optarg, argv[optind]));
                case 'h':
                    usage(0);
                    return 0;
            }
        }

        printf("optind: %d\n", optind);

        signal(SIGCHLD, sigchild_handler);

        master_pt_fd = open("/dev/ptmx", O_RDWR); // like posix_openpt
        if (master_pt_fd == -1) {
            perror("Could not get a terminal");
            exit(2);
        }

        fcntl(master_pt_fd, F_SETFL, O_NONBLOCK);

        tty_fd = open("/dev/tty", O_RDWR);
        if (tty_fd == -1) {
            perror("Could not open tty");
            exit(2);
        }

        if (ioctl(tty_fd, TIOCGWINSZ, &wbuf) != -1) {
            signal(SIGWINCH, winch_handler);
            ioctl(master_pt_fd, TIOCGWINSZ, &wbuf);
        }

        pt_name = ttyname(master_pt_fd);

        if ((newpid = fork()) == -1) {
            perror("fork");
        } else if (newpid == 0) {
            child(user, address, port, name, password);
        } else if (newpid < 0) {
            perror("fail ");
        }

        int slave_pt_fd = open(pt_name, O_RDWR | O_NOCTTY);

        int status = 0;
        int terminate = 0;
        pid_t wait_id;
        sigset_t sigmask, sigmask_select;

        sigemptyset(&sigmask_select);

        sigemptyset(&sigmask);
        sigaddset(&sigmask, SIGCHLD);

        sigprocmask(SIG_SETMASK, &sigmask, NULL);

        do {
            if (!terminate) {
                fd_set readfd;
                FD_ZERO(&readfd);
                FD_SET(master_pt_fd, &readfd);

                printf("try to pselect\n");
                int selret = pselect(master_pt_fd + 1, &readfd, NULL, NULL, NULL, &sigmask_select);
                printf("pselect %d\n", selret);
                if (selret > 0) {
                    if (FD_ISSET(master_pt_fd, &readfd)) {
                        int ret;
                        if ((ret = handleoutput(master_pt_fd))) {
                            if (ret > 0) {
                                close(master_pt_fd);
                                close(slave_pt_fd);
                            }

                            terminate = ret;

                            if (terminate) {
                                close(slave_pt_fd);
                            }
                        }
                    }
                }
                wait_id = waitpid(newpid, &status, WNOHANG);
            } else {
                wait_id = waitpid(newpid, &status, 0);
            }
        } while (wait_id == 0 || (!WIFEXITED(status) && !WIFSIGNALED(status)));
    }
    return 0;
}


void child(char *user, char *address, long port, char *name, char *password) {
//    setsid();
    int sla_fd;
    char *arglist[1024];
    int numargs = 0;
    char *temp1 = malloc(2049);
    char *temp2 = malloc(2049);
    char *ssh = "ssh";

    sla_fd = open(pt_name, O_RDWR);
    close(sla_fd);
    close(master_pt_fd);
    arglist[numargs++] = ssh;

    strcpy(temp1, user);
    strcat(temp1, "@");
    strcat(temp1, address);
    arglist[numargs++] = makestr(temp1);

    memset(temp1, 0, 2049);

    memset(temp1, 0, 2049);
    sprintf(temp2, "%li", port);
    strcat(temp1, "-p");
    strcat(temp1, temp2);
    arglist[numargs++] = makestr(temp1);

    for (int i = 0; i < numargs; i++) {
        printf("%s\n", arglist[i]);
    }

    execvp(arglist[0], arglist);

    perror("execvp fail");
    exit(1);
}

char *opt_str(char *optarg, char *argv_optind) {
    char *temp = malloc(1024);
    if (optarg != NULL) {
        strcpy(temp, optarg);
    } else if (argv_optind != NULL) {
        strcpy(temp, argv_optind);
    } else {
        return NULL;
    }
    return temp;
}

char *makestr(char *buf) {
    char *cp;
    buf[strlen(buf)] = '\0';
    cp = malloc(strlen(buf) + 1);
    if (cp == NULL) {
        fprintf(stderr, "no memory\n");
        exit(1);
    }
    strcpy(cp, buf);
    return cp;
}

void usage(int status) {
    if (status != 0) {
        fprintf(stderr, "Try 'sshx --help' for more information.\n");
    } else {
        printf("Usage: sshx [user@host:port] [-P password] [-n name]\n"
               "  or: sshx [user@host] [-p port] [-P password] [-n name]\n"
               "  -a, --address   bind address\n"
               "  -i, --id        ssh connect id\n"
               "  -n, --name      ssh connect name\n"
               "  -P, --password  ssh qconnect name\n"
               "  -p, --port      port\n"
               "  -u, --user      user\n"
               "  -h, --help      help\n");
    }
}

int handleoutput(int fd) {
    static int prevmatch = 0;

    char buffer[256];
    int ret = 0;

    int numread = (int) read(fd, buffer, sizeof(buffer) - 1);
    buffer[numread] = '\0';

    if (!prevmatch) {
        write(fd, password, strlen(password));
        write(fd, "\n", 1);
        prevmatch = 1;
    } else {
        fprintf(stderr, "wrong password");
        ret = -1;
    }

    return ret;
}

void sigchild_handler(int signum) {

}

void winch_handler(int signum) {
    struct winsize wsize;
    if (ioctl(tty_fd, TIOCGWINSZ, &wsize) == 0) {
        ioctl(master_pt_fd, TIOCGWINSZ, &wsize);
    }
}