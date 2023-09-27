#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <curl/curl.h>
#include "cJSON.h"
#include <ifaddrs.h>
#include <arpa/inet.h>

#define MAX_LISTENING_PORTS 1024
#define JSON_BUFFER_SIZE 1024

struct NetworkInterface {
    char name[256];
    char ip[INET_ADDRSTRLEN];
};

struct ServerInfo {
    char ip[INET_ADDRSTRLEN];
    int ports[MAX_LISTENING_PORTS];
    int numPorts;
};

// Dichiarazione delle funzioni
int findListeningPorts(int *freePorts, int maxPorts) ;
int findWebExposedInterface(struct NetworkInterface *webInterface);
int generateRandomPort(const int *listeningPorts, int numListeningPorts);
int sendServerInfo(struct ServerInfo *info, const char *serverURL);
int isPortInList(const int *listeningPorts, int numListeningPorts, int port);
int startServer(int port);
int handlePostRequest(int clientSocket);

int main(int argc, char *argv[]) {
    if (argc != 2) {
        fprintf(stderr, "Usage: %s <server_url>\n", argv[0]);
        return 1;
    }

    const char *serverURL = argv[1];  // Ottieni l'URL del server HTTP dai parametri

    int freePorts[MAX_LISTENING_PORTS];
    struct NetworkInterface webInterface;
    struct ServerInfo serverInfo;

    int numListeningPorts = findListeningPorts(freePorts, MAX_LISTENING_PORTS);
    if (numListeningPorts == -1) {
        fprintf(stderr, "Errore durante la ricerca delle porte in ascolto.\n");
        return 1;
    }

    int result = findWebExposedInterface(&webInterface);
    if (result == -1) {
        fprintf(stderr, "Errore durante la ricerca dell'interfaccia esposta sul web.\n");
        return 1;
    }

    int randomPort = generateRandomPort(freePorts, numListeningPorts);
    if (randomPort == -1) {
        fprintf(stderr, "Nessuna porta libera disponibile.\n");
        return 1;
    }

    // Creare la struttura JSON con le informazioni
    snprintf(serverInfo.ip, sizeof(serverInfo.ip), "%s", webInterface.ip);
    serverInfo.numPorts = 1;
    serverInfo.ports[0] = randomPort;


    // Invia le informazioni al server HTTP utilizzando l'URL passato come argomento
    result = sendServerInfo(&serverInfo, serverURL);
    if (result == -1) {
        fprintf(stderr, "Errore nell'invio delle informazioni al server HTTP.\n");
        return 1;
    }

    // Avvia il server sulla porta specificata nel JSON
    if (serverInfo.numPorts > 0) {
        int serverPort = serverInfo.ports[0]; // Assume che ci sia almeno una porta nell'array
        result = startServer(serverPort);
        if (result == -1) {
            fprintf(stderr, "Errore nell'avvio del server sulla porta %d\n", serverPort);
            return 1;
        }
    }

    return 0;
}

int findListeningPorts(int *freePorts, int maxPorts) {
    int numFreePorts = 0;

    for (int port = 1024; port <= 65535 && numFreePorts < maxPorts; ++port) {
        int sockfd = socket(AF_INET, SOCK_STREAM, 0);
        if (sockfd == -1) {
            perror("Errore nella creazione del socket");
            return -1;  // Segnala un errore
        }

        struct sockaddr_in server_addr;
        memset(&server_addr, 0, sizeof(server_addr));
        server_addr.sin_family = AF_INET;
        server_addr.sin_addr.s_addr = INADDR_ANY;
        server_addr.sin_port = htons(port);

        if (bind(sockfd, (struct sockaddr *)&server_addr, sizeof(server_addr)) == 0) {
            // La porta è libera
            freePorts[numFreePorts++] = port;
        }

        close(sockfd);
    }

    return numFreePorts;
}


int findWebExposedInterface(struct NetworkInterface *webInterface) {
    struct ifaddrs *ifaddr, *ifa;

    if (getifaddrs(&ifaddr) == -1) {
        perror("Errore durante la ricerca delle interfacce di rete");
        return -1;
    }

    for (ifa = ifaddr; ifa != NULL; ifa = ifa->ifa_next) {
        if (ifa->ifa_addr == NULL) {
            continue;
        }

        // Verifica se l'interfaccia ha un indirizzo IP IPv4
        if (ifa->ifa_addr->sa_family == AF_INET) {
            struct sockaddr_in *addr = (struct sockaddr_in *)ifa->ifa_addr;
            char ip[INET_ADDRSTRLEN];
            inet_ntop(AF_INET, &(addr->sin_addr), ip, INET_ADDRSTRLEN);

            CURL *curl = curl_easy_init();
            if (curl) {
                const char *testURL = "http://www.google.com";
                curl_easy_setopt(curl, CURLOPT_URL, testURL);
                curl_easy_setopt(curl, CURLOPT_NOBODY, 1L);
                curl_easy_setopt(curl, CURLOPT_INTERFACE, ifa->ifa_name);
                curl_easy_setopt(curl, CURLOPT_TIMEOUT, 10L); // Imposta un timeout di 10 secondi

                CURLcode res = curl_easy_perform(curl);

                if (res == CURLE_OK) {
                    // Questa interfaccia ha connessione a Internet
                    strncpy(webInterface->name, ifa->ifa_name, sizeof(webInterface->name));
                    strncpy(webInterface->ip, ip, sizeof(webInterface->ip));
                    freeifaddrs(ifaddr); // Libera la memoria allocata da getifaddrs
                    curl_easy_cleanup(curl);
                    return 0; // Successo
                }

                curl_easy_cleanup(curl);
            }
        }
    }

    freeifaddrs(ifaddr); // Libera la memoria allocata da getifaddrs
    return -1; // Nessuna interfaccia esposta sul web trovata
}

int isPortInList(const int *listeningPorts, int numListeningPorts, int port) {
    for (int i = 0; i < numListeningPorts; i++) {
        if (listeningPorts[i] == port) {
            return 1;  // La porta è nella lista
        }
    }
    return 0;  // La porta non è nella lista
}

int generateRandomPort(const int *listeningPorts, int numListeningPorts) {
    srand(time(NULL));  // Inizializza il generatore di numeri casuali con il tempo corrente

    int maxPort = 65535;  // La porta massima consentita
    int minPort = 1024;   // La porta minima consentita (puoi regolare questa soglia)

    int randomPort;
    int attempts = 0;

    do {
        // Genera una porta casuale tra minPort e maxPort
        randomPort = minPort + (rand() % (maxPort - minPort + 1));

        // Verifica se la porta è nella lista delle porte in ascolto
        if (!isPortInList(listeningPorts, numListeningPorts, randomPort)) {
            return randomPort;  // Restituisci la porta casuale
        }

        attempts++;
    } while (attempts < 100);  // Limita il numero di tentativi

    return -1;  // Nessuna porta libera trovata dopo 100 tentativi
}

size_t write_callback(void *contents, size_t size, size_t nmemb, void *userp) {
    // Callback per gestire la risposta HTTP (puoi personalizzarla in base alle tue esigenze)
    // In questo esempio, la risposta viene ignorata
    return size * nmemb;
}

int sendServerInfo(struct ServerInfo *info, const char *serverURL) {
    CURL *curl;
    CURLcode res;

    curl = curl_easy_init();
    if (!curl) {
        fprintf(stderr, "Errore nell'inizializzazione di libcurl.\n");
        return -1;
    }

    // Imposta l'URL del server remoto
    curl_easy_setopt(curl, CURLOPT_URL, serverURL);

    // Imposta l'invio dei dati tramite POST
    curl_easy_setopt(curl, CURLOPT_POST, 1);

    // Crea un oggetto JSON con l'IP e l'array di porte selezionate
    char json_data[JSON_BUFFER_SIZE];  // Dimensione arbitraria
    snprintf(json_data, sizeof(json_data), "{\"clientIP\":\"%s\",\"ports\":[", info->ip);

    // Aggiungi le porte libere all'array JSON
    for (int i = 0; i < info->numPorts; i++) {
        char portStr[16];
        snprintf(portStr, sizeof(portStr), "%d", info->ports[i]);
        strcat(json_data, portStr);

        if (i < info->numPorts - 1) {
            strcat(json_data, ",");
        }
    }

    strcat(json_data, "]}");

    // Imposta i dati da inviare
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, json_data);

    // Imposta la funzione di callback per la gestione della risposta
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_callback);

    // Esegui la richiesta HTTP POST
    res = curl_easy_perform(curl);

    if (res != CURLE_OK) {
        fprintf(stderr, "Errore nell'esecuzione della richiesta HTTP POST: %s\n", curl_easy_strerror(res));
        curl_easy_cleanup(curl);
        return -1;
    }

    // Cleanup di libcurl
    curl_easy_cleanup(curl);

    return 0;  // Successo nell'invio delle informazioni al server HTTP
}


int startServer(int port) {
    int sockfd, newsockfd;
    socklen_t clilen;
    struct sockaddr_in serv_addr, cli_addr;

    // Crea un socket
    sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd == -1) {
        perror("Errore nella creazione del socket");
        return -1;
    }

    // Inizializza la struttura dell'indirizzo del server
    memset(&serv_addr, 0, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_addr.s_addr = INADDR_ANY;
    serv_addr.sin_port = htons(port);

    // Collega il socket all'indirizzo del server
    if (bind(sockfd, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) == -1) {
        perror("Errore nella bind");
        return -1;
    }

    // Inizia ad ascoltare sul socket
    if (listen(sockfd, 5) == -1) {
        perror("Errore nell'ascolto");
        return -1;
    }

    printf("Server socket in ascolto sulla porta %d\n", port);

    while (1) {
        // Accetta le connessioni in arrivo
        clilen = sizeof(cli_addr);
        newsockfd = accept(sockfd, (struct sockaddr *)&cli_addr, &clilen);
        if (newsockfd == -1) {
            perror("Errore nell'accettare la connessione");
            return -1;
        }

        // Gestisci la richiesta POST
        if (handlePostRequest(newsockfd) == -1) {
            fprintf(stderr, "Errore nella gestione della richiesta POST\n");
        }

        // Chiudi il socket per questa connessione
        close(newsockfd);
    }

    // Chiudi il socket del server (questa parte verrà eseguita solo alla chiusura del server)
    close(sockfd);

    return 0;
}


int handlePostRequest(int clientSocket) {
    char buffer[1024];
    memset(buffer, 0, sizeof(buffer));

    // Leggi i dati inviati dal client (richiesta POST)
    ssize_t bytes_read = recv(clientSocket, buffer, sizeof(buffer), 0);

    if (bytes_read == -1) {
        perror("Errore nella lettura della richiesta");
        return -1;
    }

    // Analizza il JSON utilizzando libcjson
    cJSON *root = cJSON_Parse(buffer);
    if (!root) {
        fprintf(stderr, "Errore nel parsing del JSON\n");
        return -1;
    }

    // Estrai il campo "command" dal JSON
    cJSON *command = cJSON_GetObjectItemCaseSensitive(root, "command");
    if (!cJSON_IsString(command)) {
        fprintf(stderr, "Il campo 'command' non è una stringa\n");
        cJSON_Delete(root);
        return -1;
    }

    const char *commandStr = command->valuestring;
    printf("Comando ricevuto: %s\n", commandStr);

    // Puoi eseguire il comando qui

    // Invia una risposta al client
    const char *response = "HTTP/1.1 200 OK\r\nContent-Length: 12\r\n\r\nHello, World!";
    send(clientSocket, response, strlen(response), 0);

    // Rilascia la memoria allocata per il JSON
    cJSON_Delete(root);

    return 0;
}