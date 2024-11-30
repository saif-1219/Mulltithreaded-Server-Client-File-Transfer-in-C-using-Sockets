#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <pthread.h>
#include <openssl/evp.h>
#include <errno.h>

#define PORT 8080
int BUFFER_SIZE = 3024000;

pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;

typedef struct __threadArgs {
    FILE *file;
    int socket;
} threadArgs;

void *threadFunction(void *args) {
    threadArgs *threadArgs = args;
    int sock = threadArgs->socket;  
    unsigned char * buffer = (unsigned char *)malloc(BUFFER_SIZE);
    memset(buffer, 0, BUFFER_SIZE);  // Clear the buffer

    struct timeval timeout;
    timeout.tv_sec = 5;  // Wait for 5 seconds
    timeout.tv_usec = 0;
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, (const char *)&timeout, sizeof(timeout));

    int new_buf_size = 7024000; // Larger size
    setsockopt(sock, SOL_SOCKET, SO_RCVBUF, &new_buf_size, sizeof(new_buf_size));

    int bytes_received = 0;
    int total_bytes_received = 0;

    while ((bytes_received = read(sock, buffer + total_bytes_received, BUFFER_SIZE - total_bytes_received)) > 0) {
        total_bytes_received += bytes_received;
    }

    if (bytes_received == -1) {
        perror("Read failed or timed out");
        free(args);
        pthread_exit((void *)-1);
        return NULL;
    }

    if (total_bytes_received > 0) {
        // printf("thread number %d received :::: bytesRead = %s \n", threadNum, buffer);
        //the buffer read contains: first, the thread num, second the length of content, third the sha256 hash and then the file content

        char *startStr = (char *)malloc(32);
        memset(startStr, 0, 32);
        char *bytesReadStr = (char *)malloc(32);
        memset(bytesReadStr, 0, 32);
        char *hashAndStr = (char *)malloc(total_bytes_received);
        memset(hashAndStr, 0, total_bytes_received);

        char *first_delim = strstr((char *)buffer, ":>");
        char *second_delim = strstr(first_delim + 2, ":>");

        // Extract fields using pointers
        if (first_delim && second_delim) {
            strncpy(startStr, (char *)buffer, first_delim - (char *)buffer); // Copy startStr
            startStr[first_delim - (char *)buffer] = '\0';          // Null-terminate

            strncpy(bytesReadStr, first_delim + 2, second_delim - (first_delim + 2)); // Copy offset
            bytesReadStr[second_delim - (first_delim + 2)] = '\0';                    // Null-terminate

            // strcpy(hashAndStr, second_delim + 2); // Copy data
            hashAndStr = second_delim + 2;
        }

        int offSet = atoi(startStr);
        int contentLength = atoi(bytesReadStr);

        unsigned char* hash = (unsigned char *)malloc(32);
        memset(hash, 0, 32);
        for (int i = 0; i < 32; i++) {
            hash[i] = hashAndStr[i];
            // hash[i] = buffer[sizeof(startStr) + sizeof(bytesReadStr) - 8 + i];
        }
        // printf("Thread %d content: ", offSet);
        char* fileContent = (char *)malloc(contentLength);
        memset(fileContent, 0, contentLength);
        for (int i = 0; i < contentLength; i++) {
            fileContent[i] = hashAndStr[i+32];
        }

        //finding the hash of the file content and comparing it with the hash received
        EVP_MD_CTX *mdctx = EVP_MD_CTX_new();
        if (!mdctx) {
            fprintf(stderr, "Error creating EVP_MD_CTX\n");
            free(threadArgs);
            free(hash);
            free(fileContent);
            free(buffer);
            pthread_exit((void *)-1);
            return NULL;
        }

        // Initialize the SHA-256 digest
        if (EVP_DigestInit_ex(mdctx, EVP_sha256(), NULL) != 1) {
            fprintf(stderr, "Error initializing digest\n");
            EVP_MD_CTX_free(mdctx);
            free(threadArgs);
            free(hash);
            free(fileContent);
            free(buffer);
            pthread_exit((void *)-1);
            return NULL;
        }

        // Update the digest with the data
        if (EVP_DigestUpdate(mdctx, fileContent, contentLength) != 1) {
            fprintf(stderr, "Error updating digest\n");
            EVP_MD_CTX_free(mdctx);
            free(threadArgs);
            free(hash);
            free(fileContent);
            free(buffer);
            pthread_exit((void *)-1);
            return NULL;
        }

        // Finalize the digest
        unsigned char hashFile[EVP_MAX_MD_SIZE];
        unsigned int hashLength;
        if (EVP_DigestFinal_ex(mdctx, hashFile, &hashLength) != 1) {
            fprintf(stderr, "Error finalizing digest\n");
            EVP_MD_CTX_free(mdctx);
            free(threadArgs);
            free(hash);
            free(fileContent);
            free(buffer);
            pthread_exit((void *)-1);
            return NULL;
        }

        //comparing the two hashes
        int flag = 1;
        for (int i = 0; i < 32; i++) {
            if (hash[i] != hashFile[i]) {
                // printf("Hash[%d]: %02x HashFile[%d]: %02x\n", i, hash[i], i, hashFile[i]);
                flag = 0;
                // break;
            }
        }

        // printf("Flag: %d Thread: %d\n", flag, offSet); 
        if (flag == 0){
            printf("Error: Hashes do not match\n");
            free(threadArgs); 
            free(hash);
            free(fileContent);
            free(buffer);
            pthread_exit((void *)-1);
            return NULL;
        }

        pthread_mutex_lock(&lock);
        // printf("Thread %d writing to file \n", threadNum);
        fseek(threadArgs->file, offSet, SEEK_SET);
        fflush(threadArgs->file);
        fwrite(fileContent, 1, contentLength, threadArgs->file);
        pthread_mutex_unlock(&lock);

        free(hash);
        free(fileContent);
        EVP_MD_CTX_free(mdctx);
    }

    free(buffer);
    free(threadArgs); 
    pthread_exit((void *)1);

    return NULL;
}

int main(int argc, char* argv[]) {
    if (argc != 3){
        printf("Input Format: ./client <fileName> <numThreads>\n");
        return 0;
    }
    char* filename = argv[1];
    char* numThreads = argv[2];
    int numThreadsInt = atoi(numThreads);

    int sock = 0;
    struct sockaddr_in server_address;
    char buffer[BUFFER_SIZE];
    memset(buffer, 0, BUFFER_SIZE);
    char message[BUFFER_SIZE];
    memset(message, 0, BUFFER_SIZE);

    

    // Create socket
    if ((sock = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
        perror("Socket creation failed");
        exit(EXIT_FAILURE);
    }

    // Define server address
    server_address.sin_family = AF_INET;                  // IPv4
    server_address.sin_port = htons(PORT);               // Convert port to network byte order

    // Convert IP address to binary form
    if (inet_pton(AF_INET, "127.0.0.1", &server_address.sin_addr) <= 0) {
        perror("Invalid address/Address not supported");
        close(sock);
        exit(EXIT_FAILURE);
    }

    // Connect to the server
    if (connect(sock, (struct sockaddr *)&server_address, sizeof(server_address)) < 0) {
        perror("Connection failed");
        close(sock);
        exit(EXIT_FAILURE);
    }

    printf("Connected to server on port %d.\n", PORT);

    strcpy(message, filename);
    strcat(message, " ");
    strcat(message, numThreads);

    int *sock_arr = (int *)malloc(numThreadsInt * sizeof(int));
    // Send message to the server
    send(sock, message, strlen(message), 0);

    sleep(1);

    char* name_suffix = strdup("_at_client");
    char* filename_dup = strdup(filename);
    char* file_name = strtok(filename_dup, ".");
    char* file_extension = strtok(NULL, ".");
    char* new_filename;

    if (file_extension != NULL) {
        new_filename = (char *)malloc(strlen(file_extension) + strlen(name_suffix) + strlen(file_name) + 2);
        strcpy(new_filename, file_name);
        strcat(new_filename, name_suffix);
        strcat(new_filename, ".");
        strcat(new_filename, file_extension);
    } else {
        new_filename = (char *)malloc(strlen(name_suffix) + strlen(file_name) + 1);
        strcpy(new_filename, file_name);
        strcat(new_filename, name_suffix);
    }

    // Creating file
    FILE *file = fopen(new_filename, "wb+");
    if (file == NULL) {
        perror("File creation failed");
        exit(EXIT_FAILURE);
    }


    for (int i = 0;i < numThreadsInt; i++) {
        if ((sock_arr[i] = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
            perror("Socket creation failed");
            exit(EXIT_FAILURE);
        }
        if (connect(sock_arr[i], (struct sockaddr *)&server_address, sizeof(server_address)) < 0) {
            perror("Connection failed");
            close(sock_arr[i]);
            exit(EXIT_FAILURE);
        }
    }
    // Create threads
    pthread_t *threads = (pthread_t *)malloc(numThreadsInt * sizeof(pthread_t));
    for (int i = 0; i < numThreadsInt; i++) {
        threadArgs *args = (threadArgs *)malloc(sizeof(threadArgs));
        args->socket = sock_arr[i];
        args->file = file;
        pthread_create(&threads[i], NULL, threadFunction, (void *)args);
    }
    printf("\n\n");
    // Wait for threads to finish

    int thread_status;
    int success = 1;
    for (int i = 0; i < numThreadsInt; i++) {
        pthread_join(threads[i], (void **)&thread_status);
        if (thread_status == -1) {
            success = 0;
        }
    }

    if (success) {
        printf("Successfully Transfered the File\n");
    } else {
        printf("Failed to transfer File\n");
        remove(new_filename);
    }

    fclose(file);
    free(threads);
    close(sock);
    return 0;
}
