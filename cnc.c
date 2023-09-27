#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include "cJSON.h"

#define PORT 8081
#define MAX_REQUEST_SIZE 1024

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

void handle_post_request(int client_socket, char *data) {
    printf("Received POST data: %s\n", data);

    cJSON *json = cJSON_Parse(data);
    if (json == NULL) {
        char response[] = "HTTP/1.1 400 Bad Request\r\n\r\n";
        send(client_socket, response, strlen(response), 0);
        cJSON_Delete(json);
        return;
    }

    struct Data myData;
    for (int i = 0; i < 20; i++) {
        strcpy(myData.ports[i], "");
    }

    cJSON *clientIPItem = cJSON_GetObjectItem(json, "clientIP");
    cJSON *portsArray = cJSON_GetObjectItem(json, "ports");

    if (!cJSON_HasObjectItem(json, "clientIP") || !cJSON_HasObjectItem(json, "ports") || !cJSON_IsArray(portsArray)) {
        // Se uno dei campi non è presente o "ports" non è un array, gestisci l'errore
        char response[] = "HTTP/1.1 400 Bad Request\r\n\r\n";
        send(client_socket, response, strlen(response), 0);
        cJSON_Delete(json);
        return;
    }

    // Ora puoi accedere in modo sicuro ai campi "clientIP" e "ports"
    strcpy(myData.clientIP, clientIPItem->valuestring);

    // Leggi i dati dall'array "ports"
    for (int i = 0; i < cJSON_GetArraySize(portsArray) && i < 20; i++) {
        cJSON *portItem = cJSON_GetArrayItem(portsArray, i);
        if (cJSON_IsString(portItem)) {
            strncpy(myData.ports[i], portItem->valuestring, sizeof(myData.ports[i]) - 1);
            myData.ports[i][sizeof(myData.ports[i]) - 1] = '\0'; // Assicura la terminazione della stringa
        }
    }

    cJSON_Delete(json);

    // Ora puoi utilizzare i dati nella struttura
    printf("Client IP: %s\n", myData.clientIP);

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
}

int main() {
    int server_socket, client_socket;
    struct sockaddr_in server_addr, client_addr;
    socklen_t client_addr_len = sizeof(client_addr);

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

    printf("Server in ascolto sulla porta %d\n", PORT);

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

    return 0;
}
