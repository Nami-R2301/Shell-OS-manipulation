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

typedef struct pollfds_s {
    int numCmd;
    int wOpipes[2];
    int wEpipes[2];
    int wexpipes[2];
    struct pollfd fdsChild[3];
} pollfds_t;

typedef struct pat_s {
    int option; //Option -s.
    char *delim; //Délimiteur de commandes.
    int nbrCmds; //Nombre de commandes.
    char **newCmd; //Un pseudo "argv[]" pour execvp.
    int sig; //signal (SIGACT) recu lors de l'exécution d'une commande.
    int posDelD; //Position de début de la commande évaluée.
    int posDelF; //Position de fin de la commande évaluée.
    pollfds_t **pipes;
    int stdOut[2];
    int stdErr[2];
    struct pollfd *fdsChild; //Tubes utilisé pour communiquer avec "poll" (stdout, stderr, exit).
    struct pollfd fdsMain[2];
} pat_t;

void countCmds(pat_t *, char **, int);

//Sert a lire les commandes pour l'exécution de celles-ci dans l'enfant.
void readCmds(char **, const int *, pat_t *);

//Sert a exécuter les commandes avec execvp.
void execCmds(pat_t *, int);

//Sert a préparer les tubes et les descripteurs de fichiers pour la communication entre "poll" et l'enfant.
void pPoll(pat_t *);

//Transmet les données affichées par les sorties des commandes évaluées au parent en parallele.
int forking(pat_t *, int, char **);

//Aquiesce et gere les événements et données transmises par l'enfant en parallele.
void pollChild(pat_t *, int i);

void pollMain(pat_t *);

//Récupere les codes de retour des commandes ainsi que les signaux puis les transmets au parent via stdExit[1].
void printExit(int , pat_t *, int);

//Sert a généraliser les étapes de fermeture du programme en cas d'erreur (exit(1)).
void exitMain(pat_t *, char *);

//Sert a généraliser les étapes de fermeture des tubes dépendemment du thread appelant (parent/enfant/neveu).
void exitStd(pat_t *, int);

int main(int argc, char *argv[]) {

    if (argc < 2 || (argc == 2 && strcmp(argv[1], "-s") == 0) || (argc == 3 && strcmp(argv[1], "-s") == 0)) {
        fprintf(stderr, "Erreur! : Mauvais argument(s) et/ou commande(s)\n");
        return 1;
    }
    pat_t *pat = malloc(sizeof(struct pat_s));
    if(!pat) exitMain(NULL, NULL);
    *pat = (struct pat_s) {0, "+", 0, NULL, 0, 0, 0, NULL, {0}, {0}, NULL, {0}};
    if (strcmp(argv[1], "-s") == 0 && strlen(argv[2]) >= 1) {
        pat->delim = argv[2];
        pat->option = 2;
    }
    countCmds(pat, argv, argc);
    if(pat->nbrCmds == 0) {
        fprintf(stderr, "Erreur! : Mauvais argument(s) et/ou commande(s)\n");
        return 1;
    }
    pat->fdsChild = calloc(pat->nbrCmds, sizeof(struct pollfd) * 3);
    pPoll(pat);
    int retour = 0; //Code de retour de "pat".
    pid_t pid = fork();
    if(pid == -1) exitMain(pat, NULL);
    if(pid > 0) {
        exitStd(pat, 1); //Fermeture des tubes "write (stdout)" du parent.
        pid_t enfant = fork();
        pid_t neveu;
        if(enfant == 0) {
            dup2(pat->stdOut[1], STDOUT_FILENO);
            dup2(pat->stdErr[1], STDERR_FILENO);
            close(pat->stdOut[1]);
            close(pat->stdErr[1]);
            for (int i = 0; i < pat->nbrCmds; ++i) {
                neveu = fork();
                if(neveu == 0) pollChild(pat, i); //Attentes des événements de l'enfant.
            }
            if (fflush(stdout) != 0) exitMain(pat, NULL);
            exitStd(pat, 0); //Fermeture des tubes "read (stdin)" du parent.
            close(pat->stdOut[0]);
            close(pat->stdErr[0]);
            close(STDOUT_FILENO);
            close(STDERR_FILENO);
        } else if(enfant > 0) {
            close(pat->stdOut[1]);
            close(pat->stdErr[1]);
            pollMain(pat);
            while (waitpid(pid, &pid, 0) != -1) {
                if (fflush(stdout) != 0) exitMain(pat, NULL);
                if (WIFEXITED(pid)) retour = WEXITSTATUS(pid);
                if (WIFSIGNALED(pid)) retour += WTERMSIG(pid);
            }
            exitStd(pat, 0); //Fermeture des tubes "read (stdin)" du parent.
        }
    } else if(pid == 0) {
        exitStd(pat, 0); //Fermeture des tubes "read (stdin)" de l'enfant.
        retour = forking(pat, argc, argv); //Somme des codes de retour des commandes.
        if(retour != -1) exitStd(pat, 1); //Fermeture des tubes "write (stdout)" de l'enfant.
    }
    free(pat->fdsChild);
    for(int i = 0; i < pat->nbrCmds; ++i) free(pat->pipes[i]);
    free(pat->pipes);
    free(pat->newCmd);
    free(pat);
    return retour;
}

void countCmds(pat_t *pat, char **argv, const int argc) {

    bool arg = false;
    pat->pipes = calloc(argc - 1, sizeof(pollfds_t));
    for(int i = pat->option + 1; i  < argc; ++i) {
        if(strcmp(argv[i], pat->delim) != 0) {
            if(!arg) {
                pat->pipes[pat->nbrCmds] = malloc(sizeof(pollfds_t));
                pat->pipes[pat->nbrCmds]->numCmd = pat->nbrCmds + 1;
                pipe(pat->pipes[pat->nbrCmds]->wexpipes);
                pipe(pat->pipes[pat->nbrCmds]->wOpipes);
                pipe(pat->pipes[pat->nbrCmds]->wEpipes);
                pat->nbrCmds++;
            }
            arg = true;
        } else arg = false;
    }
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

    int retour = 0; //Somme des codes de retour des commandes.
    int nbrCmds = 0;
    int num;
    siginfo_t info = {0};
    pid_t neveu[pat->nbrCmds];
    for (int i = 1 + pat->option; i < argc; ++i) {
        pat->posDelF = 0;
        pat->posDelD = i; //Indice de début de commande.
        readCmds(argv, &argc, pat);
        i += pat->posDelF; //Indice de fin de commande.
        neveu[nbrCmds] = fork();
        if (neveu[nbrCmds] == -1) exitMain(pat, NULL);
        if (neveu[nbrCmds] == 0) execCmds(pat, nbrCmds);
        nbrCmds++;
    }
    while(waitid(P_ALL, 0, &info, WEXITED | WSTOPPED) != -1) { //Parent des neveux (Enfant) attend que tous les neveux terminent.
        for(int i = 0; i < nbrCmds; ++i) {
            if(neveu[i] == info.si_pid) {
                num = i;
                if (info.si_code == CLD_KILLED || info.si_code == CLD_STOPPED) {
                    pat->sig = info.si_status;
                    retour += 128 + pat->sig;
                } else if (info.si_code == CLD_EXITED) retour += info.si_status;
                printExit(info.si_status, pat, num); //Envoie vers "pat->stdExit[1]".
                pat->sig = 0;
            }
        }
    }
    return retour;
}

void execCmds(pat_t *pat, int i) {

    if(dup2(pat->pipes[i]->wEpipes[1], STDERR_FILENO) == -1 || dup2(pat->pipes[i]->wOpipes[1], STDOUT_FILENO) == -1) {
        exitMain(pat, NULL);
    }
    exitStd(pat, 1);
    execvp(pat->newCmd[0], pat->newCmd);
    perror(pat->newCmd[0]);
    free(pat->newCmd);
    free(pat);
    _exit(127);
}

void printExit(int retour, pat_t *pat, int numCmd) {

    char *exit = calloc(1,12);
    if(!exit) exitMain(pat, exit);

    if(pat->sig == 0) sprintf(exit, "status=%d\n", retour); //Fin normale de commande.
    else sprintf(exit,"signal=%d\n", pat->sig); //Fin abrupte (interruption).
    int id = pat->pipes[numCmd]->wexpipes[1];

    if(write(id, exit, strlen(exit)) == -1) exitMain(pat, NULL); //Envoie au parent.
    free(exit);
}

void pollChild(pat_t *pat, int i) {

    int pollStat = 1; //Code de retour de "poll".
    size_t size = 0; //Sert a sortir de la boucle de "poll" si il n'y a pas eu d'événements.
    char *sep = calloc(4, strlen(pat->delim) + 2);

    if(!sep) exitMain(pat, sep);
    snprintf(sep, strlen(pat->delim) * 4 + 2,"%s%s%s", pat->delim, pat->delim, pat->delim);

    while(pollStat > 0) {
        pollStat = poll(pat->pipes[i]->fdsChild, 3, -1);

        char buf[4096] = {0};
        if(fflush(stdout) != 0) exitMain(pat, sep);

        if (pat->pipes[i]->fdsChild[1].revents & POLLIN) {
            if(fflush(stdout) != 0) exitMain(pat, sep); //Vider stdout au cas ou la derniere sortie n'avait pas de saut de ligne.
            size = read(pat->pipes[i]->wOpipes[0], buf, sizeof(buf));
            if(size > 0 && pat->nbrCmds > 1) printf(" stdout %d\n%s", pat->pipes[i]->numCmd, buf);
            if(size > 0 && pat->nbrCmds == 1) printf(" stdout\n%s", buf);
            if(fflush(stdout) != 0) exitMain(pat, sep);

        }else if (pat->pipes[i]->fdsChild[2].revents & POLLIN) {
            if(fflush(stdout) != 0) exitMain(pat, sep);
            size = read(pat->pipes[i]->wEpipes[0], buf, sizeof(buf));
            if(size > 0 && pat->nbrCmds > 1) fprintf(stderr," stderr %d\n%s", pat->pipes[i]->numCmd, buf);
            if(size > 0 && pat->nbrCmds == 1) fprintf(stderr," stderr\n%s", buf);
            if(fflush(stdout) != 0) exitMain(pat, sep);

        }else if (pat->pipes[i]->fdsChild[0].revents & POLLIN) {
            if(fflush(stdout) != 0) exitMain(pat, sep);
            size = read(pat->pipes[i]->wexpipes[0], buf, sizeof(buf));
            if(size > 0 && pat->nbrCmds > 1) printf(" exit %d, %s", pat->pipes[i]->numCmd, buf);
            else if(size > 0 && pat->nbrCmds == 1) printf(" exit, %s", buf);
            if(fflush(stdout) != 0) exitMain(pat, sep);
        }
        if(size == 0) break;
        size = 0;
    }
    if(fflush(stdout) != 0) exitMain(pat, sep);
    free(sep);
    if(pollStat == -1) exitMain(pat, sep);
    for(int j = 0; j < pat->nbrCmds; ++j) free(pat->pipes[j]);
    close(pat->stdOut[0]);
    close(pat->stdErr[0]);
    free(pat->pipes);
    free(pat->fdsChild);
    _exit(0);
}

void pollMain(pat_t *pat) {

    int pollStat = 1; //Code de retour de "poll".
    size_t size = 0; //Sert a sortir de la boucle de "poll" si il n'y a pas eu d'événements.
    char *sep = calloc(4, strlen(pat->delim) + 2);

    if(!sep) exitMain(pat, sep);
    snprintf(sep, strlen(pat->delim) * 4 + 2,"%s%s%s", pat->delim, pat->delim, pat->delim);
    char *string = calloc(1, FILENAME_MAX);
    *string = (char) {'&'};

    while(pollStat > 0) {
        pollStat = poll(pat->fdsMain, 2, -1);
        char buf[FILENAME_MAX] = {0};
        if(fflush(stdout) != 0) exitMain(pat, sep); //Vider stdout au cas ou la derniere sortie n'avait pas de saut de ligne.

        if (pat->fdsMain[0].revents & POLLIN) {
            size = read(pat->stdOut[0], buf, sizeof(buf));
            if (size > 0) {
                printf("%s%s", sep, buf);
                if(fflush(stdout) != 0) exitMain(pat, sep);
            }
            if (size > 0 && buf[size - 1] != '\n')
                snprintf(sep, strlen(pat->delim) * 4 + 2, "\n%s%s%s%s", pat->delim, pat->delim, pat->delim, pat->delim);
            else snprintf(sep, strlen(pat->delim) * 4 + 2, "%s%s%s", pat->delim, pat->delim, pat->delim);
            if (fflush(stdout) != 0) exitMain(pat, sep);
        }
        if(pat->fdsMain[1].revents & POLLIN) {
            size = read(pat->stdErr[0], buf, sizeof(buf));
            if (size > 0) {
                printf("%s%s", sep, buf);
                if(fflush(stdout) != 0) exitMain(pat, sep);
            }
            if (size > 0 && buf[size - 1] != '\n')
                snprintf(sep, strlen(pat->delim) * 4 + 2, "\n%s%s%s%s", pat->delim, pat->delim, pat->delim, pat->delim);
            else snprintf(sep, strlen(pat->delim) * 4 + 2, "%s%s%s", pat->delim, pat->delim, pat->delim);
            if (fflush(stdout) != 0) exitMain(pat, sep);
        }
        if(size == 0) break;
        size = 0;
    }
    if (fflush(stdout) != 0) exitMain(pat, sep);
    free(sep);
    free(string);
    if(pollStat == -1) exitMain(pat, sep);
}

//TODO
void fixLines(char *buf) {

}

void pPoll(pat_t *pat) {

    for (int i = 0; i < pat->nbrCmds; ++i) {
        pat->pipes[i]->fdsChild[0].fd = pat->pipes[i]->wexpipes[0]; //Bout du tube qui lit "read" ([0]).
        pat->pipes[i]->fdsChild[0].events = POLLIN; //Si il y a des données a lire.
        pat->pipes[i]->fdsChild[1].fd = pat->pipes[i]->wOpipes[0];
        pat->pipes[i]->fdsChild[1].events = POLLIN;
        pat->pipes[i]->fdsChild[2].fd = pat->pipes[i]->wEpipes[0];
        pat->pipes[i]->fdsChild[2].events = POLLIN;
    }
    pipe(pat->stdOut);
    pipe(pat->stdErr);
    pat->fdsMain[0].fd = pat->stdOut[0];
    pat->fdsMain[0].events = POLLIN;
    pat->fdsMain[1].fd = pat->stdErr[0];
    pat->fdsMain[1].events = POLLIN;
}

void exitStd(pat_t *pat, int i) {

    for(int j = 0; j < pat->nbrCmds; ++j) {
        if (close(pat->pipes[j]->wOpipes[i]) == -1 || close(pat->pipes[j]->wEpipes[i]) == -1
            || close(pat->pipes[j]->wexpipes[i]) == -1) {
            fflush(stdout);
            exitMain(pat, NULL);
        }
    }
}

void exitMain(pat_t *pat, char *heapV) {

    if(pat->newCmd) free(pat->newCmd);
    free(heapV); //Heap réfere a une variable alloué sur le tas. Si la fonction appelante n'en as pas heap = NULL.
    free(pat); //Si la fonction appelante n'a pas la structure "pat", celle-ci peut mettre NULL a sa place.
    perror("Erreur fatale!");
    _exit(1);
}
