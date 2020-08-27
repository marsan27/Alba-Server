#include <json-c/json.h>
#include <netdb.h>
#include <netinet/in.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "database.h"

void signalHandler() {
    printf("Process exited by signal handler\n");
    exit(0);
}

json_object* create_custom_json(struct resultStringArray resultArray) {
    json_object* jobj = json_object_new_object();
    json_object* jstring0 = json_object_new_string(resultArray.contentArray[0]);
    json_object* jstring1 = json_object_new_string(resultArray.contentArray[1]);
    json_object* jstring2 = json_object_new_string(resultArray.contentArray[2]);

    json_object_object_add(jobj, "Date", jstring0);
    json_object_object_add(jobj, "Content", jstring1);
    json_object_object_add(jobj, "User", jstring2);
    printf("The json object created: %s\n", json_object_to_json_string(jobj));

    return jobj;
}

void* reading_thread_routine(void* params) {
    int sock = *((int*)params);
    printf("Reader thread received %i as parameter\n");

    char* line = NULL;
    size_t len = 0;
    ssize_t read;
    FILE* fp = fdopen(sock, "r");
    if (fp == NULL) {
        perror("Couldn't open file pointer");
    }
    while (1) {
        printf("Attempting to read...\n");
        while ((read = getline(&line, &len, fp)) != -1) {
            printf("Retrieved line of length %zu:\n", read);
            printf("Message received from client: %s\n", line);
        }
        sleep(5);
        
    }

    printf("Finished reader thread\n");
}

void send_init_connection_message(int sock) {
    int fillerArrayLength = 1024 * sizeof(char);
    char* fillerArray = malloc(fillerArrayLength);
    memset(fillerArray, 0, fillerArrayLength);
    printf("Called init connection message\n");

    char* header = "STARTCONN::";
    char* separator = "\n##ALBA##\n";
    int totalLength = strlen(header) + strlen(separator) + fillerArrayLength + 1;
    char* to_send = calloc(strlen(header) + strlen(separator) + fillerArrayLength + 1, sizeof(char));
    strcpy(to_send, header);
    strcat(to_send, separator);
    strcat(to_send, fillerArray);
    printf("Total length is %i\n", totalLength);

    int n = write(sock, to_send, 1024);
    printf("Sending STARTCONN message\n");
    if (n < 0) {
        perror("ERROR writing to socket");
        exit(1);
    }
    free(fillerArray);
    free(to_send);
}

void send_end_connection_message(int sock) {
    int fillerArrayLength = 1024 * sizeof(char);
    char* fillerArray = malloc(fillerArrayLength);
    memset(fillerArray, 0, fillerArrayLength);
    printf("Called end connection message\n");

    char* header = "ENDCONN::";
    char* separator = "\n##ALBA##\n";
    int totalLength = strlen(header) + strlen(separator) + fillerArrayLength + 1;
    char* to_send = calloc(strlen(header) + strlen(separator) + fillerArrayLength + 1, sizeof(char));
    strcpy(to_send, header);
    strcat(to_send, separator);
    strcat(to_send, fillerArray);
    printf("Total length is %i\n", totalLength);

    int n = write(sock, to_send, 1024);
    printf("Sending ENDCONN message\n");
    if (n < 0) {
        perror("ERROR writing to socket");
        exit(1);
    }
    free(fillerArray);
    free(to_send);
}

void doprocessing(int sock) {
    pthread_t reading_thread;  //We need another thread to process the data sent by the client.
    int n;
    char* fillerArray = malloc(1024);
    bzero(fillerArray, 1024);
    pthread_create(&reading_thread, NULL, reading_thread_routine, &sock);

    printf("Starting processing\n");

    connectDB();
    struct resultStringArray result = getLastRow();

    send_init_connection_message(sock);  //Announcing start of message to the client

    json_object* jobj = create_custom_json(result);
    char* jobjstr = (char*)json_object_to_json_string_ext(jobj, JSON_C_TO_STRING_PRETTY);
    printf("Received JSON string was: %s\n", jobjstr);
    char* header = "INCLUDE::";
    char* delimitor = "\n##ALBA##\n";
    char* to_send = calloc(strlen(jobjstr) + strlen(header) + strlen(delimitor) + sizeof(fillerArray) + 1, sizeof(char));

    //Now that we have the complete message, we proceed to adjust it to the protocol.
    char* token = strtok(jobjstr, "\n");
    int ii = 0;
    while (token != NULL) {
        printf("Part %i is: %s\n", ii, token);

        //First, the header
        strcpy(to_send, header);
        //Next, the content of the message (the token in this case)
        strcat(to_send, token);
        //We put a delimitor, unlikely to appear in a normal connection
        strcat(to_send, delimitor);
        //Finally we concatenate the string with the filler string
        strcat(to_send, fillerArray);

        printf("Sending \"%s\" to the client\n", to_send);
        printf("Writing %i bytes on socket stream\n", 1024);
        n = write(sock, to_send, 1024);
        if (n < 0) {
            perror("ERROR writing to socket");
            exit(1);
        }

        //Next token
        token = strtok(NULL, "\n");
        ii++;
    }
    printf("Completed message in %i rounds!\n", ii);

    send_end_connection_message(sock);  //Announcing end of message to the client


    //printf("Flushed buffer to client\n");
    while (1) {
        printf("Trapped\n");
        sleep(5);
        //Trapped here for now, until implementation of listening of client's commands.
        //Must not exit the proccess.
    }
}

int main(int argc, char* argv[]) {
    int sockfd, newsockfd, portno, clilen;
    char buffer[256];
    struct sockaddr_in serv_addr, cli_addr;
    int n, pid;
    int conn_number = 0;

    /* First call to socket() function */
    sockfd = socket(AF_INET, SOCK_STREAM, 0);

    if (sockfd < 0) {
        perror("ERROR opening socket");
        exit(1);
    }

    /* Initialize socket structure */
    bzero((char*)&serv_addr, sizeof(serv_addr));
    portno = 5001;

    serv_addr.sin_family = AF_INET;
    serv_addr.sin_addr.s_addr = INADDR_ANY;
    serv_addr.sin_port = htons(portno);

    /* Now bind the host address using bind() call.*/
    if (bind(sockfd, (struct sockaddr*)&serv_addr, sizeof(serv_addr)) < 0) {
        perror("ERROR on binding");
        exit(1);
    }

    //Registering SIGINT signal handler. The signal is received by BOTH THE PARENT AND CHILD.
    signal(SIGINT, signalHandler);

    printf("Started listening to connections\n");
    /* Now start listening for the clients, here
      * process will go in sleep mode and will wait
      * for the incoming connection
   */

    listen(sockfd, 5);
    clilen = sizeof(cli_addr);

    while (1) {
        newsockfd = accept(sockfd, (struct sockaddr*)&cli_addr, &clilen);
        printf("Accepting client connection...\n");
        if (newsockfd < 0) {
            perror("ERROR on accept");
            exit(1);
        }
        printf("Success: Connection %i opened\n", conn_number++);

        /* Create child process */
        pid = fork();

        if (pid < 0) {
            perror("ERROR on fork");
            exit(1);
        }

        if (pid == 0) {
            /* This is the client process */
            close(sockfd);
            doprocessing(newsockfd);
            printf("Closing connection...\n");
            exit(0);
        } else {
            close(newsockfd);
        }

    } /* end of while */
}