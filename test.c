#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>

int findListeningPorts(int *listeningPorts, int maxPorts) {
    int sockfd;
    struct sockaddr_in server_addr;
    socklen_t addr_len = sizeof(struct sockaddr_in);
    int numListeningPorts = 0;

    // Crea un socket IPv4
    sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd == -1) {
        perror("Errore nella creazione del socket");
        return -1;  // Segnala un errore
    }

    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = 0;  // Indica di cercare tutte le porte in ascolto

    // Associa il socket a una porta locale
    if (bind(sockfd, (struct sockaddr *)&server_addr, sizeof(server_addr)) == -1) {
        perror("Errore nell'associazione del socket");
        close(sockfd);
        return -1;  // Segnala un errore
    }

    // Ottieni la porta locale associata (la porta effettivamente assegnata dal sistema)
    if (getsockname(sockfd, (struct sockaddr *)&server_addr, &addr_len) == -1) {
        perror("Errore nell'ottenere la porta locale");
        close(sockfd);
        return -1;  // Segnala un errore
    }

    // Restituisci la porta assegnata
    listeningPorts[numListeningPorts++] = ntohs(server_addr.sin_port);

    // Chiudi il socket
    close(sockfd);

    return numListeningPorts;  // Restituisce il numero di porte in ascolto trovate
}


int main() {
    int maxPorts = 1024;  // Numero massimo di porte in ascolto che desideri trovare
    int listeningPorts[maxPorts];  // Array per le porte in ascolto

    // Chiama la funzione per ottenere le porte in ascolto
    int numListeningPorts = findListeningPorts(listeningPorts, maxPorts);

    if (numListeningPorts == -1) {
        printf("Errore nella ricerca delle porte in ascolto.\n");
        return 1;
    }

    printf("Porte in ascolto trovate: %d\n", numListeningPorts);
    for (int i = 0; i < numListeningPorts; i++) {
        printf("Porta %d: %d\n", i + 1, listeningPorts[i]);
    }

    return 0;
}