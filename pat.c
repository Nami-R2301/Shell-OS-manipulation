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
    struct pollfd fds[3];
    int stdO[2];
    int stdE[2];
    int sig;
    char *print;
} pat_t;

void cmds(char **, const int *, pat_t *, int);
void execCmds(pat_t *pat);
void pPoll(pat_t *pat);
int monitorStd(pat_t *, int, char **);
void printStat(int retour, pat_t *pat);
void exitStd(pat_t *);

int main(int argc, char *argv[]) {

    if (argc < 2 || (argc == 2 && strcmp(argv[1], "-s") == 0) || (argc == 3 && strcmp(argv[1], "-s") == 0)) {
        fprintf(stderr, "Erreur! : Mauvais argument(s) et/ou commande(s)\n");
        return 1;
    }
    pat_t *pat = malloc(sizeof(struct pat_s));
    *pat = (struct pat_s) {0, "+", 0, 0, 0, {0}, {0}, {0}, 0, NULL};
    if (strcmp(argv[1], "-s") == 0 && strlen(argv[2]) >= 1) {
        pat->delim = argv[2];
        pat->option = 2;
    }
    int retour;
    retour = monitorStd(pat, argc, argv);
    free(pat);
    return retour;
}

void cmds(char **argv, const int *args, pat_t *pat, int numCmd) {

    pat->newCmd = realloc(pat->newCmd, strlen(argv[0]) * *args);
    if(!pat->newCmd) {
        perror("Erreur de mémoire! (malloc/calloc/realloc)");
        _exit(1);
    }
    for (int i = 0; i < *args; ++i) {
        if (strcmp(argv[i + numCmd], pat->delim) != 0) {
            pat->newCmd[i] = argv[i + numCmd];
            pat->posDelF++;
        } else {
            pat->newCmd[i] = (char*)NULL;
            break;
        }
    }
}

void execCmds(pat_t *pat) {

    if (pat->newCmd[0]) {
        dup2(pat->stdE[1], STDERR_FILENO);
        dup2(pat->stdO[1], STDOUT_FILENO);
        exitStd(pat);
        execvp(pat->newCmd[0], pat->newCmd);
        perror(pat->newCmd[0]);
        free(pat->newCmd);
        free(pat);
        _exit(127);
    }
}

int monitorStd(pat_t *pat, const int argc, char **argv) {

    char buf[4096];
    size_t size;
    pPoll(pat);
    pid_t pid = fork();
    if (pid == -1) {
        perror("Erreur fatale survenu!");
        return 1;
    }
    if (pid > 0) {
        while (poll(pat->fds, 2, 500) > 0) {
            if (pat->fds[0].revents & POLLIN) {
                printf("%s%s%s stdout\n", pat->delim, pat->delim, pat->delim);
                size = read(pat->stdO[0], buf, sizeof(buf));
            }
            if (pat->fds[1].revents & POLLIN) {
                printf("%s%s%s stderr\n", pat->delim, pat->delim, pat->delim);
                size = read(pat->stdE[0], buf, sizeof(buf));
            }
            pat->print = calloc(1, size + 1);
            strncpy(pat->print, buf, size);
            printf("%s", pat->print);
            free(pat->print);
        }
        exitStd(pat);
        if(waitpid(0,&pid,0) != -1) return WEXITSTATUS(pid);
    } else {
        int retour = 0;
        for (int i = 1 + pat->option; i < argc; ++i) {
            pat->posDelD = i;
            cmds(argv, &argc, pat, pat->posDelD);
            printf("Commande #%d = %s\n",i, pat->newCmd[0]);
            pid = fork();
            if(pid == 0) execCmds(pat);
            i += pat->posDelF;
        }
        while(waitpid(-1,&pid,0) != -1) {
            if (WIFSIGNALED(pid)) {
                pat->sig = 128 + WTERMSIG(pid);
                retour += pat->sig;
            }  else if(WIFEXITED(pid)) retour += WEXITSTATUS(pid);
            printStat(WEXITSTATUS(pid), pat);
            pat->sig = 0;
        }
        close(pat->stdO[1]);
        close(pat->stdE[1]);
        return retour;
    }
    return -3;
}

void printStat(int retour, pat_t *pat) {

    dup2(1, pat->stdO[1]);
    printf("%s%s%s exit, ", pat->delim, pat->delim, pat->delim);
    if (pat->sig == 0) printf("status=%d\n", retour);
    else {
        printf("signal=%d\n", pat->sig);
        free(pat->newCmd);
    }
}

void pPoll(pat_t *pat) {

    pipe(pat->stdO);
    pipe(pat->stdE);
    pat->fds[0].fd = pat->stdO[0];
    pat->fds[0].events = POLLIN;
    pat->fds[1].fd = pat->stdE[0];
    pat->fds[1].events = POLLIN;
}

void exitStd(pat_t *pat) {

    close(pat->stdO[0]);
    close(pat->stdE[0]);
    close(pat->stdO[1]);
    close(pat->stdE[1]);
}
