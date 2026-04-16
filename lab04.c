/*
@author: Myko Jefferson M. Javier
@date: April 16, 2026
@section: CMSC 180 - CD3L
@code-desc: Implementing Min-Max Transformation on an nxn matrix in C using Sockets
*/

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <sys/time.h>
#include <unistd.h>
#include <time.h>
#include <string.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#ifndef __APPLE__
#include <sched.h>
#endif

//Helper Functions
double min (double **mat, int n, int col);
double max (double **mat, int n, int col);
double generate_random(int max);
void print_matrix(double **matrix, int row, int start_col, int cols_to_print);
double **generate_matrix(int row, int col);
int64_t timestamp_now (void);
double timestamp_to_seconds (int64_t timestamp);
void mmt(double ** matrix, int n, int start_col, int num_of_iter);
void mmtRow(double ** matrix, int n, int start_col, int num_of_iter);

int main(int argc, char **argv){
    if (argc < 4) {
        printf("Usage: %s <n: size> <p: port> <s: status 0=master, 1=slave>\n", argv[0]);
        return 1;
    }
    
    int n = atoi(argv[1]);
    int p = atoi(argv[2]);
    int s = atoi(argv[3]);

#ifdef CORE_AFFINED
#ifdef __linux__
    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);
    CPU_SET(0, &cpuset); // Pin to core 0
    if (sched_setaffinity(0, sizeof(cpu_set_t), &cpuset) == -1) {
        perror("sched_setaffinity");
    }
#else
    printf("Warning: Core affinity is only supported on Linux.\n");
#endif
#endif

    int t = 0;
    char ips[100][64];
    int ports[100];
    
    FILE *config = fopen("config.txt", "r");
    if (config == NULL) {
        printf("Error: Cannot open config.txt\n");
        return 1;
    }
    fscanf(config, "%d", &t);
    for(int i = 0; i < t; i++) {
        fscanf(config, "%s %d", ips[i], &ports[i]);
    }
    fclose(config);

    if (s == 0) { // Master
        double **matrix = generate_matrix(n, n);
        srand(time(NULL));
        for (int i = 0; i < n; i++){
            for(int j = 0; j < n; j++){
                matrix[i][j] = (double)generate_random(100);
            }
        }

        int rows_per_slave = n / t;
        int remaining_rows = n % t;
        int64_t start = timestamp_now();
        int *sockets = malloc(sizeof(int) * t);

        // connect to all slaves
        for(int i = 0; i < t; i++) {
            sockets[i] = socket(AF_INET, SOCK_STREAM, 0);
            struct sockaddr_in serv_addr;
            serv_addr.sin_family = AF_INET;
            serv_addr.sin_port = htons(ports[i]);
            inet_pton(AF_INET, ips[i], &serv_addr.sin_addr);
            
            // Allow time for slaves to start
            while (connect(sockets[i], (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0) {
                usleep(100000); // 100ms
            }

            if (i == 0) {
                // Send entire matrix to slave 0
                send(sockets[i], &n, sizeof(int), 0);
                for(int r = 0; r < n; r++) {
                    send(sockets[i], matrix[r], sizeof(double) * n, 0);
                }
            } else {
                int identifier = -1;
                send(sockets[i], &identifier, sizeof(int), 0);
            }
        }
        
        char ack[4];
        for (int i = 0; i < t; i++) {
            recv(sockets[i], ack, sizeof(ack), 0);
            int rows_to_receive = rows_per_slave + (i < remaining_rows ? 1 : 0);
            double **received_submatrix = generate_matrix(rows_to_receive, n);
            for(int r = 0; r < rows_to_receive; r++) {
                size_t total_received = 0;
                while (total_received < sizeof(double) * n) {
                    ssize_t bytes = recv(sockets[i], ((char*)received_submatrix[r]) + total_received, sizeof(double) * n - total_received, 0);
                    if (bytes <= 0) break;
                    total_received += bytes;
                }
            }
            printf("Master received submatrix from slave %d:\n", i);
            print_matrix(received_submatrix, rows_to_receive, 0, n);
            for(int r=0; r<rows_to_receive; r++) free(received_submatrix[r]);
            free(received_submatrix);
        }

        for(int i = 0; i < t; i++) close(sockets[i]);
        free(sockets);
        
        int64_t end = timestamp_now();
        printf("Master Elapsed Time:\n%lf\n", timestamp_to_seconds(end - start));
        
    } else if (s == 1) { // Slave
        int server_fd, new_socket;
        struct sockaddr_in address;
        int opt = 1;
        int addrlen = sizeof(address);
        
        if ((server_fd = socket(AF_INET, SOCK_STREAM, 0)) == 0) {
            perror("socket failed"); exit(EXIT_FAILURE);
        }
        if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt))) {
            perror("setsockopt"); exit(EXIT_FAILURE);
        }
        address.sin_family = AF_INET;
        address.sin_addr.s_addr = INADDR_ANY;
        address.sin_port = htons(p);
        
        if (bind(server_fd, (struct sockaddr *)&address, sizeof(address)) < 0) {
            perror("bind failed"); exit(EXIT_FAILURE);
        }
        if (listen(server_fd, 3) < 0) {
            perror("listen"); exit(EXIT_FAILURE);
        }
        
        if ((new_socket = accept(server_fd, (struct sockaddr *)&address, (socklen_t*)&addrlen)) < 0) {
            perror("accept"); exit(EXIT_FAILURE);
        }
        int64_t start = timestamp_now();
        int rows_per_slave = n / t;
        int remaining_rows = n % t;
        int is_slave0 = (p == ports[0]);

        if (is_slave0) {
            int received_n;
            recv(new_socket, &received_n, sizeof(int), 0);
            double **full_matrix = generate_matrix(n, n);
            for(int r = 0; r < n; r++) {
                size_t total_received = 0;
                while (total_received < sizeof(double) * n) {
                    ssize_t bytes = recv(new_socket, ((char*)full_matrix[r]) + total_received, sizeof(double) * n - total_received, 0);
                    if (bytes <= 0) break;
                    total_received += bytes;
                }
            }

            int *sockets = malloc(sizeof(int) * t);
            int current_row = rows_per_slave + (0 < remaining_rows ? 1 : 0);
            for (int i = 1; i < t; i++) {
                sockets[i] = socket(AF_INET, SOCK_STREAM, 0);
                struct sockaddr_in serv_addr;
                serv_addr.sin_family = AF_INET;
                serv_addr.sin_port = htons(ports[i]);
                inet_pton(AF_INET, ips[i], &serv_addr.sin_addr);
                
                while (connect(sockets[i], (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0) {
                    usleep(100000);
                }
                
                int rows_to_send = rows_per_slave + (i < remaining_rows ? 1 : 0);
                send(sockets[i], &rows_to_send, sizeof(int), 0);
                for(int r = 0; r < rows_to_send; r++) {
                    send(sockets[i], full_matrix[current_row + r], sizeof(double) * n, 0);
                }
                current_row += rows_to_send;
            }

            // process own submatrix
            int my_rows = rows_per_slave + (0 < remaining_rows ? 1 : 0);
            mmtRow(full_matrix, n, 0, my_rows);

            send(new_socket, "ack", 4, 0);
            for(int r = 0; r < my_rows; r++) {
                send(new_socket, full_matrix[r], sizeof(double) * n, 0);
            }
            
            for (int i = 1; i < t; i++) close(sockets[i]);
            free(sockets);
            close(new_socket);
            int64_t end = timestamp_now();
            printf("Slave 0 Elapsed Time:\n%lf\n", timestamp_to_seconds(end - start));
        } else {
            // Slaves 1..T-1 receive 2 connections: one from master, one from slave0
            int sock1 = new_socket;
            int sock2;
            if ((sock2 = accept(server_fd, (struct sockaddr *)&address, (socklen_t*)&addrlen)) < 0) {
                perror("accept2"); exit(EXIT_FAILURE);
            }
            
            int first_int;
            recv(sock1, &first_int, sizeof(int), 0);
            
            int master_socket, s0_socket, rows_to_receive;
            if (first_int == -1) {
                master_socket = sock1;
                s0_socket = sock2;
                recv(s0_socket, &rows_to_receive, sizeof(int), 0);
            } else {
                master_socket = sock2;
                s0_socket = sock1;
                rows_to_receive = first_int;
                int identifier;
                recv(master_socket, &identifier, sizeof(int), 0);
            }
            
            double **submatrix = generate_matrix(rows_to_receive, n);
            for(int r = 0; r < rows_to_receive; r++) {
                size_t total_received = 0;
                while (total_received < sizeof(double) * n) {
                    ssize_t bytes = recv(s0_socket, ((char*)submatrix[r]) + total_received, sizeof(double) * n - total_received, 0);
                    if (bytes <= 0) break;
                    total_received += bytes;
                }
            }

            mmtRow(submatrix, n, 0, rows_to_receive);
            
            send(master_socket, "ack", 4, 0);
            for(int r = 0; r < rows_to_receive; r++) {
                send(master_socket, submatrix[r], sizeof(double) * n, 0);
            }
            
            close(s0_socket);
            close(master_socket);
            int64_t end = timestamp_now();
            printf("Slave Elapsed Time:\n%lf\n", timestamp_to_seconds(end - start));
            printf("Received Matrix:\n");
            print_matrix(submatrix, rows_to_receive, 0, n);
        }
        close(server_fd);
    }
    return 0;
}

void print_matrix(double **matrix, int row, int start_col, int cols_to_print){
    for (int i = 0; i < row; i++){
        for (int j = start_col; j < start_col + cols_to_print; j++){
            printf(" %0.2g\t",  matrix[i][j]);
        }
        printf("\n");
    }
}
double max(double **mat, int size, int col){
    double max = mat[0][col];
    for (int i = 1; i < size; i++){
        max = (mat[i][col] >= max) ? mat[i][col] : max;
    }
    return max;
}
double min(double **mat, int size, int col){
    double min = mat[0][col];
    for (int i = 1; i < size; i++){
        min = (mat[i][col] < min) ? mat[i][col] : min;
    }
    return min;
}
double ** generate_matrix(int row, int col){
    double **temp = (double**)malloc(sizeof(double*)*row);
    for(int i = 0; i < row; i++){
        temp[i] = (double *)(malloc(sizeof(double) * col));
    }
    return temp;
}
void mmt(double ** matrix, int n, int start_col, int num_of_iter){
    double colMax, colMin; 
    for (int i=start_col, k=0; k < num_of_iter; i++, k++){
        colMax = max(matrix, n, i);
        colMin = min(matrix, n, i);
        for (int j = 0; j < n; j++){
            matrix[j][i] = (matrix[j][i] - colMin) / (colMax - colMin);
        }
    }
}
void mmtRow(double ** matrix, int n, int start_col, int num_of_iter){
    double colMax, colMin; 
    for (int i=start_col, k=0; k < num_of_iter; i++, k++){
        colMax = max(matrix, n, i);
        colMin = min(matrix, n, i);
        for (int j = 0; j < n; j++){
            matrix[i][j] = (matrix[i][j] - colMin) / (colMax - colMin);
        }
    }
}
int64_t timestamp_now (void) {
    struct timeval tv;
    gettimeofday (&tv, NULL);
    return (int64_t) tv.tv_sec * 1000000 + tv.tv_usec;
}
double timestamp_to_seconds (int64_t timestamp) {
    return timestamp / 1000000.0;
}
double generate_random(int max){
    return (double)(rand() % max);
}
