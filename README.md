# TP2 - pat patrouille à la rescousse

L'objectif du TP2 est de développer l'utilitaire `pat` qui surveille en parallèle les sorties standard et d'erreur de plusieurs commandes.

## Description

```
pat [-s séparateur] commande arguments [+ commande arguments]...
```

Dans une session interactive dans un terminal, les sorties standard et d'erreurs des commandes sont affichées en vrac sur le terminal ce qui dans certains cas rend l'affichage confus et difficile à interpréter.

pat, pour *process cat* (ou *process and tail*) permet de distinguer les différentes sorties des commandes en insérant des lignes de séparation.

Par exemple, le programme `prog1` mélange sortie standard et sortie d'erreur de façon indiscernable.

```
$ ./prog1
Bonjour
Message d'erreur
Le
Monde
Message d'erreur de fin
```

Avec pat, des lignes de séparation sont affichées quand c'est une sortie différente qui utilisée.

```
$ pat ./prog1
+++ stdout
Bonjour
+++ stderr
Message d'erreur
+++ stdout
Le
Monde
+++ stderr
Message d'erreur de fin
+++ exit, status=0
```

Par défaut, quand il n'y a qu'une seule commande, le format des lignes de séparation est:

* l'indicateur de séparation, par défaut c'est `+++`;
* un espace
* `stdout` ou `stderr` pour distinguer la sortie

Lorsque la même sortie est réutilisée (même après un long laps de temps), aucune ligne de séparation n'est bien sûr réaffichée.

```
$ pat cat prog1
+++ stdout
#!/bin/sh
echo "Bonjour"
sleep .2
echo "Message d'erreur" >&2
sleep .2
echo "Le"
sleep .2
echo "Monde"
sleep .2
echo "Message d'erreur de fin" >&2
+++ exit, status=0
```

Lorsque la commande se termine, une ligne de séparation spéciale est affichée qui indique `exit`. Voir plus loin pour les détails.


### Lignes non terminées

La confusion entre les différentes sorties est exacerbée lorsque les lignes affichées ne sont pas terminées.

```
$ ./prog2
HelloFirst errorWorld2nd WorldLast error
```

pat permet de distinguer simplement et de façon non abiguë si un changement de sortie survient alors qu'une fin de ligne n'a pas été encore affichée.

```
$ pat ./prog2
+++ stdout
Hello
++++ stderr
First error
++++ stdout
World2nd World
++++ stderr
Last error
++++ exit, status=1
```

Ainsi, si la dernière ligne n'est pas terminée par un caractère de saut de ligne, un est inséré et l'indicateur de séparation est `++++` (quatre au lieu du trois).

### Terminaison de commande

Quand une commande termine de façon volontaire, la ligne de terminaison indique son code de retour avec « `, status=` ».
Quand une commande termine de façon involontaire (par un signal), la ligne de terminaison indique le numéro du signal avec « `, signal=` ».

```
$ pat ./prog3
+++ stdout
killme
+++ exit, signal=15
```

Si une commande ne peut pas s'exécuter (fichier invalide, etc.) un message d'erreur est affiché et la commande se termine avec un code de retour de 127.

```
$ pat fail
+++ stderr
fail: No such file or directory
+++ exit, status=127
```


### Exécution de plusieurs commandes

pat patrouille l'exécution de plusieurs commandes et distingue chacune leurs sorties.

L'argument « `+` » (tout seul) permet de séparer les commandes.
Un nombre quelconque de commandes peuvent donc ainsi être surveillées.

```
$ pat ./prog1 + ./prog2 + ./prog3
+++ stdout 1
Bonjour
+++ stdout 2
Hello
++++ stderr 1
Message d'erreur
+++ stderr 2
First error
++++ stdout 3
killme
+++ exit 3, signal=15
+++ stdout 1
Le
+++ stdout 2
World
++++ stdout 1
Monde
+++ stdout 2
2nd World
++++ stderr 1
Message d'erreur de fin
+++ exit 1, status=0
+++ stderr 2
Last error
++++ exit 2, status=1
```

Quand il y a plusieurs commandes, les lignes de séparation indiquent le numéro de la commande concernée. Le numéro de la commande commence à 1.

### Option -s

L'option `-s` permet de préciser le séparateur de commande utilisé dans la ligne de commande ainsi que l'indicateur utilisé dans les lignes de séparation.
Par défaut c'est `+` mais toute chaine valide peut être utilisée.

```
$ pat -s @@ ./prog2
@@@@@@ stdout
Hello
@@@@@@@@ stderr
First error
@@@@@@@@ stdout
World2nd World
@@@@@@@@ stderr
Last error
@@@@@@@@ exit, status=1
```


Changer le séparateur par défaut permet d'utiliser `pat` si jamais `+` a un sens particulier dans les arguments des commandes ou si l'indicateur `+++` au début des lignes a un sens particulier dans les sorties des commandes (c'est le cas de `diff` par exemple).

En particulier, il permet a `pat` de patrouiller `pat` !

```
$ pat -s @1 pat -s @2 ./prog1 @2 ./prog2 @1 ./prog3
@1@1@1 stdout 1
@2@2@2 stdout 1
Bonjour
@2@2@2 stdout 2
Hello
@2@2@2@2 stderr 1
Message d'erreur
@2@2@2 stderr 2
First error
@1@1@1@1 stdout 2
killme
@1@1@1 exit 2, signal=15
@1@1@1 stdout 1

@2@2@2@2 stdout 1
Le
@2@2@2 stdout 2
World
@2@2@2@2 stdout 1
Monde
@2@2@2 stdout 2
2nd World
@2@2@2@2 stderr 1
Message d'erreur de fin
@2@2@2 exit 1, status=0
@2@2@2 stderr 2
Last error
@2@2@2@2 exit 2, status=1
@1@1@1 exit 1, status=1
```

### Valeur de retour

`pat` retourne la somme des codes de retour des commandes exécutées.

Si une commande se termine avec un signal, son code de retour est 127 plus le numéro du signal.

En cas d'erreur (arguments, ressources, etc.), `pat` affiche un message d'erreur, et sa valeur de retour est 1.

## Directives d'implémentation

Vous devez développer le programme en C.
Le fichier source doit s'appeler `pat.c` et être à la racine du dépôt.
Vu la taille du projet, tout doit rentrer dans ce seul fichier source.

Pour la réalisation du TP, vous devez respecter les directives suivantes:

* Vous utiliserez fork et une fonction de la famille de exec pour exécuter les commandes.
* Vous utiliserez des tubes simples pour capturer les sorties des commandes.
* Vous utiliserez `poll` pour implémenter le multiplexage.
* Tous les affichages des commandes et des lignes de séparation sont faits sur la sortie standard. La sortie d'erreur est réservée pour les erreurs de `pat` lui-même.

Quelques conseils:

* Ne pas mélanger FILE* et descripteur pour un même flux : c'est source infinie de problèmes.
* Pensez à `fflush` aux endroits pertinents pour vous assurer que les affichages ont lieu au bon moment.
* Assurez-vous de gérer correctement les ressources : fermez les descripteurs de fichiers, libérez la mémoire, ne sortez pas de poll pour rien, etc.


## Acceptation et remise du TP

### Acceptation

Une interface web vous permet d'accepter le TP:

* [Interface web *travo*](https://info.pages.info.uqam.ca/travo-web/?project=2187)

Autrement, vous pouvez accepter le TP manuellement en faisant les trois actions directement:

* Cloner (fork) ce dépôt sur le gitlab départemental.
* Le rendre privé : dans `Settings` → `General` → `Visibility` → `Project visibility` → `Private`.
* Ajouter l'utilisateur `@privat` comme mainteneur (oui, j'ai besoin de ce niveau de droits) : dans `Settings` → `Members` → `Invite member` → `@privat` (n'invitez pas @privat2, 3 ou 4 : ce sont mes comptes de tests).
* ⚠️ Mal effectuer ces étapes vous expose à des pénalités importantes.


### Remise

La remise s'effectue simplement en poussant votre code sur la branche `master` de votre dépôt gitlab privé.
Seule la dernière version disponible avant le **dimanche 25 avril 23 h 55** sera considérée pour la correction.


### Intégration continue et mises à jour

Pour pouvez compiler avec `make` (le `Makefile` est fourni).

Vous pouvez vous familiariser avec le contenu du dépôt, en étudiant chacun des fichiers (`README.md`, `Makefile`, `check.bats`, `.gitlab-ci.yml`, etc.).

⚠️ À priori, il n'y a pas de raison de modifier un autre fichier du dépôt.
Si vous en avez besoin, ou si vous trouvez des bogues ou problèmes dans les autres fichiers, merci de me contacter.

Le système d'intégration continue vérifie votre TP à chaque `push`.
Vous pouvez vérifier localement avec `make check` (l'utilitaire `bats` entre autres est nécessaire).

Les tests fournis ne couvrent que les cas d'utilisation de base, en particulier ceux présentés ici.
Il est **fortement suggéré** d'ajouter vos propres tests dans [local.bats](local.bats) et de les pousser pour que l’intégration continue les prennent en compte.
Ils sont dans un job distincts pour avoir une meilleure vue de l'état du projet.

❤ Des points bonus pourront être attribués si `local.bats` contient des tests pertinents et généralisables.

❤ En cas de problème pour exécuter les tests sur votre machine, merci de 1. lire la documentation présente ici et 2. poser vos questions sur [/opt/tp2](https://mattermost.info.uqam.ca/inf3173-h21/channels/tp2).
Attention toutefois à ne pas fuiter de l’information relative à votre solution (conception, morceaux de code, etc.)

### Critères de correction

Seront considéré pour la correction du code: l'exactitude, la robustesse, la lisibilité, la simplicité, la conception, les commentaires, etc.

Une pénalité sera appliquée pour chacun des tests publics (`make check`) qui ne passent pas l'intégration continue du gitlab.

Comme le TP n'est pas si gros (de l'ordre de grandeur d'une ou deux centaines de lignes), il est attendu un effort important sur le soin du code et la gestion des cas d'erreurs.
Un bonus sera attribué si vous implémentez une option `-c` pertinente.

## Mentions supplémentaires importantes

⚠️ **Intégrité académique**
Rendre public votre dépôt personnel ou votre code ici ou ailleurs ; ou faire des MR contenant votre code vers ce dépôt principal (ou vers tout autre dépôt accessible) sera considéré comme du **plagiat**.

⚠️ Attention, vérifier **≠** valider.
Ce n'est pas parce que les tests passent chez vous ou ailleurs ou que vous avez une pastille verte sur gitlab que votre TP est valide et vaut 100%.
Par contre, si des tests échouent quelque part, c'est généralement un bon indicateur de problèmes dans votre code.

⚠️ Si votre programme **ne compile pas** ou **ne passe aucun test**, une note de **0 sera automatiquement attribuée**, et cela indépendamment de la qualité de code source ou de la quantité de travail mise estimée.
Il est ultimement de votre responsabilité de tester et valider votre programme.


Quelques exemples de pénalités additionnelles:

* Vous faites une MR sur le dépôt public avec votre code privé : à partir de -10%
* Vous demandez à être membre du dépôt public : -5%
* Si vous critiquez à tort l'infrastructure de test : -10%
* Vous modifiez un fichier autre que le fichier source ou `local.bats` (ou en créez un) sans avoir l’autorisation : à partir de -10%
* Votre dépôt n'est pas un fork de celui-ci : -100%
* Votre dépôt n'est pas privé : -100%
* L'utilisateur @privat n'est pas mainteneur : -100%
* Votre dépôt n'est pas hébergé sur le gitlab départemental : -100%
* Vous faites une remise par courriel : -100%
* Vous utilisez « mais chez-moi ça marche » (ou une variante) comme argument : -100%
* Si je trouve des morceaux de votre code sur le net (même si vous en êtes l'auteur) : -100%
