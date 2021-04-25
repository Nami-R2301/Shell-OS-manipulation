#include <stdio.h> //printf, snprintf, sprintf, fflush, stdout, stderr, FILENAME_MAX, perror 
#include <string.h> //strtok, strcmp, strstr et strlen, strncat, strcat.
#include <unistd.h> //fork, dup, dup2, pipe, execvp, write, read, close, STDOUT_FILENO, STDERR_FILENO.
#include <wait.h>  //waitid, waitpid, siginfo_t, 
#include <stdlib.h> //malloc, calloc, free.
#include <sys/poll.h> //poll, pollfd_s, pollfds_t
#include <stdbool.h> //bool

/* ********************* *
 * TP2 INF3173 H2021
 * Code permanent: REGN03079808
 * Nom: Reghbati
 * Prénom: Nami
 * ********************* */

//Structure qui crée des tubes pour l'échange de données entre forking() et les polls (pour chacune commande).
typedef struct pollfds_s {
    int numCmd; //Numero de la commande en cours de lecture (poll).
    int wOpipes[2]; //Pipe pour la sortie standard d'erreur envoyé à forking.
    int wEpipes[2]; //Pipe pour la sortie standard d'erreur envoyé a forking.
    int wexpipes[2]; //Pipe pour les codes de retour des commandes lues, envoyé a forking.
    struct pollfd fdsChild[3]; //Pollfd contenant les descripteurs de fichiers de tous les tubes pour le traitage des données émises par l'enfant (forking()).
} pollfds_t;

typedef struct pat_s {
    int option; //Option -s.
    char *delim; //Délimiteur de commandes.
    int nbrCmds; //Nombre de commandes.
    char **newCmd; //Un pseudo "argv[]" pour execvp.
    int sig; //signal (SIGACT) recu lors de l'exécution d'une commande.
    int posDelD; //Position de début de la commande évaluée.
    int posDelF; //Position de fin de la commande évaluée.
    pollfds_t **pipes; //Pointeur de tableau de tubes contenant wopipes[], wepipes[] et wexpipes[] (trois par commande).

    //Tubes servant a communiquer les données pollé dans polling() vers la fonction printfpoll.
    int stdOut[2]; //Pour stdout (printPoll()).
    int stdErr[2]; //Pour stderr (printPoll()).
    int stdExit[2]; //Pour les codes de retours (printPoll()).
    struct pollfd *fdsChild; //Tubes utilisé pour communiquer avec "poll" (stdout, stderr, exit).
    struct pollfd fdsMain[3]; //Descripteurs de fichiers qui recevrons les données des tubes lus dans polling() (**pipes).
} pat_t;

//Sert a compter les commandes pour préparer les structures plus haut avant de forker.
void countCmds(pat_t *, char **, int);

//Sert a lire les commandes pour l'exécution de celles-ci dans l'enfant.
void readCmds(char **, const int *, pat_t *);

//Sert a exécuter les commandes avec execvp à l'indice spécifié.
void execCmds(pat_t *, int);

//Sert a préparer les tubes et les descripteurs de fichiers pour la communication entre "poll" et l'enfant.
void pPoll(pat_t *);

//Transmet les données affichées par les sorties des commandes évaluées au parent en parallele.
int forking(pat_t *, int, char **);

//Aquiesce et gere les événements et données transmises par l'enfant en parallele.
void polling(pat_t *pat, int i);

//Traite et formate les données transmise par polling() pour les afficher dans la sortie standard.
void printPoll(pat_t *pat);

//Vérifie si l'entrée précédente est la meme que celle courante (deux séparateurs identiques consécutifs).
void checkReps(char *buf, char *string, char *sep);

//Récupere les codes de retour des commandes ainsi que les signaux puis les transmets au parent via stdExit[1].
void printExit(int , pat_t *, int);

//Sert a généraliser les étapes de fermeture du programme en cas d'erreur (exit(1)).
//Si la fonction appelante contient une variable dynamique alloué, celle-ci sera mise comme deuxième argument. 
void exitMain(pat_t *, char *);

//Sert a généraliser les étapes de fermeture des tubes dépendemment du thread appelant (parent/enfant/neveu).
void exitStd(pat_t *, int);

//Libere toutes les variables dynamiques communes entres les fonctions pour éliminer la redondance.
void freeing(pat_t *, char *);

int main(int argc, char *argv[]) {

    //Si l'argument est valide
    if(argc < 2 || (argc == 2 && strcmp(argv[1], "-s") == 0) || (argc == 3 && strcmp(argv[1], "-s") == 0)) {
        fprintf(stderr, "Erreur! : Mauvais argument(s) et/ou commande(s)\n");
        return 1;
    }
    pat_t *pat = malloc(sizeof(struct pat_s));
    if(!pat) exitMain(NULL, NULL);
    *pat = (struct pat_s) {0, "+", 0, NULL, 0, 0, 0, NULL, {0}, {0}, {0}, NULL, {0}};
    if(strcmp(argv[1], "-s") == 0 && strlen(argv[2]) >= 1) {
        pat->delim = argv[2];
        pat->option = 2; //-s + le séparateur, donc argv[+2].
    }
    countCmds(pat, argv, argc);
    if(pat->nbrCmds == 0) {
        fprintf(stderr, "Erreur! : Mauvais argument(s) et/ou commande(s)\n");
        return 1;
    }
    pat->fdsChild = calloc(pat->nbrCmds, sizeof(struct pollfd) * 3);
    if(!pat->fdsChild) exitMain(pat, NULL);
    pPoll(pat); //Préparer les tubes et descripteurs de fichiers pour poll.
    int retour = 0; //Code de retour de "pat".
    pid_t pid = fork();
    if(pid == -1) exitMain(pat, NULL);
    if(pid > 0) {
        exitStd(pat, 1); //Fermeture des tubes "write (stdout)" du parent.
        pid_t enfant = fork(); //Lire les données émise par les commande en parallele.
        pid_t neveu; //Poll en parallele
        if(enfant == -1) exitMain(pat, NULL);
        if(enfant == 0) {
            //Duplication des entrées standard
            if(dup2(pat->stdOut[1], STDOUT_FILENO) == -1 || dup2(pat->stdErr[1], STDERR_FILENO) == -1) {
                exitMain(pat, NULL);
            }
            if(close(pat->stdOut[1]) == -1 || close(pat->stdErr[1]) == -1) exitMain(pat, NULL);
            for(int i = 0; i < pat->nbrCmds; ++i) {
                neveu = fork();
                if(neveu == -1) exitMain(pat, NULL);
                if(neveu == 0) polling(pat, i); //Attentes des événements de l'enfant.
            }
            if(fflush(stdout) != 0) exitMain(pat, NULL);
            exitStd(pat, 0); //Fermeture des tubes "read (stdin)" du parent.
            exitStd(pat, 2); //Fermeture des tubes stdOut, stdErr et stdExit a l'indice 0.
            if(close(STDOUT_FILENO) == -1 || close(STDERR_FILENO) == -1) exitMain(pat, NULL);
        } else if(enfant > 0) { //Lire les données émises par polling().
            exitStd(pat, 3); //Fermeture des tubes stdOut, stdErr et stdExit a l'indice 1.
            printPoll(pat);
            while (waitpid(pid, &pid, 0) != -1) { //Attendre pid au cas ou l'enfant n'ai pas terminé.
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
    freeing(pat, NULL);
    return retour;
}

void countCmds(pat_t *pat, char **argv, const int argc) {

    bool arg = false; //Vérifier si c'est est bel et bien une commande et non un argument de commande.
    pat->pipes = calloc(argc - 1, sizeof(pollfds_t));
    for(int i = pat->option + 1; i  < argc; ++i) {
        if(strcmp(argv[i], pat->delim) != 0) {
            if(!arg) {
                pat->pipes[pat->nbrCmds] = malloc(sizeof(pollfds_t));
                pat->pipes[pat->nbrCmds]->numCmd = pat->nbrCmds + 1; //Identifiant de chaque commande.
                if(pipe(pat->pipes[pat->nbrCmds]->wexpipes) == -1 || pipe(pat->pipes[pat->nbrCmds]->wOpipes) == -1
                   || pipe(pat->pipes[pat->nbrCmds]->wEpipes) == -1)
                    exitMain(pat, NULL);
                pat->nbrCmds++;
            }
            arg = true;
        } else arg = false; //Sinon, le prochain indice est forcément une commande.
    }
}

void readCmds(char **argv, const int *args, pat_t *pat) {

    if(pat->newCmd) free(pat->newCmd); //Libéré l'ancienne commande avant de procédé à la prochaine.
    pat->newCmd = calloc(*args - (pat->option + 1), FILENAME_MAX); //Pour maximum robustesse.
    if(!pat->newCmd) exitMain(pat, NULL);

    for(int i = 0; i + pat->posDelD < *args; ++i) {
        if(strcmp(argv[i + pat->posDelD], pat->delim) != 0) {
            pat->newCmd[i] = (char *) {""}; //Pour calmer Valgrind.
            pat->newCmd[i] = argv[i + pat->posDelD];
            pat->posDelF++;
        } else break; //Fin de "argv[]".
    }

}

int forking(pat_t *pat, int argc, char **argv) {

    int retour = 0; //Somme des codes de retour des commandes.
    int nbrCmds = 0; //Indice de chaque commande pour remplir le tableau de pid (neveu[]).
    int num; //Numéro de commande pour wexpipe[] (code de retour) dans printfExit().
    siginfo_t info = {0}; //Utilisé pour déterminer quelle commande (pid) a fini.
    pid_t neveu[pat->nbrCmds]; //Tableau de pids utilisé pour stocker chaque pid de commande qui c'est exécuté lors de la boucle.
    for(int i = 1 + pat->option; i < argc; ++i) {
        pat->posDelF = 0;
        pat->posDelD = i; //Indice de début de commande.
        readCmds(argv, &argc, pat);
        i += pat->posDelF; //Indice de fin de commande.
        neveu[nbrCmds] = fork(); //Rajoute le pid courant.
        if(neveu[nbrCmds] == -1) exitMain(pat, NULL);
        if(neveu[nbrCmds] == 0) execCmds(pat, nbrCmds);
        nbrCmds++;
    }
    while(waitid(P_ALL, 0, &info, WEXITED | WSTOPPED) != -1) { //Parent des neveux (Enfant) attend que tous les neveux terminent.
        for(int i = 0; i < nbrCmds; ++i) {
            if(neveu[i] == info.si_pid) { //Vérifier quel neveu (numéro) est associé avec ce pid.
                num = i;
                if(info.si_code == CLD_KILLED || info.si_code == CLD_STOPPED) {
                    pat->sig = info.si_status;
                    retour += 128 + pat->sig;
                } else if(info.si_code == CLD_EXITED) retour += info.si_status;
                printExit(info.si_status, pat, num); //Envoie vers "pat->stdExit[1]".
                pat->sig = 0; //Préparer pour le prochain (potentiel) signal.
            }
        }
    }
    return retour;
}

void execCmds(pat_t *pat, int i) {

    if(dup2(pat->pipes[i]->wEpipes[1], STDERR_FILENO) == -1 || dup2(pat->pipes[i]->wOpipes[1], STDOUT_FILENO) == -1) {
        exitMain(pat, NULL);
    }
    exitStd(pat, 1); //Fermeture des tubes wopipe, wepipe et wexpipe a l'indice 1 du neveu.
    execvp(pat->newCmd[0], pat->newCmd);
    perror(pat->newCmd[0]);
    freeing(pat, NULL);
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

void polling(pat_t *pat, int i) {

    int pollStat = 1; //Code de retour de "poll".
    size_t size = 0; //Sert a sortir de la boucle de "poll" si il n'y a pas eu d'événements.
    char *exit = calloc(1, FILENAME_MAX); //Utilisé pour snprintf() pour envoyer le code de retour séparément.

    while(pollStat > 0) {
        pollStat = poll(pat->pipes[i]->fdsChild, 3, -1); //Vérifie l'état des descripteurs de fichiers.
        if(pollStat == -1) exitMain(pat, exit);
        char buf[FILENAME_MAX] = {0};
        if(fflush(stdout) != 0) exitMain(pat, exit); //Vider stdout au cas ou la derniere sortie n'avait pas de saut de ligne.

        if(pat->pipes[i]->fdsChild[1].revents & POLLIN) { //Données lues de la sortie standard (wopipe[1]).
            if(fflush(stdout) != 0) exitMain(pat, exit);
            size = read(pat->pipes[i]->wOpipes[0], buf, sizeof(buf));
            if(size == -1) exitMain(pat, exit);
            if(size > 0 && pat->nbrCmds > 1) printf(" stdout %d\n%s", pat->pipes[i]->numCmd, buf);
            if(size > 0 && pat->nbrCmds == 1) printf(" stdout\n%s", buf);
            if(fflush(stdout) != 0) exitMain(pat, exit);

        }else if(pat->pipes[i]->fdsChild[2].revents & POLLIN) { //Données lues de la sortie standard d'erreur (wepipe[1]).
            if(fflush(stdout) != 0) exitMain(pat, exit);
            size = read(pat->pipes[i]->wEpipes[0], buf, sizeof(buf));
            if(size == -1) exitMain(pat, exit);
            if(size > 0 && pat->nbrCmds > 1) fprintf(stderr," stderr %d\n%s", pat->pipes[i]->numCmd, buf);
            else if(size > 0) fprintf(stderr," stderr\n%s", buf);
            if(fflush(stdout) != 0) exitMain(pat, exit);

        }else if(pat->pipes[i]->fdsChild[0].revents & POLLIN) { //Données lues du tube de code de retour (wexpipe[1]).
            if(fflush(stdout) != 0) exitMain(pat, exit);
            size = read(pat->pipes[i]->wexpipes[0], buf, sizeof(buf));
            if(size == -1) exitMain(pat, exit);
            if(size > 0 && pat->nbrCmds > 1) snprintf(exit, strlen(buf) + 11," exit %d, %s", pat->pipes[i]->numCmd, buf);
            else if(size > 0) snprintf(exit, strlen(buf) + 11," exit, %s", buf);
            if(write(pat->stdExit[1], exit, strlen(exit)) == -1) exitMain(pat, NULL);
            if(fflush(stdout) != 0) exitMain(pat, exit);
        }
        if(size == 0) break; //Quitter si rien n'a été lu.
        size = 0;
    }
    if(fflush(stdout) != 0) exitMain(pat, exit);
    if(pollStat == -1) exitMain(pat, exit);
    exitStd(pat, 2); //Fermeture des tubes stdOut, stdErr et stdExit a l'indice 0 du neveu.
    freeing(pat, exit);
    _exit(0);
}

void printPoll(pat_t *pat) {

    int pollStat = 1; //Code de retour de "poll".
    size_t size = 0; //Sert a sortir de la boucle de "poll" si il n'y a pas eu d'événements.
    char *sep = calloc(4, strlen(pat->delim) + 2); //Contient le séparateur
    if(!sep) exitMain(pat, sep);
    snprintf(sep, strlen(pat->delim) * 4 + 2,"%s%s%s", pat->delim, pat->delim, pat->delim);
    char *string = calloc(1, FILENAME_MAX); //Contient la sortie précédente pour compareravec la courante.

    while(pollStat > 0) {
        pollStat = poll(pat->fdsMain, 3, -1); //Pas de timeout (infinie). 
        if(pollStat == -1) exitMain(pat, sep);
        char buf[FILENAME_MAX] = {0}; //Buffer contenant toutes les données.
        if(fflush(stdout) != 0) exitMain(pat, sep); //Vider stdout au cas ou la derniere sortie n'avait pas de saut de ligne.

        if(pat->fdsMain[0].revents & POLLIN) { //Données lues de la sortie standard (stdOut[1]/STDOUT_FILENO).
            size = read(pat->stdOut[0], buf, sizeof(buf));
            if(size == -1) exitMain(pat, sep);
            if(size > 0) checkReps(buf, string, sep); //Vérifier si il y a une répétition.
            if(size > 0 && buf[size - 1] != '\n')
                snprintf(sep, strlen(pat->delim) * 4 + 2, "\n%s%s%s%s", pat->delim, pat->delim, pat->delim, pat->delim);
            else snprintf(sep, strlen(pat->delim) * 4 + 2, "%s%s%s", pat->delim, pat->delim, pat->delim);
            if(fflush(stdout) != 0) exitMain(pat, sep);

        } else if(pat->fdsMain[1].revents & POLLIN) { //Données lues de la sortie standard d'erreur (stdErr[1]/STDERR_FILENO).
            size = read(pat->stdErr[0], buf, sizeof(buf));
            if(size == -1) exitMain(pat, sep);
            if(size > 0) checkReps(buf, string, sep); //Vérifier si il y a une répétition.
            if(size > 0 && buf[size - 1] != '\n')
                snprintf(sep, strlen(pat->delim) * 4 + 2, "\n%s%s%s%s", pat->delim, pat->delim, pat->delim, pat->delim);
            else snprintf(sep, strlen(pat->delim) * 4 + 2, "%s%s%s", pat->delim, pat->delim, pat->delim);
            if(fflush(stdout) != 0) exitMain(pat, sep);

        } else if(pat->fdsMain[2].revents & POLLIN) { //Données lues du tube de code de retour (stdExit[1]/STDOUT_FILENO).
            size = read(pat->stdExit[0], buf, sizeof(buf));
            if(size == -1) exitMain(pat, sep);
            if(size > 0) checkReps(buf, string, sep); //Vérifier si il y a une répétition.
        }
        if(size > 0) strncpy(string, buf, strlen(buf)); //Copier l'ancienne commande.
        if(size == 0) break; //Quitter si rien n'a été lu.
        size = 0;
    }
    if(fflush(stdout) != 0) exitMain(pat, sep);
    free(sep);
    free(string);
    if(pollStat == -1) exitMain(pat, sep);
}

void checkReps(char *buf, char *string, char *sep) {

    char *retour = calloc(1, FILENAME_MAX); //Pour éviter de tronquer le buf original.
    char *point; //Pointeur qui pointe vers la chaine de caractere tronquer par strtok().
    strncpy(retour, buf, strlen(buf)); //Copie des sorties.
    if(strcmp(string, "") != 0) {
        strtok(string, "\n"); //Vérifier si la ligne de séparation est la meme (+++ stdout %d\n).
        strtok(retour, "\n");
    }
    if(strcmp(string, "") != 0 && strstr(string, retour)) {
        point = strtok(NULL, "\0"); //Si il y a une correspondance, pointer seulement vers le reste de la chaine.
        if(point) printf("%s", point);
    } else printf("%s%s", sep, buf); //Sinon, afficher les séparateur avec la chaine.
    free(retour);
}

void pPoll(pat_t *pat) {

    for(int i = 0; i < pat->nbrCmds; ++i) {
        pat->pipes[i]->fdsChild[0].fd = pat->pipes[i]->wexpipes[0]; //Bout du tube qui lit "read" ([0]).
        pat->pipes[i]->fdsChild[0].events = POLLIN; //Si il y a des données a lire.
        pat->pipes[i]->fdsChild[1].fd = pat->pipes[i]->wOpipes[0];
        pat->pipes[i]->fdsChild[1].events = POLLIN;
        pat->pipes[i]->fdsChild[2].fd = pat->pipes[i]->wEpipes[0];
        pat->pipes[i]->fdsChild[2].events = POLLIN;
    }
    if(pipe(pat->stdOut) == -1 || pipe(pat->stdErr) == -1 || pipe(pat->stdExit) == -1) exitMain(pat, NULL);
    pat->fdsMain[0].fd = pat->stdOut[0];
    pat->fdsMain[0].events = POLLIN;
    pat->fdsMain[1].fd = pat->stdErr[0];
    pat->fdsMain[1].events = POLLIN;
    pat->fdsMain[2].fd = pat->stdExit[0];
    pat->fdsMain[2].events = POLLIN;
}

void exitStd(pat_t *pat, int i) {

    if(i == 2) {
        if(close(pat->stdOut[0]) == -1 || close(pat->stdErr[0]) == -1 || close(pat->stdExit[0])) exitMain(pat, NULL);
    } else if(i == 3) {
        if(close(pat->stdOut[1]) == -1 || close(pat->stdErr[1]) == -1 || close(pat->stdExit[1])) exitMain(pat, NULL);
    } else {
        for (int j = 0; j < pat->nbrCmds; ++j) {
            if (close(pat->pipes[j]->wOpipes[i]) == -1 || close(pat->pipes[j]->wEpipes[i]) == -1
                || close(pat->pipes[j]->wexpipes[i]) == -1) {
                fflush(stdout);
                exitMain(pat, NULL);
            }
        }
    }
}

void exitMain(pat_t *pat, char *heapV) {

    freeing(pat, heapV);
    perror("Erreur fatale!");
    _exit(1);
}

void freeing(pat_t *pat, char *heapV) {

    free(heapV); //Heap réfere a une variable alloué sur le tas. Si la fonction appelante n'en as pas heap, = NULL.
    free(pat->newCmd);
    for(int j = 0; j < pat->nbrCmds; ++j) free(pat->pipes[j]); //Fermer tous les tubes de chaque commandes.
    free(pat->pipes);
    free(pat->fdsChild);
    free(pat); //Si la fonction appelante n'a pas la structure "pat", celle-ci peut mettre NULL a sa place.
}
