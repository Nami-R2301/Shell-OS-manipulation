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

//TODO
//typedef struct pollfds_s {
//   struct pollfd *fds[3];
//} pollfds_t;

typedef struct pat_s {
    int option; //Option -s.
    char *delim; //Délimiteur de commandes.
    int nbrCmds; //Nombre de commandes.
    char **newCmd; //Un pseudo "argv[]" pour execvp.
    int sig; //signal (SIGACT) recu lors de l'exécution d'une commande.
    struct pollfd fds[3]; //Tubes utilisé pour communiquer avec "poll" (stdout, stderr, exit).
    int stdExit[2]; //Tube personalisé pour envoyer le status/signal au parent sans passer par stdout.
    int stdOut[2]; //Tube pour envoyer l'affichage de la sortie standard au parent.
    int stdErr[2]; //Tube pour envoyer l'affichage de la sortie standard d'erreur au parent.
    int posDelD; //Position de début de la commande évaluée.
    int posDelF; //Position de fin de la commande évaluée.
} pat_t;

//Sert a lire les commandes pour l'exécution de celles-ci dans l'enfant.
void readCmds(char **argv, const int *args, pat_t *pat);

//Sert a exécuter les commandes avec execvp.
void execCmds(pat_t *pat);

//Sert a préparer les tubes et les descripteurs de fichiers pour la communication entre "poll" et l'enfant.
void pPoll(pat_t *pat);

//Transmet les données affichées par les sorties des commandes évaluées au parent en parallele.
int forking(pat_t *pat, int, char **);

//Aquiesce et gere les événements et données transmises par l'enfant en parallele.
void polling(pat_t *);

//Récupere les codes de retour des commandes ainsi que les signaux puis les transmets au parent via stdExit[1].
void printExit(int retour, pat_t *pat);

//Sert a.
void exitMain(pat_t *, char *);

//Aquiesce les données affichées par les sorties des commandes évaluées et les transmet au parent en parallele.
void exitStd(pat_t *, int);

int main(int argc, char *argv[]) {

    if (argc < 2 || (argc == 2 && strcmp(argv[1], "-s") == 0) || (argc == 3 && strcmp(argv[1], "-s") == 0)) {
        fprintf(stderr, "Erreur! : Mauvais argument(s) et/ou commande(s)\n");
        return 1;
    }
    pat_t *pat = malloc(sizeof(struct pat_s));
    if(!pat) exitMain(NULL, NULL);
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
    free(pat->newCmd);
    free(pat);
    return retour;
}

void readCmds(char **argv, const int *args, pat_t *pat) {

    if(pat->newCmd) free(pat->newCmd); //Libéré l'ancienne commande avant de procédé à la prochaine.
    pat->newCmd = calloc(*args - (pat->option + 1), FILENAME_MAX); //Pour maximum robustesse.
    if(!pat->newCmd) exitMain(pat, NULL);

    for (int i = 0; i + pat->posDelD < *args; ++i) {
        if (strcmp(argv[i + pat->posDelD], pat->delim) != 0) {
            pat->newCmd[i] = (char *) {""}; //Pour calmer Valgrind.
            pat->newCmd[i] = argv[i + pat->posDelD];
            pat->posDelF++;
        } else break; //Fin de "argv[]".
    }

}

int forking(pat_t *pat, int argc, char **argv) {

    int retour = 0;
    //int nbrCmds = pat->nbrCmds;
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
            if(fflush(stdout) != 0) exitMain(pat, sep);
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

    if(pat->newCmd) free(pat->newCmd);
    if(heapV) free(heapV);
    if(pat) free(pat);
    perror("Erreur fatale!");
    _exit(1);
}
