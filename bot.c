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
#include <errno.h>

#define MAX_LISTENING_PORTS 1024
#define JSON_BUFFER_SIZE 1024
#define MAX_REQUEST_SIZE 1024

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
int findListeningPorts(int *freePorts, int maxPorts);

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

        if (bind(sockfd, (struct sockaddr *) &server_addr, sizeof(server_addr)) == 0) {
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
            struct sockaddr_in *addr = (struct sockaddr_in *) ifa->ifa_addr;
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
    if (bind(sockfd, (struct sockaddr *) &serv_addr, sizeof(serv_addr)) == -1) {
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
        newsockfd = accept(sockfd, (struct sockaddr *) &cli_addr, &clilen);
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

cJSON *buildInfoJson() {
    cJSON *responseJson = cJSON_CreateObject();

    // Esegui il comando Linux e ottieni i risultati
    char result[512];

    // Esegui il comando lscpu e ottieni il risultato
    FILE *lscpuFile = popen("lscpu", "r");
    if (lscpuFile) {
        fgets(result, sizeof(result), lscpuFile);
        cJSON_AddStringToObject(responseJson, "infoCPU", result);
        pclose(lscpuFile);
    }

    // Esegui il comando free e ottieni il risultato
    FILE *freeFile = popen("free", "r");
    if (freeFile) {
        fgets(result, sizeof(result), freeFile);
        cJSON_AddStringToObject(responseJson, "infoRAM", result);
        pclose(freeFile);
    }

    // Esegui il comando df -h e ottieni il risultato
    FILE *dfFile = popen("df -h", "r");
    if (dfFile) {
        fgets(result, sizeof(result), dfFile);
        cJSON_AddStringToObject(responseJson, "infoHD", result);
        pclose(dfFile);
    }

    // Esegui il comando uname e ottieni il risultato
    FILE *unameFile = popen("uname", "r");
    if (unameFile) {
        fgets(result, sizeof(result), unameFile);
        cJSON_AddStringToObject(responseJson, "infoOS", result);
        pclose(unameFile);
    }

    return responseJson;
}

cJSON *getInfo() {
    cJSON *infoJson = cJSON_CreateObject();

    // Esegui il comando completo "lscpu" e ottieni l'output
    FILE *lscpuFile = popen("lscpu", "r");
    if (lscpuFile) {
        char result[512];
        char lscpuOutput[4096] = ""; // Aumentato il buffer
        while (fgets(result, sizeof(result), lscpuFile) != NULL) {
            strcat(lscpuOutput, result);
        }
        cJSON_AddStringToObject(infoJson, "infoCPU", lscpuOutput);
        pclose(lscpuFile);
    }

    // Esegui il comando completo "free" e ottieni l'output
    FILE *freeFile = popen("free", "r");
    if (freeFile) {
        char result[512];
        char freeOutput[4096] = ""; // Aumentato il buffer
        while (fgets(result, sizeof(result), freeFile) != NULL) {
            strcat(freeOutput, result);
        }
        cJSON_AddStringToObject(infoJson, "infoRAM", freeOutput);
        pclose(freeFile);
    }

    // Esegui il comando completo "df -h" e ottieni l'output
    FILE *dfFile = popen("df -h", "r");
    if (dfFile) {
        char result[512];
        char dfOutput[4096] = ""; // Aumentato il buffer
        while (fgets(result, sizeof(result), dfFile) != NULL) {
            strcat(dfOutput, result);
        }
        cJSON_AddStringToObject(infoJson, "infoHD", dfOutput);
        pclose(dfFile);
    }

    // Esegui il comando completo "uname -a" e ottieni l'output
    FILE *unameFile = popen("uname -a", "r");
    if (unameFile) {
        char result[512];
        char unameOutput[4096] = ""; // Aumentato il buffer
        while (fgets(result, sizeof(result), unameFile) != NULL) {
            strcat(unameOutput, result);
        }
        cJSON_AddStringToObject(infoJson, "infoOS", unameOutput);
        pclose(unameFile);
    }

    // Esegui il comando completo "ifconfig" e ottieni l'output
    FILE *ifconfigFile = popen("ifconfig", "r");
    if (ifconfigFile) {
        char result[512];
        char ifconfigOutput[4096] = ""; // Aumentato il buffer
        while (fgets(result, sizeof(result), ifconfigFile) != NULL) {
            strcat(ifconfigOutput, result);
        }
        cJSON_AddStringToObject(infoJson, "infoNetwork", ifconfigOutput);
        pclose(ifconfigFile);
    }

    return infoJson;
}

int sendGetRequestToTarget(cJSON *json) {
    cJSON *hostnameItem = cJSON_GetObjectItem(json, "hostname");
    cJSON *portItem = cJSON_GetObjectItem(json, "port");

    if (hostnameItem != NULL && portItem != NULL) {
        const char *hostname = hostnameItem->valuestring;
        const char *port = portItem->valuestring;
        printf("hostname %s\n", hostname);
        printf("port %s\n", port);
        // Crea l'URL con l'hostname e la porta specificati
        char url[100];
        snprintf(url, sizeof(url), "http://%s:%s", hostname, port);

        // Esegui la richiesta HTTP GET all'URL
        CURL *curl;
        CURLcode res;

        curl_global_init(CURL_GLOBAL_DEFAULT);

        curl = curl_easy_init();
        if (curl) {
            // Imposta l'URL
            curl_easy_setopt(curl, CURLOPT_URL, url);

            // Imposta la richiesta HTTP come GET
            curl_easy_setopt(curl, CURLOPT_HTTPGET, 1);

            // Esegui la richiesta GET
            res = curl_easy_perform(curl);
            if (res != CURLE_OK) {
                fprintf(stderr, "curl_easy_perform() failed: %s\n", curl_easy_strerror(res));
                return 0;
            } else {
                printf("Sent GET request to %s\n", url);
            }

            // Chiudi la sessione CURL
            curl_easy_cleanup(curl);
        }

        curl_global_cleanup();
    } else {
        return 0;
    }

    return 1;
}
int handlePostRequest(int clientSocket) {
    char buffer[MAX_REQUEST_SIZE];
    ssize_t bytesRead;

    memset(buffer, 0, sizeof(buffer));

    // Leggi i dati inviati dal client (richiesta POST)
    bytesRead = recv(clientSocket, buffer, sizeof(buffer), 0);

    if (bytesRead == -1) {
        perror("Errore nella lettura della richiesta del client");
        return -1;
    }

    // Trova i dati POST (supponendo una richiesta semplice senza chunking)
    char *data = strstr(buffer, "\r\n\r\n");
    if (data == NULL) {
        fprintf(stderr, "Errore nei dati POST: dati mancanti o formato non supportato\n");
        return -1;
    }

    data += 4; // Avanza oltre la sequenza "\r\n\r\n"

    // Analizza il JSON utilizzando libcjson
    cJSON *json = cJSON_Parse(data);
    if (json == NULL) {
        fprintf(stderr, "Errore nel parsing del JSON\n");
        // Invia una risposta con codice di stato 500
        char response[] = "HTTP/1.1 500 Internal Server Error\r\n\r\n";
        send(clientSocket, response, strlen(response), 0);
        return -1;
    }

    cJSON *commandItem = cJSON_GetObjectItem(json, "command");
    if (!cJSON_IsString(commandItem)) {
        fprintf(stderr, "Errore: campo 'command' non presente o non è una stringa\n");
        cJSON_Delete(json);
        // Invia una risposta con codice di stato 500
        char response[] = "HTTP/1.1 500 Internal Server Error\r\n\r\n";
        send(clientSocket, response, strlen(response), 0);
        return -1;
    }

    const char *command = commandItem->valuestring;

    cJSON *responseJson = cJSON_CreateObject();

    // Esegui il comando Linux specificato nel campo "command"
    if (strcmp(command, "get info") == 0) {
        cJSON *infoJson = getInfo();
        cJSON_AddItemToObject(responseJson, "infoCPU", cJSON_Duplicate(cJSON_GetObjectItem(infoJson, "infoCPU"), 1));
        cJSON_AddItemToObject(responseJson, "infoRAM", cJSON_Duplicate(cJSON_GetObjectItem(infoJson, "infoRAM"), 1));
        cJSON_AddItemToObject(responseJson, "infoHD", cJSON_Duplicate(cJSON_GetObjectItem(infoJson, "infoHD"), 1));
        cJSON_AddItemToObject(responseJson, "infoOS", cJSON_Duplicate(cJSON_GetObjectItem(infoJson, "infoOS"), 1));
        cJSON_AddItemToObject(responseJson, "infoNetwork",
                              cJSON_Duplicate(cJSON_GetObjectItem(infoJson, "infoNetwork"), 1));
        cJSON_Delete(infoJson);
    } else if (strcmp(command, "request") == 0) {
        if (sendGetRequestToTarget(json) == 0) {
            cJSON_AddStringToObject(responseJson, "error", "Parametri JSON 'hostname' o 'port' mancanti o non validi");
            // Invia una risposta con codice di stato 500
            char response[] = "HTTP/1.1 500 Internal Server Error\r\n\r\n";
            send(clientSocket, response, strlen(response), 0);
        }
    } else {
        cJSON_AddStringToObject(responseJson, "error", "Comando sconosciuto");
        // Invia una risposta con codice di stato 500
        char response[] = "HTTP/1.1 500 Internal Server Error\r\n\r\n";
        send(clientSocket, response, strlen(response), 0);
    }

    // Converti il JSON di risposta in una stringa
    char *responseString = cJSON_Print(responseJson);

    // Invia l'intestazione HTTP con Content-Type
    char responseHeader[256];
    snprintf(responseHeader, sizeof(responseHeader),
             "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nContent-Length: %zu\r\n\r\n",
             strlen(responseString));

    send(clientSocket, responseHeader, strlen(responseHeader), 0);

    // Invia il corpo della risposta JSON
    send(clientSocket, responseString, strlen(responseString), 0);

    // Liberare la memoria allocata per il JSON di risposta e la stringa
    cJSON_Delete(json);
    cJSON_Delete(responseJson);
    free(responseString);

    return 0;
}
