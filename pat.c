#include <stdio.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>
#include <wait.h>
#include <stdlib.h>
#include <sys/poll.h>

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
    int sig;
    int nbrArgs;
    struct pollfd fds[4];
    int stdExit[2];
    int stdOut[2];
    int stdErr[2];
    int stdNum[2];
} pat_t;

void cmds(char **, const int *, pat_t *);
void execCmds(pat_t *pat);
void pPoll(pat_t *pat);
int forking(pat_t *pat, int, char **argv);
void polling(pat_t *);
void printExit(int retour, pat_t *pat);
void exitStd(pat_t *);

int main(int argc, char *argv[]) {

    if (argc < 2 || (argc == 2 && strcmp(argv[1], "-s") == 0) || (argc == 3 && strcmp(argv[1], "-s") == 0)) {
        fprintf(stderr, "Erreur! : Mauvais argument(s) et/ou commande(s)\n");
        return 1;
    }
    pat_t *pat = malloc(sizeof(struct pat_s));
    *pat = (struct pat_s) {0, "+", 0, 0, 0, 0, 0};
    *pat->fds = (struct pollfd) {0};
    if (strcmp(argv[1], "-s") == 0 && strlen(argv[2]) >= 1) {
        pat->delim = argv[2];
        pat->option = 2;
    }
    if(argc - pat->option > 2) pat->nbrArgs = argc;
    int retour;
    retour = forking(pat, argc, argv);
    free(pat);
    return retour;
}

void cmds(char **argv, const int *args, pat_t *pat) {

    pat->newCmd = realloc(pat->newCmd, strlen(argv[0]) * *args);
    if(!pat->newCmd) {
        perror("Erreur de mémoire! (malloc/calloc/realloc)");
        _exit(1);
    }
    for (int i = 0; i + pat->posDelD < *args; ++i) {
        if (strcmp(argv[i + pat->posDelD], pat->delim) != 0) {
            pat->newCmd[i] = argv[i + pat->posDelD];
            pat->posDelF++;
        } else {
            pat->newCmd[i] = (char*)NULL;
            break;
        }
    }
}

int forking(pat_t *pat, const int argc, char **argv) {

    int retour = 0;
    pPoll(pat);
    pid_t pid = fork();

    if (pid == -1) { perror("Erreur fatale survenu!"); retour = 1; }
    else if (pid > 0) {
        polling(pat);
        if(waitpid(0,&pid,0) != -1) {
            exitStd(pat);
            retour = WEXITSTATUS(pid);
        }
    } else if(pid == 0) {
        int parent = getpid();
        for (int i = 1 + pat->option; i < argc; ++i) {
            pat->posDelF = 0;
            pat->posDelD = i;
            cmds(argv, &argc, pat);
            i += pat->posDelF;
            pid_t neveu = fork();
            if(neveu == 0) execCmds(pat);
            if (pat->nbrArgs > 0) {
                char *buf = calloc(1, sizeof(int));
                sprintf(buf, "%d", neveu - parent);
                if (write(pat->stdNum[1], buf, sizeof(buf)) == -1) {
                    free(buf);
                    _exit(1);
                }
            }
        }
        while(waitpid(-1,&pid,0) != -1) {
            if (WIFSIGNALED(pid)) {
                pat->sig = WTERMSIG(pid);
                retour += 128 + pat->sig;
            }  else if(WIFEXITED(pid)) retour += WEXITSTATUS(pid);
            printExit(WEXITSTATUS(pid), pat);
            pat->sig = 0;
        }
        exitStd(pat);
        return retour;
    } else retour = -3;
    return retour;
}

void execCmds(pat_t *pat) {

    if (pat->newCmd[0]) {
        dup2(pat->stdErr[1], STDERR_FILENO);
        dup2(pat->stdOut[1], STDOUT_FILENO);
        execvp(pat->newCmd[0], pat->newCmd);
        perror(pat->newCmd[0]);
        free(pat->newCmd);
        free(pat);
        _exit(127);
    }
}

void printExit(int retour, pat_t *pat) {

    char *exit = malloc(strlen(pat->delim) + 12);
    if (pat->sig == 0) sprintf(exit, "status=%d\n", retour);
    else {
        sprintf(exit,"signal=%d\n", pat->sig);
        free(pat->newCmd);
    }
    write(pat->stdExit[1], exit, strlen(exit));
    free(exit);
}


void polling(pat_t *pat) {

    close(pat->stdNum[1]);
    size_t size = 0;
    char *std;
    struct pollfd fd;
    char *sep = calloc(4, sizeof(pat->delim));
    printf("%d\n", getpid());
    snprintf(sep, sizeof(sep),"%s%s%s", pat->delim, pat->delim, pat->delim);
    while(poll(pat->fds, 4, -1) > 0) {

        char buf[4096] = {0};
        int numCmd;
        if(pat->fds[3].revents & POLLIN) {
            size = read(pat->stdNum[0], buf, sizeof(buf));
            if(size > 0) numCmd = (int) strtol(buf,NULL,0);
        }
        for(int i = 1; i < 4; ++i) {
            if(i == 1) { std = "stdout"; fd = pat->fds[1]; }
            else if(i == 2) { std = "stderr"; fd = pat->fds[2]; }
            else { std = "exit"; fd = pat->fds[0]; }

            if (fd.revents & POLLIN) {
                if (pat->nbrArgs > 0) {
                    if(fd.fd != pat->stdExit[0]) printf("%s %s %d\n", sep, std, numCmd);
                    else printf("%s %s %d, ", sep, std, numCmd);
                }
                else printf("%s %s\n", sep, std);
                size = read(fd.fd, buf, sizeof(buf));
                if (size > 0 && buf[size - 1] != '\n')
                    snprintf(sep, sizeof(sep), "%s%s%s%s", pat->delim, pat->delim, pat->delim, pat->delim);
                else snprintf(sep, sizeof(sep), "%s%s%s", pat->delim, pat->delim, pat->delim);
                if (size > 0 && buf[size - 1] != '\n') printf("%s\n", buf);
                else if (size > 0) printf("%s", buf);
                fflush(stdout);
            }
        }
        if(size == 0) break;
        size = 0;
    }
    free(sep);
}



void pPoll(pat_t *pat) {

    pipe(pat->stdExit);
    pipe(pat->stdOut);
    pipe(pat->stdErr);
    pipe(pat->stdNum);
    pat->fds[0].fd = pat->stdExit[0];
    pat->fds[0].events = POLLIN;
    pat->fds[1].fd = pat->stdOut[0];
    pat->fds[1].events = POLLIN;
    pat->fds[2].fd = pat->stdErr[0];
    pat->fds[2].events = POLLIN;
    pat->fds[3].fd = pat->stdNum[0];
    pat->fds[3].events = POLLIN;
}

void exitStd(pat_t *pat) {

    close(pat->stdExit[1]);
    close(pat->stdExit[0]);
    close(pat->stdOut[1]);
    close(pat->stdOut[0]);
    close(pat->stdErr[1]);
    close(pat->stdErr[0]);
    close(pat->stdNum[1]);
    close(pat->stdNum[0]);
}
