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
    int stdO[2];
    int stdE[2];
    char *print;
} pat_t;

void sigAction(int);
void cmds(char **, const int *, pat_t *);
int printStd(pat_t *pat);


int main(int argc, char *argv[]) {

    if (argc < 2 || (argc == 2 && strcmp(argv[1], "-s") == 0) || (argc == 3 && strcmp(argv[1], "-s") == 0)) {
        fprintf(stderr, "Erreur! : Mauvais argument(s) et/ou commande(s)\n");
        return 1;
    }
    pat_t *pat = malloc(sizeof(struct pat_s));
    *pat = (struct pat_s) {0, "+", 0, 0, NULL,{0}, {0}, NULL};
    if (strcmp(argv[1], "-s") == 0 && strlen(argv[2]) >= 1) {
        pat->delim = argv[2];
        pat->option = 1;
    }
    struct sigaction signal;
    if (sigemptyset(&signal.sa_mask) == -1) {
        perror("Erreur fatale!");
        free(pat);
        return 1;
    }
    signal.sa_flags = 0;
    signal.sa_handler = sigAction;
    int std[2];
    int retour = 0;
    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], pat->delim) != 0 && ((pat->option == 1 && i != 1) || pat->option == 0)) {
            pat->posDelD = i;
            cmds(argv, &argc, pat);
            if(pipe(std) == -1) return -1;
            retour = printStd(pat);
            if(retour < 0) { fprintf(stderr, "Erreur fatale survenue!\n"); return 1; }
            printf("%s%s%s exit, status=%d\n", pat->delim, pat->delim, pat->delim, retour);
            i += pat->posDelF;
            free(pat->newCmd);
        }
    }
    free(pat);
    return retour;
}

void sigAction(int sig) { printf("signal=%d\n", sig); }

void cmds(char **argv, const int *args, pat_t *pat) {

    pat->newCmd = calloc(*args, strlen(argv[0]));
    for (int i = 0; i + pat->posDelD < *args; ++i) {
        if (strcmp(argv[pat->posDelD + i], pat->delim) != 0) {
            pat->newCmd[i] = argv[pat->posDelD + i];
            pat->posDelF++;
        } else break;
    }
}

int printStd(pat_t *pat) {

    char buf[4096];
    size_t size;
    pipe(pat->stdO); pipe(pat->stdE);
    struct pollfd pfds[2];
    pfds[0].fd = pat->stdO[0];
    pfds[0].events = POLLIN;
    pfds[1].fd = pat->stdE[0];
    pfds[1].events = POLLIN;
    pid_t pid = fork();

    if (pid == -1) {
        perror("Erreur fatale!");
        return -1;
    }
    if (pid == 0 && pat->newCmd[0]) {
        dup2(pat->stdE[1], STDERR_FILENO); dup2(pat->stdO[1], STDOUT_FILENO);
        close(pat->stdE[0]); close(pat->stdE[1]); close(pat->stdE[0]); close(pat->stdE[1]);
        execvp(pat->newCmd[0], pat->newCmd);
        perror(pat->newCmd[0]);
        free(pat->newCmd);
        free(pat);
        _exit(127);
    } else if(pid > 0) {
        close(pat->stdO[1]); close(pat->stdE[1]);
        while (1) {
            size_t i = poll(pfds, 2, 1000);
            if (i > 0) {
                if(!(pfds[0].revents & POLLIN) && !(pfds[1].revents & POLLIN)) {
                    waitpid(0, &pid, 0);
                    return WEXITSTATUS(pid);
                }
                if (pfds[0].revents & POLLIN) {
                    printf("%s%s%s stdout\n", pat->delim, pat->delim, pat->delim);
                    size = read(pat->stdO[0], buf, sizeof(buf));
                } else if (pfds[1].revents & POLLIN) {
                    printf("%s%s%s stderr\n", pat->delim, pat->delim, pat->delim);
                    size = read(pat->stdE[0], buf, sizeof(buf));
                }
                pat->print = calloc(1, size + 1);
                strncpy(pat->print, buf, size);
                printf("%s", pat->print);
                free(pat->print);
            }
        }
    }
    return -2;
}