#include <stdio.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>
#include <wait.h>
#include <stdlib.h>
//#include <sys/poll.h>

/* ********************* *
 * TP2 INF3173 H2021
 * Code permanent: REGN03079808
 * Nom: Reghbati
 * Prénom: Nami
 * ********************* */

typedef struct pat_s {
    int option;
    char *delim;
    int posDelD;
    int posDelF;
    char **newCmd;
} pat_t;

void sigAction(int);
char **cmds(char **, const int *, pat_t *);
//int fprintStat(char *, char *);

int main(int argc, char *argv[]) {

    if(argc < 2 || (argc == 2 && strcmp(argv[1], "-s") == 0) || (argc == 3 && strcmp(argv[1], "-s") == 0)) {
        fprintf(stderr,"Erreur! : Mauvais argument(s) et/ou commande(s)\n");
        return 1;
    }
    pat_t *pat = malloc(sizeof(struct pat_s));
    *pat = (struct pat_s) {0,"+",0,0,NULL};
    if(strcmp(argv[1], "-s") == 0 && strlen(argv[2]) == 1) {
        pat->delim = argv[2];
        pat->option = 1;
    }
    struct sigaction signal;
    if(sigemptyset(&signal.sa_mask) == -1) { perror("Erreur fatale!"); return 1; }
    signal.sa_flags = 0;
    signal.sa_handler = sigAction;
    for(int i = 1; i < argc; ++i) {
        if(strcmp(argv[i],pat->delim) != 0) {
            pat->posDelD = i;
            pat->newCmd = cmds(argv, &argc, pat);

            pid_t pid = fork();
            if (pid == -1) { perror("Erreur fatale!"); return 1; }
            if (pid == 0) {
                if (pat->newCmd[0]) {
                    execvp(pat->newCmd[0], pat->newCmd);
                    perror("");
                    free(pat->newCmd);
                    free(pat);
                    _exit(127);
                }
            } else {
                if (waitpid(0, &pid, 0) != -1) {
                    if (WIFEXITED(pid)) {
                        printf("+++ exit, status=%d\n", WEXITSTATUS(pid));
                        free(pat->newCmd);
                    }
                }
            }
            i += pat->posDelF;
        }
    }
    free(pat);
    return 0;
}

void sigAction(int sig) { printf("signal=%d\n", sig); }

char **cmds(char **argv, const int *args, pat_t *pat) {

    char **cmd = calloc(*args, strlen(argv[0]));
    for (int i = 0; i + pat->posDelD < *args; ++i) {
        if (strcmp(argv[pat->posDelD + i], pat->delim) != 0) {
            cmd[i] = argv[pat->posDelD + i];
            pat->posDelF++;
        } else break;
    }
    return cmd;
}

//int fprintStat(char *cmd, char *delim) {

//execvp(cmds, )
//    return 0;
//}

//    struct pollfd fds[2];
//    int ret;
/* watch stdin for input */
//    fds[0].fd = STDIN_FILENO;
//    fds[0].events = POLLIN;

/* watch stdout for ability to write */
//    fds[1].fd = STDOUT_FILENO;
//    fds[1].events = POLLOUT;

//    ret = poll(fds, 2, 2 * 1000);
//    if (ret == -1) {
//        perror ("poll");
//        return 1;
//   }
//    if (!ret) {
//        printf ("%d seconds elapsed.\n", 2);
//        return 0;
//    }
//    if (fds[0].revents & POLLIN)printf ("stdin is readable\n");
//    if (fds[1].revents & POLLOUT) printf ("stdout is writable\n");