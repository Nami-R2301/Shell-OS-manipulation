#include <stdio.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>
#include <wait.h>
#include <stdlib.h>
#include <sys/poll.h>
#include <stdbool.h>

/* ********************* *
 * TP2 INF3173 H2021
 * Code permanent: REGN03079808
 * Nom: Reghbati
 * Prénom: Nami
 * ********************* */

//typedef struct pollfds_s {
//   struct pollfd *fds[3];
//} pollfds_t;

typedef struct pat_s {
    int option;
    char *delim;
    int nbrCmds;
    char **newCmd;
    int sig;
    struct pollfd fds[3];
    int stdExit[2];
    int stdOut[2];
    int stdErr[2];
    int posDelD;
    int posDelF;
} pat_t;

void readCmds(char **argv, const int *args, pat_t *pat);
void execCmds(pat_t *pat);
void pPoll(pat_t *pat);
int forking(pat_t *pat, int, char **);
void polling(pat_t *);
void printExit(int retour, pat_t *pat);
void exitMain(pat_t *, char *);
void exitStd(pat_t *, int);

int main(int argc, char *argv[]) {

    if (argc < 2 || (argc == 2 && strcmp(argv[1], "-s") == 0) || (argc == 3 && strcmp(argv[1], "-s") == 0)) {
        fprintf(stderr, "Erreur! : Mauvais argument(s) et/ou commande(s)\n");
        return 1;
    }
    pat_t *pat = malloc(sizeof(struct pat_s));
    if(!pat) exitMain(pat, NULL);
    *pat = (struct pat_s) {0, "+", 0, NULL, 0, {0}, {0}, {0}, {0}, 0, 0};
    if (strcmp(argv[1], "-s") == 0 && strlen(argv[2]) >= 1) {
        pat->delim = argv[2];
        pat->option = 2;
    }
    pPoll(pat);
    int retour = 1;
    pid_t pid = fork();
    if(pid == -1) exitMain(pat, NULL);
    if(pid == 0) {
        exitStd(pat, 0);
        retour = forking(pat, argc, argv);
    }
    else {
        exitStd(pat, 1);
        polling(pat);
        if(waitpid(0,&pid,0) != -1) {
            fflush(stdout);
            if(WIFEXITED(pid)) retour = WEXITSTATUS(pid);
            if(WIFSIGNALED(pid)) retour += WTERMSIG(pid);
            exitStd(pat, 0);
        }
    }
    for(int i = 0; i < pat->nbrCmds; ++i) free(pat->newCmd[i]);
    free(pat->newCmd);
    free(pat);
    return retour;
}

void readCmds(char **argv, const int *args, pat_t *pat) {

    pat->newCmd = calloc(*args, strlen(argv[0]) * 255);
    if(!pat->newCmd) exitMain(pat, NULL);

    for (int i = 0; i + pat->posDelD < *args; ++i) {
        if (strcmp(argv[i + pat->posDelD], pat->delim) != 0) {
            pat->newCmd[i] = argv[i + pat->posDelD];
            pat->posDelF++;
        } else break;
    }

}

int forking(pat_t *pat, int argc, char **argv) {

    int retour = 0;
    int nbrCmds = pat->nbrCmds;
    pid_t neveu;
    for (int i = 1 + pat->option; i < argc; ++i) {
        pat->posDelF = 0;
        pat->posDelD = i;
        readCmds(argv, &argc, pat);
        i += pat->posDelF;
        neveu = fork();
        if(neveu == -1) exitMain(pat, NULL);
        if (neveu == 0) execCmds(pat);
    }
    while(waitpid(-1, &neveu, 0) != -1) {
        if (WIFSIGNALED(neveu)) {
            pat->sig = WTERMSIG(neveu);
            retour += 128 + pat->sig;
        }else if(WIFEXITED(neveu)) retour += WEXITSTATUS(neveu);
        printExit(WEXITSTATUS(neveu), pat);
        pat->sig = 0;
        //nbrCmds--;
        //if(nbrCmds == 0) {
    }
    exitStd(pat, 1);
    return retour;
}

void execCmds(pat_t *pat) {

    if(dup2(pat->stdErr[1], STDERR_FILENO) == -1 || dup2(pat->stdOut[1], STDOUT_FILENO) == -1) {
        exitMain(pat, NULL);
    }
    execvp(pat->newCmd[0], pat->newCmd);
    perror(pat->newCmd[0]);
    free(pat->newCmd);
    free(pat);
    _exit(127);
}

void printExit(int retour, pat_t *pat) {

    char *exit = malloc(strlen(pat->delim) + 12);
    if(!exit) exitMain(pat, exit);

    if(pat->sig == 0) sprintf(exit, "status=%d\n", retour);
    else sprintf(exit,"signal=%d\n", pat->sig);

    if(write(pat->stdExit[1], exit, strlen(exit)) == -1) exitMain(pat, NULL);
    free(exit);
}

void polling(pat_t *pat) {

    int pollStat = 1;
    size_t size = 0;
    bool flagO = false;
    bool flagE = false;
    char *sep = calloc(4, sizeof(pat->delim));
    if(!sep) exitMain(pat, sep);
    snprintf(sep, sizeof(sep),"%s%s%s", pat->delim, pat->delim, pat->delim);

    while(pollStat >= 1) {
        pollStat = poll(pat->fds, 3, -1);

        char buf[4096] = {0};
        if (pat->fds[1].revents & POLLIN) {
            if(fflush(stdout) != 0) exitMain(pat, sep);
            flagE = false;
            size = read(pat->stdOut[0], buf, sizeof(buf));
            if(size > 0) {
                if(!flagO) printf("%s stdout\n", sep);
                printf("%s", buf);
                if(fflush(stdout) != 0) exitMain(pat, sep);
            }
            if (size > 0 && buf[size - 1] != '\n')
                snprintf(sep, sizeof(sep), "\n%s%s%s%s", pat->delim, pat->delim, pat->delim, pat->delim);
            else snprintf(sep, sizeof(sep), "%s%s%s", pat->delim, pat->delim, pat->delim);
            flagO = true;
        }
        if (pat->fds[2].revents & POLLIN) {
            flagO = false;
            size = read(pat->stdErr[0], buf, sizeof(buf));
            if(size > 0) {
                if(!flagE) printf("%s stderr\n", sep);
                printf("%s", buf);
                if(fflush(stdout) != 0) exitMain(pat, sep);
            }
            if (size > 0 && buf[size - 1] != '\n')
                snprintf(sep, sizeof(sep), "\n%s%s%s%s", pat->delim, pat->delim, pat->delim, pat->delim);
            else snprintf(sep, sizeof(sep), "%s%s%s", pat->delim, pat->delim, pat->delim);
            flagE = true;
        }
        if (pat->fds[0].revents & POLLIN) {
            if(fflush(stdout) != 0) exitMain(pat, sep);
            size = read(pat->stdExit[0], buf, sizeof(buf));
            if(size > 0) printf("%s exit, %s", sep, buf);
            if (size > 0 && buf[size - 1] != '\n')
                snprintf(sep, sizeof(sep), "\n%s%s%s%s", pat->delim, pat->delim, pat->delim, pat->delim);
            else snprintf(sep, sizeof(sep), "%s%s%s", pat->delim, pat->delim, pat->delim);
        }
        if(size == 0) break;
        size = 0;
    }
    free(sep);
    if(pollStat == -1) exitMain(pat, sep);
}

void pPoll(pat_t *pat) {

    if(pipe(pat->stdExit) == -1 || pipe(pat->stdOut) == -1 || pipe(pat->stdErr) == -1) {
        exitMain(pat, NULL);
    }
    pat->fds[0].fd = pat->stdExit[0];
    pat->fds[0].events = POLLIN;
    pat->fds[1].fd = pat->stdOut[0];
    pat->fds[1].events = POLLIN;
    pat->fds[2].fd = pat->stdErr[0];
    pat->fds[2].events = POLLIN;
}

void exitStd(pat_t *pat, int i) {

    if(i == 1) {
        if(close(pat->stdExit[1]) == -1 || close(pat->stdOut[1]) == -1 || close(pat->stdErr[1]) == -1) {
            exitMain(pat, NULL);
        }
    } else {
        if(close(pat->stdExit[0]) == -1 || close(pat->stdOut[0]) == -1 || close(pat->stdErr[0]) == -1) {
            exitMain(pat, NULL);
        }
    }
}

void exitMain(pat_t *pat, char *heapV) {

    if(pat->newCmd) {
        for (int i = 0; i < pat->nbrCmds; ++i) free(pat->newCmd[i]);
        free(pat->newCmd);
    }
    if(heapV) free(heapV);
    free(pat);
    perror("Erreur fatale!");
    _exit(1);
}
