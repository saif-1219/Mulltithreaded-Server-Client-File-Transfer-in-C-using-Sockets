#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <pthread.h>  
#include <openssl/evp.h>  


#define PORT 8080
#define BUFFER_SIZE 1024

pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;

typedef struct __threadArgs {
    char *filename;
    int threadNum;
    int totalThreads;
    int socket;
} threadArgs;

void *threadFunction(void *args) {
    threadArgs *ThreadArgs = args;
    char* filename = malloc(strlen(ThreadArgs->filename) + 1);
    filename = strdup(ThreadArgs->filename);
    int threadNum = ThreadArgs->threadNum;
    int totalThreads = ThreadArgs->totalThreads;
    int socket = ThreadArgs->socket;

    int file = open(filename, O_RDONLY); 
    if (file == -1) {
        printf("Thread %d: ", threadNum);
        perror("File open failed");
        pthread_exit(NULL);
    }

    int filesize = lseek(file, 0, SEEK_END);
    lseek(file, 0, SEEK_SET);

    int chunkSize = filesize / totalThreads;
    int start = threadNum * chunkSize;
    int end = (threadNum == totalThreads - 1) ? filesize : (threadNum + 1) * chunkSize;


    char *buffer = (char *)malloc((end - start) * sizeof(char)); 
    
    lseek(file, start, SEEK_SET);
    int bytesRead = read(file, buffer, end - start);
    if (bytesRead == -1) {
        perror("File read failed");
        close(file);
        free(buffer);
        pthread_exit(NULL);
    }

    EVP_MD_CTX *mdctx = EVP_MD_CTX_new();
    if (!mdctx) {
        fprintf(stderr, "Error creating EVP_MD_CTX\n");
        close(file);
        pthread_exit(NULL);
        return NULL;

    }

    // Initialize the SHA-256 digest
    if (EVP_DigestInit_ex(mdctx, EVP_sha256(), NULL) != 1) {
        fprintf(stderr, "Error initializing digest\n");
        EVP_MD_CTX_free(mdctx);
        close(file);
        pthread_exit(NULL);
        return NULL;
    }

    // Update the digest with the data
    if (EVP_DigestUpdate(mdctx, buffer, bytesRead) != 1) {
        fprintf(stderr, "Error updating digest\n");
        EVP_MD_CTX_free(mdctx);
        close(file);
        pthread_exit(NULL);
        return NULL;
    }

    // Finalize the digest
    unsigned char hash[EVP_MAX_MD_SIZE];
    unsigned int hashLength;
    if (EVP_DigestFinal_ex(mdctx, hash, &hashLength) != 1) {
        fprintf(stderr, "Error finalizing digest\n");
        EVP_MD_CTX_free(mdctx);
        close(file);
        pthread_exit(NULL);
        return NULL;
    }

    EVP_MD_CTX_free(mdctx); 

    char startStr[12] = {0};
    char bytesReadStr[12] = {0};
    sprintf(startStr, "%d", start);
    sprintf(bytesReadStr, "%d", bytesRead);
    unsigned char* hashBuffer = (unsigned char *)malloc(bytesRead + hashLength + strlen(startStr) + strlen(bytesReadStr) + 16); // Adjusted size for delimiters  /////////////////////////// malloc 3
    memset(hashBuffer, 0, bytesRead + hashLength + strlen(startStr) + strlen(bytesReadStr) + 16);

    strcpy((char *)hashBuffer, startStr);
    strcat((char *)hashBuffer, ":>");
    strcat((char *)hashBuffer, bytesReadStr);
    strcat((char *)hashBuffer, ":>");

    for (int i = 0; i < hashLength; i++) {
        hashBuffer[i + strlen(startStr) + strlen(bytesReadStr) + 4] = hash[i];
    }

    for (int i = 0; i < bytesRead; i++) {
        hashBuffer[i + strlen(startStr) + strlen(bytesReadStr) + 4 + hashLength] = buffer[i];
    }
    hashBuffer[bytesRead + hashLength + strlen(startStr) + strlen(bytesReadStr) + 4] = '\0';

    printf("Thread %d: Sending data\n", threadNum);

    pthread_mutex_lock(&lock);
    int sock_send = send(socket, hashBuffer, bytesRead + hashLength + strlen(startStr) + strlen(bytesReadStr) + 4 , 0);
    // printf("Socket_sent %d bytes :::: bytesRead = %d \n", sock_send, bytesRead);
    if (sock_send == -1) {
        perror("Send failed");
        close(file);
        free(buffer);
        pthread_exit(NULL);
    }
    pthread_mutex_unlock(&lock);

    close(file);
    close(socket);
    free(filename);
    // free(ThreadArgs);
    free(buffer);
    free(hashBuffer);
    free(args);

    pthread_exit(NULL);

    return NULL;
}


int main() {
    int server_fd, new_socket;
    struct sockaddr_in address;
    int addrlen = sizeof(address);

    char buffer[BUFFER_SIZE] = {0};

    // Create socket file descriptor
    if ((server_fd = socket(AF_INET, SOCK_STREAM, 0)) == 0) { /////////////////////// server_fd created 1
        perror("Socket creation failed");
        exit(EXIT_FAILURE);
    }

    int opt = 1;
    if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
        perror("setsockopt failed");
        close(server_fd);
        exit(EXIT_FAILURE);
    }

    // Define server address
    address.sin_family = AF_INET;          // IPv4
    address.sin_addr.s_addr = INADDR_ANY;  // Bind to all available interfaces
    address.sin_port = htons(PORT);       // Convert port to network byte order

    // Bind the socket to the address
    if (bind(server_fd, (struct sockaddr *)&address, sizeof(address)) < 0) {
        perror("Bind failed");
        close(server_fd);
        exit(EXIT_FAILURE);
    }

    // Listen for incoming connections
    if (listen(server_fd, 5) < 0) {
        perror("Listen failed");
        close(server_fd);
        exit(EXIT_FAILURE);
    }

    printf("Server listening on port %d...\n", PORT);

    // Accept an incoming connection
    // Communication loop
    while (1) {
        if ((new_socket = accept(server_fd, (struct sockaddr *)&address, (socklen_t *)&addrlen)) < 0) { ////////////////////////////////////////////////////// new_socket created 1
            perror("Accept failed");
            close(server_fd);
            exit(EXIT_FAILURE);
        }
        char* filename;
        int numThreadsInt;
        memset(buffer, 0, BUFFER_SIZE);  // Clear the buffer
        int bytes_read = read(new_socket, buffer, BUFFER_SIZE);

        if (bytes_read > 0){
            //input format: <filename> <numThreads>
            filename = strtok(buffer, " ");
            char *numThreads = strtok(NULL, " ");
            numThreadsInt = atoi(numThreads);
            
            int *arr_sockets = (int *)malloc(numThreadsInt * sizeof(int)); ///////////////////////////////////////////////// malloc 1

            for (int i = 0; i < numThreadsInt; i++) {
                arr_sockets[i] = accept(server_fd, (struct sockaddr *)&address, (socklen_t *)&addrlen);
                if (arr_sockets[i] < 0) {
                    perror("Accept failed");
                    close(server_fd);
                    exit(EXIT_FAILURE);
                }
            }

            pthread_t *threads = (pthread_t *)malloc(numThreadsInt * sizeof(pthread_t)); /////////////////////////////// malloc 2
            for (int i = 0; i < numThreadsInt; i++) {
                threadArgs *args = (threadArgs *)malloc(sizeof(threadArgs)); /////////////////////////////////////////// malloc 3
                args->filename = strdup(filename);
                args->threadNum = i;    
                args->totalThreads = numThreadsInt;
                args->socket = arr_sockets[i];
                pthread_create(&threads[i], NULL, &threadFunction, (void *)args);
            }

            for (int i = 0; i < numThreadsInt; i++) {
                pthread_join(threads[i], NULL);
            }

            printf("File %s sent to client\n", filename);

            free(threads);
            free(arr_sockets);
        }
    }

    close(new_socket);
    close(server_fd);

    return 0;
}