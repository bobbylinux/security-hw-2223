#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <pthread.h>

void handle_get_request(int client_socket);
void handle_post_request(int client_socket, const char *data);
int start_server(int port);
void *server_thread(void *arg);

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
            printf("1) botnet\n get the list of ip and ports of active bots with specific target id.\n\n2) requests {hostname} {port} {target}\n Send a massive requests to a specified target with specific port through one or more bots.\n Use wildcard * to send request from all botnet.\n Examples:\n  $ requests localhost 5000 *\n  $ requests localhost 5000 1\n\n3) get info {target}\n Receive hardware and software information about hosts connected to this botnet.\n\n");
        }
            // botnet command
        else if (strcmp(command, "botnet\n") == 0) {
//            printBotList();
        }
            // get info command
        else if (strncmp(command, "get info ", 9) == 0) {
            int target = atoi(command + 9); // Estrai l'argomento numerico dopo "get info "

            // Esegui la funzione getInfo con l'ID del target
//            getInfo(target);
        }
            // request command
        else if (strncmp(command, "requests ", 8) == 0) {
//            sendRequest(command);
        }
            // exit command
        else if (strcmp(command, "exit\n") == 0) {
            // Eseguire un'azione di uscita o terminare il server
            exit(0);
        }
    }

    // Attendere il thread del server per terminare e ottenere il risultato
    int *result;
    if (pthread_join(server_thread_id, (void **)&result) != 0) {
        perror("Error joining server thread");
        return 1;
    }

    return *result;
}

// Funzione del thread per la gestione del server e delle richieste
void *server_thread(void *arg) {
    int port = *((int *)arg);
    int result = start_server(port);
    pthread_exit((void *)&result); // Passa l'intero direttamente senza cast
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
    // Gestisci la richiesta POST qui
    // Puoi analizzare i dati POST e inviare una risposta al client utilizzando 'send'
    char response[] = "HTTP/1.1 200 OK\r\n"
                      "Content-Type: text/plain\r\n\r\n"
                      "POST request handled successfully!\r\n";
    send(client_socket, response, strlen(response), 0);
}
