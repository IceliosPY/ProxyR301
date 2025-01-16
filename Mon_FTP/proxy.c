#include <stdio.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <netdb.h>
#include <string.h>
#include <unistd.h>
#include <stdbool.h>
#include <errno.h>
#include <sys/time.h>
#include "./simpleSocketAPI.h"

#define SERVADDR       "127.0.0.1"
#define SERVPORT       "0"
#define LISTENLEN      1
#define MAXBUFFERLEN   1024
#define MAXHOSTLEN     64
#define MAXPORTLEN     64

/******************************************************************************
 * Fonctions utilitaires de lecture/écriture avec gestion simple des erreurs.
 ******************************************************************************/

/**
 * Lit des données sur une socket.
 * @param sock       La socket depuis laquelle on lit.
 * @param buffer     Le buffer où stocker les données lues.
 * @param maxLen     La taille maximum du buffer.
 * @return           Le nombre d’octets lus, 0 si fermée, -1 si erreur.
 */
static int read_socket(int sock, char *buffer, size_t maxLen)
{
    int ret = read(sock, buffer, maxLen);
    if (ret < 0) {
        perror("Erreur de lecture sur socket");
    }
    else if (ret == 0) {
        // Socket fermée
        fprintf(stderr, "Socket fermée côté distant.\n");
    }
    return ret;
}

/**
 * Écrit des données sur une socket.
 * @param sock       La socket sur laquelle on écrit.
 * @param buffer     Le buffer à écrire.
 * @param len        La longueur du buffer à écrire.
 * @return           Le nombre d’octets écrits, -1 si erreur.
 */
static int write_socket(int sock, const char *buffer, size_t len)
{
    int ret = write(sock, buffer, len);
    if (ret < 0) {
        perror("Erreur d'écriture sur socket");
    }
    return ret;
}

/******************************************************************************
 * Fonctions auxiliaires de configuration & de connexion
 ******************************************************************************/

/**
 * Crée la socket de rendez-vous (écoute) et la retourne.
 * @return   Le descripteur de socket créée ou -1 si erreur.
 */
static int create_server_socket(void)
{
    // 1) Création de la socket
    int descSockRDV = socket(AF_INET, SOCK_STREAM, 0);
    if (descSockRDV == -1) {
        perror("Erreur création socket RDV");
        return -1;
    }

    // 2) Préparation de hints
    struct addrinfo hints;
    memset(&hints, 0, sizeof(hints));
    hints.ai_flags    = AI_PASSIVE;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_family   = AF_INET;

    // 3) getaddrinfo pour bind
    //permet de faire une structure a l'aide de l'adresse IP et le port pour la fonction BIND
    struct addrinfo *res = NULL;
    int ecode = getaddrinfo(SERVADDR, SERVPORT, &hints, &res);
    if (ecode != 0) {
        fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(ecode));
        close(descSockRDV);
        return -1;
    }

    // 4) bind()
    //sert a lier le client et le server (l'ip et le port)
    ecode = bind(descSockRDV, res->ai_addr, res->ai_addrlen);
    if (ecode == -1) {
        perror("Erreur liaison de la socket de RDV");
        freeaddrinfo(res);
        close(descSockRDV);
        return -1;
    }
    freeaddrinfo(res);

    // 5) listen
    // active le mode ecoute
    ecode = listen(descSockRDV, LISTENLEN);
    if (ecode == -1) {
        perror("Erreur listen()");
        close(descSockRDV);
        return -1;
    }

    return descSockRDV;
}

/**
 * Affiche l'IP et le port effectifs de la socket (si on a bind sur port 0).
 * @param sock  Le descripteur de socket.
 */
static void print_server_info(int sock)
{
    struct sockaddr_storage myinfo;
    socklen_t len = sizeof(myinfo);

    if (getsockname(sock, (struct sockaddr *)&myinfo, &len) == -1) {
        perror("getsockname");
        return;
    }

    char serverAddr[MAXHOSTLEN], serverPort[MAXPORTLEN];
    int ecode = getnameinfo(
        (struct sockaddr *)&myinfo, len,
        serverAddr, MAXHOSTLEN,
        serverPort, MAXPORTLEN,
        NI_NUMERICHOST | NI_NUMERICSERV
    );

    if (ecode != 0) {
        fprintf(stderr, "error in getnameinfo: %s\n", gai_strerror(ecode));
        return;
    }

    printf("L'adresse d'écoute est: %s\n", serverAddr);
    printf("Le port d'écoute est:   %s\n", serverPort);
}

/**
 * Tente de se connecter en mode actif au client FTP (pour la data-connection).
 * @param ipClient   L'adresse IP du client.
 * @param portClient Le port du client.
 * @return           Le descripteur de socket connecté ou -1 si échec.
 */
static int connect_active_mode(const char *ipClient, const char *portClient)
{
    int sock = -1;
    if (connect2Server(ipClient, portClient, &sock) == -1) {
        perror("Connexion mode actif vers le client");
        return -1;
    }
    return sock;
}

/**
 * Tente de se connecter au serveur FTP (ex: sur port 21 ou en mode passif).
 * @param ipServeur   L'adresse IP du serveur.
 * @param portServeur Le port du serveur (ex: "21").
 * @return            Le descripteur de socket connecté ou -1 si échec.
 */
static int connect_ftp_server(const char *ipServeur, const char *portServeur)
{
    int sock = -1;
    if (connect2Server(ipServeur, portServeur, &sock) == -1) {
        perror("Connexion au serveur FTP");
        return -1;
    }
    return sock;
}

/******************************************************************************
 * Gestion d'une session FTP (remplace la fonction `fils()`).
 ******************************************************************************/
static void handle_ftp_session(int descSockCOM)
{
    char buffer[MAXBUFFERLEN];
    int ecode;

    /**** 1) Envoi d'un message de bienvenue ****/
    // ça dit juste bonjour
    strcpy(buffer, "220 Bienvenue au proxy :) \r\n");
    if (write_socket(descSockCOM, buffer, strlen(buffer)) < 0) {
        goto close_client;
    }

    /**** 2) Lecture de "USER login@server" ****/
    //anonymous 
    ecode = read_socket(descSockCOM, buffer, MAXBUFFERLEN - 1);
    if (ecode <= 0) {
        goto close_client;
    }
    buffer[ecode] = '\0';
    printf("Reçu du client ftp: %s\n", buffer);

    // Extraction login et serverName
    // sert a extraire les logins et le nom du server
    char login[50], serverName[50];
    if (sscanf(buffer, "%50[^@]@%50s", login, serverName) != 2) {
        fprintf(stderr, "Format de login@server incorrect.\n");
        goto close_client;
    }
    strncat(login, "\r\n", sizeof(login) - strlen(login) - 1);

    /**** 3) Connexion au serveur FTP distant (port 21) ****/
    // se connecte au serveur
    int sockServerCMD = connect_ftp_server(serverName, "21");
    if (sockServerCMD == -1) {
        goto close_client;
    }

    /**** 4) Lecture de la bannière 220 ****/
    //verifie si le serveur réponds bien en cherchant a lire la banniére, la banniére c'est simplement 
    //le premier message envoyé
    ecode = read_socket(sockServerCMD, buffer, MAXBUFFERLEN - 1);
    if (ecode <= 0) {
        close(sockServerCMD);
        goto close_client;
    }
    buffer[ecode] = '\0';
    printf("Reçu du serveur (bannière): %s\n", buffer);

    /**** 5) Envoi USER login au serveur ****/
    //anonymous
    if (write_socket(sockServerCMD, login, strlen(login)) < 0) {
        close(sockServerCMD);
        goto close_client;
    }

    /**** 6) Lecture reponse 331 (demande PASS) ****/
    // demande le mot de passe, si c'est pas bon, il close le serv et au proxy de fermer le client
    ecode = read_socket(sockServerCMD, buffer, MAXBUFFERLEN - 1);
    if (ecode <= 0) {
        close(sockServerCMD);
        goto close_client;
    }
    buffer[ecode] = '\0';
    printf("Reçu du serveur: %s\n", buffer);

    /**** 7) Renvoi 331 au client ****/
    //teste si la commande a été correctement envoyée
    if (write_socket(descSockCOM, buffer, strlen(buffer)) < 0) {
        close(sockServerCMD);
        goto close_client;
    }

    /**** 8) Lecture PASS depuis le client ****/
    //lis le MODP 
    ecode = read_socket(descSockCOM, buffer, MAXBUFFERLEN - 1);
    if (ecode <= 0) {
        close(sockServerCMD);
        goto close_client;
    }
    buffer[ecode] = '\0';
    printf("Reçu du client ftp (PASS): %s\n", buffer);

    /**** 9) Envoi PASS au serveur ****/
    //sert a transmettre le mot de passe
    if (write_socket(sockServerCMD, buffer, strlen(buffer)) < 0) {
        close(sockServerCMD);
        goto close_client;
    }

    /**** 10) Lecture reponse 230 ****/
    // il tente de lire ce qu'il viens de recevoir 
    ecode = read_socket(sockServerCMD, buffer, MAXBUFFERLEN - 1);
    if (ecode <= 0) {
        close(sockServerCMD);
        goto close_client;
    }
    buffer[ecode] = '\0';
    printf("Reçu du serveur: %s\n", buffer);

    /**** 11) Renvoi 230 au client ****/
    // test d'écriture 
    if (write_socket(descSockCOM, buffer, strlen(buffer)) < 0) {
        close(sockServerCMD);
        goto close_client;
    }

    /**** 12) Lecture commande SYST ****/
    // lire la commande SYST
    ecode = read_socket(descSockCOM, buffer, MAXBUFFERLEN - 1);
    if (ecode <= 0) {
        close(sockServerCMD);
        goto close_client;
    }
    buffer[ecode] = '\0';
    printf("Reçu du client ftp (SYST): %s\n", buffer);

    /**** 13) Envoi SYST au serveur ****/ 
    // il envoie les infos de l'os au serveur 
    if (write_socket(sockServerCMD, buffer, strlen(buffer)) < 0) {
        close(sockServerCMD);
        goto close_client;
    }

    /**** 14) Lecture 215 ****/
    ecode = read_socket(sockServerCMD, buffer, MAXBUFFERLEN - 1);
    if (ecode <= 0) {
        close(sockServerCMD);
        goto close_client;
    }
    buffer[ecode] = '\0';
    printf("Reçu du serveur (215): %s\n", buffer);

    /**** 15) Renvoi 215 au client ****/
    if (write_socket(descSockCOM, buffer, strlen(buffer)) < 0) {
        close(sockServerCMD);
        goto close_client;
    }

    /**** 16) Lecture PORT x,x,x,x,x,x ****/
    ecode = read_socket(descSockCOM, buffer, MAXBUFFERLEN - 1);
    if (ecode <= 0) {
        close(sockServerCMD);
        goto close_client;
    }
    buffer[ecode] = '\0';
    printf("Reçu du client ftp (PORT): %s\n", buffer);

    // Découpage du PORT pour mode actif
    int n1, n2, n3, n4, n5, n6;
    if (sscanf(buffer, "PORT %d,%d,%d,%d,%d,%d", 
               &n1, &n2, &n3, &n4, &n5, &n6) != 6) {
        fprintf(stderr, "Commande PORT mal formatée.\n");  // DECOMPOSITION DES PORTS
        close(sockServerCMD);
        goto close_client;
    }
    char ipClient[50], portClient[10];
    sprintf(ipClient, "%d.%d.%d.%d", n1, n2, n3, n4);
    sprintf(portClient, "%d", (n5 * 256) + n6);

    /**** 17) Connexion en mode actif au client ****/
    int actif = connect_active_mode(ipClient, portClient);
    if (actif == -1) {
        close(sockServerCMD);
        goto close_client;
    }

    /**** 18) Envoi PASV au serveur (pour la data-connection côté serveur) ****/
    if (write_socket(sockServerCMD, "PASV\r\n", strlen("PASV\r\n")) < 0) {
        close(sockServerCMD);
        close(actif);
        goto close_client;
    }

    /**** 19) Lecture 227 (mode passif) du serveur ****/
    ecode = read_socket(sockServerCMD, buffer, MAXBUFFERLEN - 1);
    if (ecode <= 0) {
        close(sockServerCMD);
        close(actif);
        goto close_client;
    }
    buffer[ecode] = '\0';
    printf("Reçu du serveur (227): %s\n", buffer);

    // Extraction IP et port du serveur passif
    if (sscanf(buffer, "227 Entering Passive Mode (%d,%d,%d,%d,%d,%d)",
               &n1, &n2, &n3, &n4, &n5, &n6) != 6) {
        fprintf(stderr, "Réponse PASV mal formatée.\n");
        close(sockServerCMD);
        close(actif);
        goto close_client;
    }
    char ipServeur[50], portServeur[10];
    sprintf(ipServeur, "%d.%d.%d.%d", n1, n2, n3, n4);
    sprintf(portServeur, "%d", (n5 * 256) + n6);

    /**** 20) Connexion au serveur en mode passif ****/
    // sert a se connecter en passif via le FTP 
    int passif = connect_ftp_server(ipServeur, portServeur);
    if (passif == -1) {
        close(sockServerCMD);
        close(actif); 
        goto close_client;
    }


    // ----------------------------
    //  TIMEOUT sur la socket passif (tests, 5 s)
    // ----------------------------
    {
        struct timeval tv;
        tv.tv_sec  = 5;
        tv.tv_usec = 0;
        if (setsockopt(passif, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv)) < 0) {
            perror("setsockopt (SO_RCVTIMEO) a échoué");
        }
    }


    /**** 21) Envoi de 200 (OK) au client pour confirmer le PORT ****/
    // verifie si l'écriture a reussie 
    if (write_socket(descSockCOM, "200 PORT Command successful\r\n",
                     strlen("200 PORT Command successful\r\n")) < 0) {
        close(sockServerCMD);
        close(actif);
        close(passif);
        goto close_client;
    }

    /**** 22) Lecture commande LIST du client ****/
    // vérifie si la lecture a reussie 
    ecode = read_socket(descSockCOM, buffer, MAXBUFFERLEN - 1);
    if (ecode <= 0) {
        close(sockServerCMD);
        close(actif);
        close(passif);
        goto close_client;
    }
    buffer[ecode] = '\0';
    printf("Reçu du client ftp (LIST): %s\n", buffer);

    /**** 23) Envoi LIST au serveur ****/
    //teste si l'écriture reussie ou échoue
    if (write_socket(sockServerCMD, buffer, strlen(buffer)) < 0) {
        close(sockServerCMD);
        close(actif);
        close(passif);
        goto close_client;
    }

    /**** 24) Lecture 150 du serveur ****/
    // grossiérement, c'est un test pour voir si l'envoi a bien été effectué
    ecode = read_socket(sockServerCMD, buffer, MAXBUFFERLEN - 1);
    if (ecode <= 0) {
        close(sockServerCMD);
        close(actif);
        close(passif);
        goto close_client;
    }
    buffer[ecode] = '\0';
    printf("Reçu du serveur (150): %s\n", buffer);

    /**** 25) Envoi 150 au client ****/
    // grossiérement, c'est un test pour voir si l'envoi a bien été effectué
    if (write_socket(descSockCOM, buffer, strlen(buffer)) < 0) {
        close(sockServerCMD);
        close(actif);
        close(passif);
        goto close_client;
    }
    // LS
    /**** 26) Transfert de données : serveur(passif) => proxy => client(actif) ****/
    printf("----- Début du listing reçu du serveur -----\n");
    while ((ecode = read_socket(passif, buffer, MAXBUFFERLEN - 1)) > 0) {
        buffer[ecode] = '\0';
        printf("%s", buffer);   // On log le listing côté proxy

        // On transfère les données lues sur la socket "passif" vers la socket "actif"
        if (write_socket(actif, buffer, ecode) < 0) {
            break; // on arrête si problème d'écriture
        }
    }



    if (ecode < 0) { //sert a différencier un timeout d'une véritable erreur
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            fprintf(stderr, "\nTimeout atteint, on quitte la lecture data.\n");
        } else {    
            perror("\nErreur de lecture sur la socket passif");
        }
    }



    // On ferme les sockets data
    close(actif);
    close(passif);

    /**** 27) Lecture 226 du serveur (fin de transfert) ****/
    ecode = read_socket(sockServerCMD, buffer, MAXBUFFERLEN - 1);
    // ecode = variable de stockage utilisée pour a peu prés tout
    if (ecode <= 0) {
        close(sockServerCMD);
        goto close_client;
    }
    buffer[ecode] = '\0';
    printf("\nReçu du serveur: %s\n", buffer);

    /**** 28) Renvoi 226 (ou autre code) au client ****/
    if (write_socket(descSockCOM, buffer, strlen(buffer)) < 0) {
        close(sockServerCMD);
        goto close_client;
    }

    /**** 29) Fermeture socket serveur ****/
    close(sockServerCMD);

close_client:
    /**** 30) Fermeture socket client ****/
    close(descSockCOM);
    // Fin de la fonction
}

/******************************************************************************
 * MAIN
 ******************************************************************************/
int main(void)
{
    // Création et configuration de la socket serveur
    int descSockRDV = create_server_socket();
    if (descSockRDV == -1) {
        exit(EXIT_FAILURE);
    }

    // Affiche l'IP et le port effectifs (si port=0)
    print_server_info(descSockRDV);

    // Boucle infinie d’acceptation
    while (true) {
        struct sockaddr_storage from;
        socklen_t len = sizeof(from);

        int descSockCOM = accept(descSockRDV, (struct sockaddr *)&from, &len);
        if (descSockCOM == -1) {
            perror("Erreur accept()");
            continue; // On continue si erreur d'accept pour ne pas tuer le serveur
        }

        // Gestion du client dans une fonction dédiée
        handle_ftp_session(descSockCOM);
        // (descSockCOM sera fermé dans handle_ftp_session())
    }

    // Normalement jamais atteint (boucle infinie)
    close(descSockRDV);
    return 0;
}
