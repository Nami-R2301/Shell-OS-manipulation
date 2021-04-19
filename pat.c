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
    struct pollfd fds[3];
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
    struct pollfd *fds; //Tubes utilisé pour communiquer avec "poll" (stdout, stderr, exit).
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
void polling(pat_t *, int);

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
    *pat = (struct pat_s) {0, "+", 0, NULL, 0, 0, 0, NULL, NULL};
    if (strcmp(argv[1], "-s") == 0 && strlen(argv[2]) >= 1) {
        pat->delim = argv[2];
        pat->option = 2;
    }
    countCmds(pat, argv, argc);
    if(pat->nbrCmds == 0) {
        fprintf(stderr, "Erreur! : Mauvais argument(s) et/ou commande(s)\n");
        return 1;
    }
    pat->fds = calloc(pat->nbrCmds, sizeof(struct pollfd) * 3);
    pPoll(pat);
    int retour = 1; //Code de retour de "pat".
    pid_t pid = fork();
    if(pid == -1) exitMain(pat, NULL);
    if(pid == 0) {
        exitStd(pat, 0); //Fermeture des tubes "read (stdin)" de l'enfant.
        retour = forking(pat, argc, argv); //Somme des codes de retour des commandes.
    }
    else {
        exitStd(pat, 1); //Fermeture des tubes "write (stdout)" du parent.
        if(pat->nbrCmds > 1) {
            for (int i = 0; i < pat->nbrCmds; ++i) {
                pid_t enfant = fork();
                if (enfant == 0) polling(pat, i); //Attentes des événements de l'enfant.
            }
        } else polling(pat, 0);
        if(waitpid(pid,&pid,0) != -1) { //Assurer que tout est terminer avant de retourner.
            fflush(stdout);
            if(WIFEXITED(pid)) retour = WEXITSTATUS(pid);
            if(WIFSIGNALED(pid)) retour += WTERMSIG(pid);
            sleep(1);
            exitStd(pat, 0); //Fermeture des tubes "read (stdin)" du parent.
        }
    }
    free(pat->fds);
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
    siginfo_t info;
    int numCmd = getpid();
    pid_t neveu;
    for (int i = 1 + pat->option; i < argc; ++i) {
        pat->posDelF = 0;
        pat->posDelD = i; //Indice de début de commande.
        readCmds(argv, &argc, pat);
        i += pat->posDelF; //Indice de fin de commande.
        neveu = fork();
        if(neveu == -1) exitMain(pat, NULL);
        if (neveu == 0) execCmds(pat, nbrCmds);
        nbrCmds++;
    }
    while(waitid(P_ALL, neveu, &info, WEXITED | WSTOPPED) != -1) { //Parent des neveux (Enfant) attend que tous les neveux terminent.
        int num;
        if(pat->nbrCmds > 1) num = ((info.si_pid - numCmd)  - nbrCmds) - 1;
        else num = 0;
        if (info.si_code == CLD_KILLED || info.si_code == CLD_STOPPED) {
            pat->sig = info.si_status;
            retour += 128 + pat->sig;
        }else if(info.si_code == CLD_EXITED) retour += info.si_status;
        printExit(info.si_status, pat, num); //Envoie vers "pat->stdExit[1]".
        pat->sig = 0;
        info = (siginfo_t) {0};
    }
    exitStd(pat, 1); //Fermeture des tubes "write (stdout)" de l'enfant.
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

    char *exit = malloc(strlen(pat->delim) + 12);
    if(!exit) exitMain(pat, exit);

    if(pat->sig == 0) sprintf(exit, "status=%d\n", retour); //Fin normale de commande.
    else sprintf(exit,"signal=%d\n", pat->sig); //Fin abrupte (interruption).

    if(write(pat->pipes[numCmd]->wexpipes[1], exit, strlen(exit)) == -1) exitMain(pat, NULL); //Envoie au parent.
    free(exit);
}

void polling(pat_t *pat, int i) {

    int pollStat = 1; //Code de retour de "poll".
    size_t size = 0; //Sert a sortir de la boucle de "poll" si il n'y a pas eu d'événements.
    bool flagO = false; //Vérifier si la derniere sortie était une sortie standard pour éviter la répétition de séparateur.
    bool flagE = false; //Vérifier si la derniere sortie était une sortie d'erreur pour éviter la répétition de séparateur.
    bool flagEx = false;
    char *sep = calloc(4, strlen(pat->delim) + 2);

    if(!sep) exitMain(pat, sep);
    snprintf(sep, strlen(pat->delim) * 4 + 2,"%s%s%s", pat->delim, pat->delim, pat->delim);

    while(!flagEx && pollStat >= 1) {
        pollStat = poll(pat->pipes[i]->fds, 3, -1);

        char buf[4096] = {0};
        fflush(stdout);
        if (pat->pipes[i]->fds[1].revents & POLLIN) {
            if(fflush(stdout) != 0) exitMain(pat, sep); //Vider stdout au cas ou la derniere sortie n'avait pas de saut de ligne.
            flagE = false;
            size = read(pat->pipes[i]->wOpipes[0], buf, sizeof(buf));
            if(size > 0) {
                if(!flagO && pat->nbrCmds > 1) printf("%s stdout %d\n", sep, pat->pipes[i]->numCmd);
                else if(!flagO) printf("%s stdout\n", sep);
                printf("%s", buf);
                if(fflush(stdout) != 0) exitMain(pat, sep);
            }
            if (size > 0 && buf[size - 1] != '\n')
                snprintf(sep, strlen(pat->delim) * 4 + 2, "%s%s%s%s", pat->delim, pat->delim, pat->delim, pat->delim);
            else snprintf(sep, strlen(pat->delim) * 4 + 2, "%s%s%s", pat->delim, pat->delim, pat->delim);
            if(size > 0 && buf[size - 1] != '\n') printf("\n");
            if(fflush(stdout) != 0) exitMain(pat, sep);
            flagO = true;
        }
        if (pat->pipes[i]->fds[2].revents & POLLIN) {
            if(fflush(stdout) != 0) exitMain(pat, sep);
            flagO = false;
            size = read(pat->pipes[i]->wEpipes[0], buf, sizeof(buf));
            if(size > 0) {
                if(!flagE && pat->nbrCmds > 1) printf("%s stderr %d\n", sep, pat->pipes[i]->numCmd);
                else if(!flagE) printf("%s stderr\n", sep);
                printf("%s", buf);
                if(fflush(stdout) != 0) exitMain(pat, sep);
            }
            if (size > 0 && buf[size - 1] != '\n')
                snprintf(sep, strlen(pat->delim) * 4 + 2, "%s%s%s%s", pat->delim, pat->delim, pat->delim, pat->delim);
            else snprintf(sep, strlen(pat->delim) * 4 + 2, "%s%s%s", pat->delim, pat->delim, pat->delim);
            if(size > 0 && buf[size - 1] != '\n') printf("\n");
            if(fflush(stdout) != 0) exitMain(pat, sep);
            flagE = true;
        }
        if (pat->pipes[i]->fds[0].revents & POLLIN) {
            if(fflush(stdout) != 0) exitMain(pat, sep);
            size = read(pat->pipes[i]->wexpipes[0], buf, sizeof(buf));
            if(size > 0 && pat->nbrCmds > 1) printf("%s exit %d, %s", sep, pat->pipes[i]->numCmd, buf);
            else if(size > 0) printf("%s exit, %s", sep, buf);
            if(fflush(stdout) != 0) exitMain(pat, sep);
            flagEx = true;
        }
        size = 0;
    }
    free(sep);
    if(pollStat == -1) exitMain(pat, sep);
    if(pat->nbrCmds > 1) _exit(0);
}

void pPoll(pat_t *pat) {

    for(int i = 0; i < pat->nbrCmds; ++i) {
        pat->pipes[i]->fds[0].fd = pat->pipes[i]->wexpipes[0]; //Bout du tube qui lit "read" ([0]).
        pat->pipes[i]->fds[0].events = POLLIN; //Si il y a des données a lire.
        pat->pipes[i]->fds[1].fd = pat->pipes[i]->wOpipes[0];
        pat->pipes[i]->fds[1].events = POLLIN;
        pat->pipes[i]->fds[2].fd = pat->pipes[i]->wEpipes[0];
        pat->pipes[i]->fds[2].events = POLLIN;
    }
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

    free(pat->newCmd);
    free(heapV); //Heap réfere a une variable alloué sur le tas. Si la fonction appelante n'en as pas heap = NULL.
    free(pat); //Si la fonction appelante n'a pas la structure "pat", celle-ci peut mettre NULL a sa place.
    perror("Erreur fatale!");
    _exit(1);
}
