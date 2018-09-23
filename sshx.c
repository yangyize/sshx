#include <stdio.h>
#include <getopt.h>
#include <stdlib.h>
#include <string.h>
#include <zconf.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <signal.h>

#define FILE_PATH "./ssh.txt"

enum use_code {
    LIST_REC,
    USE_ID,
    ACCESS_SAVE,
    IGNORE_SAVE,
};

struct {
    int id;
    char *name;
    char *user;
    char *host;
    int port;
    char *password;
    int type;
} user_obj;

void usage(int status);

char *opt_str(char *optarg, char *argv_optind);

void child();

char *makestr(char *buf);

void sigchild_handler(int signum);

void winch_handler(int signum);

int handleoutput(int fd);

void read_index();

void list_rec();

void save_access();

long fsize(FILE *file);

void fcopy(FILE *fdst, long d, FILE *fsrc, long s, long offset);

char *fname(FILE *file);

int fupdate(FILE *file, long offset, void *buffer, int newlen, int oldlen);

void init_struct();

struct option long_options[] = {
        {"address",  required_argument, NULL, 'a'},
        {"help",     no_argument,       NULL, 'h'},
        {"id",       required_argument, NULL, 'i'},
        {"list",     no_argument,       NULL, 'l'},
        {"name",     required_argument, NULL, 'n'},
        {"password", required_argument, NULL, 'P'},
        {"port",     required_argument, NULL, 'p'},
        {"user",     required_argument, NULL, 'u'},
        {0, 0, 0,                             0}
};

static int master_pt_fd;
static int tty_fd;
static char *pt_name;

int main(int argc, char *argv[]) {
    int newpid;
    int optc;
    struct winsize wbuf;

    char *tmp;
    int optionindex = -1;

    init_struct();
    if (argc <= 1) {
        usage(1);
    } else {
        while ((optc = getopt_long(argc, argv, "a:hi:ln:P:p:u:", long_options, &optionindex)) != -1) {
            printf("opt = %c\t\t", optc);
            printf("optarg = %s\t\t", optarg);
            printf("optind = %d\t\t", optind);
            printf("argv[optind] = %s\n", argv[optind]);
            char *port_str;
            char *id_str;
            switch (optc) {
                case 0:
                    break;
                case 'a':
                    strcpy(user_obj.host, opt_str(optarg, argv[optind]));
                    break;
                case 'i':
                    id_str = opt_str(optarg, argv[optind]);
                    if (id_str) {
                        long id = strtol(id_str, &tmp, 10);
                        if (id > 65535) {
                            fprintf(stderr, "Error: id is too big\n");
                        }
                        user_obj.id = (int) id;
                        user_obj.type = USE_ID;
                    }
                    break;
                case 'I':
                    user_obj.type = IGNORE_SAVE;
                    break;
                case 'n':
                    strcpy(user_obj.name, opt_str(optarg, argv[optind]));
                    break;
                case 'p':
                    port_str = opt_str(optarg, argv[optind]);
                    long port = 22;
                    if (port_str != NULL) {
                        port = strtol(port_str, &tmp, 10);
                    }
                    if (port > 65535) {
                        fprintf(stderr, "Error: port is too big\n");
                        exit(1);
                    }
                    user_obj.port = (int) port;
                    break;
                case 'l':
                    user_obj.type = LIST_REC;
                    break;
                case 'P':
                    strcpy(user_obj.password, opt_str(optarg, argv[optind]));
                    break;
                case 'u':
                    strcpy(user_obj.user, opt_str(optarg, argv[optind]));
                    break;
                case 'h':
                    usage(0);
                    return 0;
            }
        }

        printf("optind: %d\n", optind);

        signal(SIGCHLD, sigchild_handler);

        master_pt_fd = open("/dev/ptmx", O_RDWR | O_NONBLOCK); // like posix_openpt
        if (master_pt_fd == -1) {
            perror("Could not get a terminal");
            exit(2);
        }

        if (grantpt(master_pt_fd) != 0) {
            perror("Could not grant access to the slave");
            exit(1);
        }

        if (unlockpt(master_pt_fd) != 0) {
            perror("Could not unclock");
            exit(1);
        }

        tty_fd = open("/dev/tty", O_RDWR | O_NONBLOCK);
        if (tty_fd == -1) {
            perror("Could not open tty");
            exit(2);
        }

        if (ioctl(tty_fd, TIOCGWINSZ, &wbuf) != -1) { // 获取winsize, 传给tty
            signal(SIGWINCH, winch_handler);
            ioctl(master_pt_fd, TIOCGWINSZ, &wbuf);
        }

        pt_name = ttyname(master_pt_fd);

        int slave_pt_fd;

        if (user_obj.type == LIST_REC) {
            list_rec();
            return 0;
        } else if (user_obj.type == USE_ID) {
            read_index();
        } else if (user_obj.type == ACCESS_SAVE) {
            if (!user_obj.name) {
                user_obj.name = user_obj.host;
            }
            save_access();
        }

        if ((newpid = fork()) == -1) {
            perror("fork");
        } else if (newpid == 0) {
            child();
        } else if (newpid < 0) {
            perror("fail ");
        }

        slave_pt_fd = open(pt_name, O_RDWR | O_NOCTTY);

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

void child() {
    //setsid();
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

    strcpy(temp1, user_obj.user);
    strcat(temp1, "@");


    strcat(temp1, user_obj.host);
    arglist[numargs++] = makestr(temp1);

    memset(temp1, 0, 2049);

    memset(temp1, 0, 2049);
    sprintf(temp2, "%d", user_obj.port);
    strcat(temp1, "-p");
    strcat(temp1, temp2);
    arglist[numargs++] = makestr(temp1);
    arglist[numargs++] = NULL;
//    for (int i = 0; i < numargs; i++) {
//        printf("%s\n", arglist[i]);
//    }

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

void list_rec() {
    FILE *file = fopen(FILE_PATH, "r+");
    if (file == NULL) {
        fprintf(stderr, "read %s fail", FILE_PATH);
    }
    char line[1024];

    printf("%s\t%s\t%s\t%s\t%s\t%s\n",
           "id",
           "name",
           "user",
           "host",
           "port",
           "password");
    int id = 1;
    while (!feof(file)) {
        fgets(line, 1024, file);
        printf("%d\t%s\n", id++, line);
    }
    fclose(file);
}

void read_index() {
    if (!user_obj.id) {
        fprintf(stderr, "Error: Unknown id\n");
    }

    FILE *file = fopen(FILE_PATH, "r+");
    if (file == NULL) {
        fprintf(stderr, "read %s fail", FILE_PATH);
        return;
    }

    int r_id = 1;
    char *r_name = NULL;
    char *r_user = NULL;
    char *r_host = NULL;
    int r_port;
    char *r_password = NULL;

    while (!feof(file)) {
        fscanf(file, "%s\t%s\t%s\t%d\t%s\n",
               r_name,
               r_user,
               r_host,
               &r_port,
               r_password
        );
        if (r_id++ == user_obj.id) {
            user_obj.name = r_name;
            user_obj.user = r_user;
            user_obj.host = r_host;
            user_obj.port = r_port;
            user_obj.password = r_password;
            fclose(file);
            return;
        }
    }

    fclose(file);
}

void save_access() {
    FILE *file = fopen(FILE_PATH, "r+w");
    if (file == NULL) {
        fprintf(stderr, "read %s fail", FILE_PATH);
        return;
    }

    char *r_name = NULL;
    char *r_user = NULL;
    char *r_host = NULL;
    int r_port;
    char *r_password = NULL;

    while (!feof(file)) {
        int scaf = fscanf(file, "%s\t%s\t%s\t%d\t%s\n",
                          r_name,
                          r_user,
                          r_host,
                          &r_port,
                          r_password);
        if (scaf == EOF) {
            return;
        }
        int bufsize = sizeof(r_name) + sizeof(r_user) + sizeof(r_host) + sizeof(r_password) + 9;
        if (user_obj.host == r_host && user_obj.port == r_port) {
            long offset;
            if (user_obj.name && user_obj.name != r_name) {
                offset = ftell(file);
                fupdate(file, offset - bufsize, user_obj.name, sizeof(user_obj.name), sizeof(r_name));
            }

            if (user_obj.password && user_obj.password != r_password) {
                offset = ftell(file);
                fupdate(file, offset - bufsize + sizeof(r_password), user_obj.password, sizeof(user_obj.password),
                        sizeof(r_password));
            }
            fclose(file);
            return;
        }
    }

    fprintf(file, "%s\t%s\t%s\t%d\t%s\n",
            user_obj.name,
            user_obj.user,
            user_obj.host,
            user_obj.port,
            user_obj.password);
    fclose(file);
}

int handleoutput(int fd) {
    static int prevmatch = 0;

    char buffer[256];
    int ret = 0;

    int numread = (int) read(fd, buffer, sizeof(buffer) - 1);
    buffer[numread] = '\0';

    if (!prevmatch) {
        write(fd, user_obj.password, strlen(user_obj.password));
        write(fd, "\n", 1);
        prevmatch = 1;
    } else {
        fprintf(stderr, "wrong password");
        ret = -1;
    }

    return ret;
}

int fupdate(FILE *file, long offset, void *buffer, int newlen, int oldlen) {
    long file_size = fsize(file);
    char *file_name;
    FILE *temp;
    if (offset > file_size || offset < 0 || newlen < 0 || oldlen < 0) {
        return -1;
    }
    if (offset == file_size) {
        fseek(file, offset, SEEK_SET);
        if (!fwrite(buffer, newlen, 1, file)) {
            return -1;
        }
    }
    if (offset < file_size) {
        temp = tmpfile();
        fcopy(temp, 0, file, 0, offset);
        fwrite(buffer, newlen, 1, temp);
        fcopy(temp, offset + newlen, file, offset + oldlen, -1);
        file_name = fname(file);
        freopen(file_name, "wb+", file);
        fcopy(file, 0, temp, 0, -1);
        fclose(temp);
    }
    return 0;
}

int finsert(FILE *file, long offset, void *buffer, int len) {
    long file_size = fsize(file);
    char *file_name;
    FILE *temp;
    if (offset > file_size || offset < 0 || len < 0) {
        return -1;
    }
    if (offset == file_size) {
        fseek(file, offset, SEEK_SET);
        if (!fwrite(buffer, len, 1, file)) {
            return -1;
        }
    }
    if (offset < file_size) {
        temp = tmpfile();
        fcopy(temp, 0, file, 0, offset);
        fwrite(buffer, len, 1, temp);
        fcopy(temp, offset + len, file, offset, -1);
        file_name = fname(file);
        freopen(file_name, "wb+", file);
        fcopy(file, 0, temp, 0, -1);
        fclose(temp);

    }
    return 0;
}

int fdel(FILE *file, long offset, int len) {
    char *file_name;
    long file_size = fsize(file);
    FILE *temp;
    if (offset > file_size || offset < 0 || len < 0) {
        return -1;
    }
    temp = tmpfile();
    fcopy(temp, 0, file, 0, offset);
    fcopy(temp, offset, file, offset + len, -1);
    file_name = fname(file);
    freopen(file_name, "wb+", file);
    fcopy(file, 0, temp, 0, -1);
    fclose(temp);
    return 0;
}

char *fname(FILE *file) {
    char fd_path[1024] = {'\0'};
    char *filename = malloc(1024);
    int fd = fileno(file);
    sprintf(fd_path, "/proc/self/fd/%d", fd);
    ssize_t r = readlink(fd_path, filename, 1023);
    if (r < 0) {
        // TODO error
        return NULL;
    }
    filename[r] = '\0';
    return filename;
}

void fcopy(FILE *fdst, long d, FILE *fsrc, long s, long offset) {
    fseek(fdst, d, SEEK_CUR);
    fseek(fsrc, s, SEEK_CUR);

    int c;
    int i = 0;
    while ((c = fgetc(fsrc)) != EOF && (i < offset || i < 0)) {
        fputc(c, fdst);
    }
}

long fsize(FILE *file) {
    if (!file) {
        return -1;
    }
    fseek(file, 0L, SEEK_END);
    return ftell(file);
}

void init_struct() {
    user_obj.user = calloc(1024, 1);
    user_obj.host = calloc(1024, 1);
    user_obj.password = calloc(1024, 1);
    user_obj.name = calloc(1024, 1);
    user_obj.user = "root";
    user_obj.port = 22;
    user_obj.type = ACCESS_SAVE;
}

void sigchild_handler(int signum) {

}

void winch_handler(int signum) {
    struct winsize wsize;
    if (ioctl(tty_fd, TIOCGWINSZ, &wsize) == 0) {
        ioctl(master_pt_fd, TIOCGWINSZ, &wsize);
    }
}