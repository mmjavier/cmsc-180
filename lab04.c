/*
@author: Myko Jefferson M. Javier (Modified for Distributed Computing)
@date: April 20, 2026
@section: CMSC 180 - CD3L
@code-desc: Distributed Min-Max Transformation using Tree-based Broadcasting
*/

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <sys/time.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <time.h>
#include <errno.h>

#define CONFIG_FILE "config.txt"
#define BUFFER_SIZE 8192

// Configuration structure for slaves
typedef struct {
    char ip[16];
    int port;
} SlaveConfig;

// Function Prototypes - Network
int create_server_socket(int port);
int connect_to_slave(const char *ip, int port);
void send_matrix_data(int socket, double **matrix, int rows, int cols, int start_row);
void receive_matrix_data(int socket, double **matrix, int rows, int cols);
void send_ack(int socket);
void receive_ack(int socket);

// Function Prototypes - MMT
void mmt(double **mat, int n, int start_row, int num_rows);
double min_row(double **mat, int n, int row);
double max_row(double **mat, int n, int row);

// Helper Functions
double generate_random(int max);
void print_matrix(double **matrix, int rows, int cols);
double **generate_matrix(int row, int col);
void free_matrix(double **matrix, int rows);
int64_t timestamp_now(void);
double timestamp_to_seconds(int64_t timestamp);

// Configuration file reading
int read_config_master(SlaveConfig **slaves, int *num_slaves);

// Row Assignment Helpers
int get_start_row(int rank, int n, int t);
int get_num_rows(int rank, int n, int t);

// Tree-based broadcasting
void tree_broadcast_master(int n, int t, double **matrix, SlaveConfig *slaves);
void tree_broadcast_slave(int my_rank, int group_start, int group_end, int total_nodes, int dimension, double **payload_matrix, int payload_start_row, SlaveConfig *slaves_config);

int main(int argc, char **argv) {
    int n = 0, p = 0, s = -1;
    
    if (argc >= 4) {
        n = atoi(argv[1]);
        p = atoi(argv[2]);
        s = atoi(argv[3]);
    } else {
        // Read user inputs
        printf("Enter matrix dimension (n): ");
        if (scanf("%d", &n) != 1) {
            fprintf(stderr, "Failed to read matrix dimension.\n");
            return 1;
        }
        printf("Enter port number (p): ");
        if (scanf("%d", &p) != 1) {
            fprintf(stderr, "Failed to read port number.\n");
            return 1;
        }
        printf("Enter status (0=master, 1=slave): ");
        if (scanf("%d", &s) != 1) {
            fprintf(stderr, "Failed to read status.\n");
            return 1;
        }
    }
    
    if (s == 0) {
        // MASTER PROCESS
        printf("\n=== MASTER PROCESS ===\n");
        
        // Read configuration
        SlaveConfig *slaves = NULL;
        int t = 0;
        if (read_config_master(&slaves, &t) != 0) {
            printf("Error reading configuration file\n");
            return 1;
        }
        printf("Configuration: %d slaves\n", t);
        for (int i = 0; i < t; i++) {
            printf("  Slave %d: %s:%d\n", i, slaves[i].ip, slaves[i].port);
        }
        
        // Generate random non-zero matrix
        double **matrix = generate_matrix(n, n);
        srand(time(NULL));
        for (int i = 0; i < n; i++) {
            for (int j = 0; j < n; j++) {
                matrix[i][j] = generate_random(100);
            }
        }
        
        //Comment out 
        // printf("\nOriginal Matrix (%dx%d):\n", n, n);
        // print_matrix(matrix, n, n);
        
        // Calculate rows per slave
        int rows_per_slave = n / t;
        int remaining_rows = n % t;
        
        printf("\nStarting tree-based broadcast...\n");
        int64_t start_time = timestamp_now();
        
        // Tree-based broadcasting
        tree_broadcast_master(n, t, matrix, slaves);
        
        // Receive acknowledgements and transformed submatrices from all slaves
        printf("\nWaiting for acknowledgements and results from slaves...\n");
        for (int i = 0; i < t; i++) {
            int slave_sock = connect_to_slave(slaves[i].ip, slaves[i].port);
            if (slave_sock < 0) {
                printf("Failed to connect to slave %d for receiving results\n", i);
                continue;
            }
            
            // Receive ACK
            receive_ack(slave_sock);
            printf("Received ACK from slave %d\n", i);
            
            // Receive transformed submatrix directly into the correct position
            int start_row = i * rows_per_slave + (i < remaining_rows ? i : remaining_rows);
            int num_rows = rows_per_slave + (i < remaining_rows ? 1 : 0);
            
            receive_matrix_data(slave_sock, matrix + start_row, num_rows, n);
            
            close(slave_sock);
        }
        
        int64_t end_time = timestamp_now();
        
        // Comment out to print matrix
        // printf("\n=== RECEIVED MATRIX ===\n");
        // print_matrix(matrix, n, n);
        
        printf("\nTotal Time Elapsed: %lf seconds\n", 
               timestamp_to_seconds(end_time - start_time));
        
        free(slaves);
        free_matrix(matrix, n);
        
    } else if (s == 1) {
        // SLAVE PROCESS
        printf("\n=== SLAVE PROCESS ===\n");
        
        // Create server socket and listen
        int server_sock = create_server_socket(p);
        if (server_sock < 0) {
            printf("Failed to create server socket\n");
            return 1;
        }
        
        printf("Listening on port %d...\n", p);
        listen(server_sock, 5);
        
        // Accept connection from master or another slave
        struct sockaddr_in client_addr;
        socklen_t addr_len = sizeof(client_addr);
        int client_sock = accept(server_sock, (struct sockaddr *)&client_addr, &addr_len);
        if (client_sock < 0) {
            printf("Failed to accept connection\n");
            close(server_sock);
            return 1;
        }
        
        printf("Connection accepted from %s\n", inet_ntoa(client_addr.sin_addr));
        
        int64_t time_before = timestamp_now();
        
        int total_slaves;
        int group_start;
        int group_end;
        int rows, cols;
        recv(client_sock, &rows, sizeof(int), 0);
        recv(client_sock, &total_slaves, sizeof(int), 0);
        recv(client_sock, &group_start, sizeof(int), 0);
        recv(client_sock, &group_end, sizeof(int), 0);
        cols = rows; // Full matrix initially n x n
        int rank = group_start;
        
        int payload_start_row = get_start_row(group_start, rows, total_slaves);
        int payload_end_row = get_start_row(group_end, rows, total_slaves) + get_num_rows(group_end, rows, total_slaves) - 1;
        int payload_rows = payload_end_row - payload_start_row + 1;
        
        printf("Receiving submatrix chunk for subtree: %d x %d as Rank %d (Group %d to %d)\n", payload_rows, cols, rank, group_start, group_end);
        
        double **submatrix = generate_matrix(payload_rows, cols);
        receive_matrix_data(client_sock, submatrix, payload_rows, cols);
        
        // We received the broadcast and finished computing, now close the parent node's socket early
        close(client_sock); 
        
        // Read config and forward using tree cast
        SlaveConfig *slaves = NULL;
        int config_slaves = 0;
        if (read_config_master(&slaves, &config_slaves) == 0) {
            tree_broadcast_slave(rank, group_start, group_end, total_slaves, rows, submatrix, payload_start_row, slaves);
        } else {
            printf("Error reading slaves configuration for tree broadcast.\n");
        }
        
        int my_start_row = payload_start_row;
        int my_num_rows = get_num_rows(rank, rows, total_slaves);
        
        // Comment out to print the submatrix
        // printf("\nSlave %d Assigned Submatrix (rows %d to %d):\n", rank, my_start_row, my_start_row + my_num_rows - 1);
        // print_matrix(submatrix, my_num_rows, cols);
        
        // MMT placeholder: logic will be implemented here later 
        // mmt(submatrix, rows, 0, my_num_rows);
        
        printf("\nSlave %d waiting for master to collect results...\n", rank);
        
        // Accept a NEW connection directly from the master to send back our final results
        int master_collect_sock = accept(server_sock, (struct sockaddr *)&client_addr, &addr_len);
        if (master_collect_sock < 0) {
            printf("Failed to accept connection from master\n");
            // handle error properly
        }
        
        // Return exactly the assigned submatrix rows (currently unaltered) back to master
        send_ack(master_collect_sock);
        send_matrix_data(master_collect_sock, submatrix, my_num_rows, cols, 0);
        
        int64_t time_after = timestamp_now();
        
        printf("\nTime Elapsed (slave): %lf seconds\n", 
               timestamp_to_seconds(time_after - time_before));
        
        if (master_collect_sock >= 0) close(master_collect_sock);
        close(server_sock);
        if (slaves) free(slaves);
        free_matrix(submatrix, payload_rows);
    }
    
    return 0;
}

// Tree-based broadcast from master
void tree_broadcast_master(int n, int t, double **matrix, SlaveConfig *slaves) {
    if (t <= 0) return;
    
    // Master sends the ENTIRE matrix to slave 0
    int slave_sock = connect_to_slave(slaves[0].ip, slaves[0].port);
    if (slave_sock >= 0) {
        int group_start = 0;
        int group_end = t - 1;
        
        // Send dimension and total slaves
        send(slave_sock, &n, sizeof(int), 0);
        send(slave_sock, &t, sizeof(int), 0);
        send(slave_sock, &group_start, sizeof(int), 0);
        send(slave_sock, &group_end, sizeof(int), 0);
        
        // Send entire matrix contiguous block to root slave
        send_matrix_data(slave_sock, matrix, n, n, 0);
        printf("Sent full matrix to slave 0 for tree scattering\n");
        
        close(slave_sock);
    } else {
        printf("Failed to connect to root slave 0 for forwarding.\n");
    }
}

// Tree-based broadcast logic for slaves (binary tree topology recursive scatter)
void tree_broadcast_slave(int my_rank, int group_start, int group_end, int total_nodes, int dimension, 
                          double **payload_matrix, int payload_start_row, SlaveConfig *slaves_config) {
    if (group_start >= group_end) return; // Leaf node, no children to scatter to
    
    int remaining_start = group_start + 1;
    int remaining_end = group_end;
    int mid = remaining_start + (remaining_end - remaining_start) / 2;
    
    // Forward the specific contiguous submatrix to the left child node
    if (remaining_start <= mid) {
        int left_node = remaining_start;
        int left_sock = connect_to_slave(slaves_config[left_node].ip, slaves_config[left_node].port);
        if (left_sock >= 0) {
            send(left_sock, &dimension, sizeof(int), 0);
            send(left_sock, &total_nodes, sizeof(int), 0);
            send(left_sock, &remaining_start, sizeof(int), 0);
            send(left_sock, &mid, sizeof(int), 0);
            
            int c1_start_row = get_start_row(remaining_start, dimension, total_nodes);
            int c1_end_row = get_start_row(mid, dimension, total_nodes) + get_num_rows(mid, dimension, total_nodes) - 1;
            int c1_rows = c1_end_row - c1_start_row + 1;
            
            int row_offset = c1_start_row - payload_start_row;
            send_matrix_data(left_sock, payload_matrix, c1_rows, dimension, row_offset);
            printf("Slave %d forwarded contiguous chunk to left node (Slave %d, %d rows)\n", my_rank, left_node, c1_rows);
            close(left_sock);
        } else {
            printf("Slave %d failed to connect to left node (Slave %d)\n", my_rank, left_node);
        }
    }
    
    // Forward the specific contiguous submatrix to the right child node
    if (mid + 1 <= remaining_end) {
        int right_node = mid + 1;
        int right_sock = connect_to_slave(slaves_config[right_node].ip, slaves_config[right_node].port);
        if (right_sock >= 0) {
            send(right_sock, &dimension, sizeof(int), 0);
            send(right_sock, &total_nodes, sizeof(int), 0);
            send(right_sock, &right_node, sizeof(int), 0);
            send(right_sock, &remaining_end, sizeof(int), 0);
            
            int c2_start_row = get_start_row(right_node, dimension, total_nodes);
            int c2_end_row = get_start_row(remaining_end, dimension, total_nodes) + get_num_rows(remaining_end, dimension, total_nodes) - 1;
            int c2_rows = c2_end_row - c2_start_row + 1;
            
            int row_offset = c2_start_row - payload_start_row;
            send_matrix_data(right_sock, payload_matrix, c2_rows, dimension, row_offset);
            printf("Slave %d forwarded contiguous chunk to right node (Slave %d, %d rows)\n", my_rank, right_node, c2_rows);
            close(right_sock);
        } else {
            printf("Slave %d failed to connect to right node (Slave %d)\n", my_rank, right_node);
        }
    }
}

// Network Functions
// Row Assignment Helper Functions
int get_start_row(int rank, int n, int t) {
    int rows_per_slave = n / t;
    int remaining_rows = n % t;
    return rank * rows_per_slave + (rank < remaining_rows ? rank : remaining_rows);
}

int get_num_rows(int rank, int n, int t) {
    int rows_per_slave = n / t;
    int remaining_rows = n % t;
    return rows_per_slave + (rank < remaining_rows ? 1 : 0);
}

int create_server_socket(int port) {
    int server_sock = socket(AF_INET, SOCK_STREAM, 0);
    if (server_sock < 0) {
        perror("socket");
        return -1;
    }
    
    int opt = 1;
    setsockopt(server_sock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    
    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(port);
    
    if (bind(server_sock, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        perror("bind");
        close(server_sock);
        return -1;
    }
    
    return server_sock;
}

int connect_to_slave(const char *ip, int port) {
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) {
        perror("socket");
        return -1;
    }
    
    struct sockaddr_in slave_addr;
    memset(&slave_addr, 0, sizeof(slave_addr));
    slave_addr.sin_family = AF_INET;
    slave_addr.sin_port = htons(port);
    
    if (inet_pton(AF_INET, ip, &slave_addr.sin_addr) <= 0) {
        perror("inet_pton");
        close(sock);
        return -1;
    }
    
    // Retry connection a bunch of times in case the node is still computing MMT
    int retries = 30;
    while (retries > 0) {
        if (connect(sock, (struct sockaddr *)&slave_addr, sizeof(slave_addr)) == 0) {
            return sock;
        }
        retries--;
        sleep(1);
    }
    
    perror("connect");
    close(sock);
    return -1;
}

void send_matrix_data(int socket, double **matrix, int rows, int cols, int start_row) {
    for (int i = 0; i < rows; i++) {
        int total_sent = 0;
        int bytes_to_send = cols * sizeof(double);
        
        while (total_sent < bytes_to_send) {
            int sent = send(socket, ((char *)matrix[start_row + i]) + total_sent, 
                            bytes_to_send - total_sent, 0);
            if (sent <= 0) {
                printf("Error sending matrix data\n");
                return;
            }
            total_sent += sent;
        }
    }
}

void receive_matrix_data(int socket, double **matrix, int rows, int cols) {
    for (int i = 0; i < rows; i++) {
        int total_received = 0;
        int bytes_to_receive = cols * sizeof(double);
        
        while (total_received < bytes_to_receive) {
            int received = recv(socket, ((char *)matrix[i]) + total_received, 
                              bytes_to_receive - total_received, 0);
            if (received <= 0) {
                printf("Error receiving matrix data\n");
                return;
            }
            total_received += received;
        }
    }
}

void send_ack(int socket) {
    int ack = 1;
    send(socket, &ack, sizeof(int), 0);
}

void receive_ack(int socket) {
    int ack = 0;
    int total_received = 0;
    while (total_received < sizeof(int)) {
        int received = recv(socket, ((char *)&ack) + total_received, 
                            sizeof(int) - total_received, 0);
        if (received <= 0) {
            printf("Error receiving ACK\n");
            return;
        }
        total_received += received;
    }
}

// Configuration file reading - unified format for both master and slaves
int read_config_master(SlaveConfig **slaves, int *num_slaves) {
    FILE *fp = fopen(CONFIG_FILE, "r");
    if (!fp) {
        perror("fopen config.txt");
        return -1;
    }
    
    // First line: number of slaves
    fscanf(fp, "%d", num_slaves);
    *slaves = (SlaveConfig *)malloc(sizeof(SlaveConfig) * (*num_slaves));
    
    // Following lines: IP and port for each slave
    for (int i = 0; i < *num_slaves; i++) {
        fscanf(fp, "%s %d", (*slaves)[i].ip, &(*slaves)[i].port);
    }
    
    fclose(fp);
    return 0;
}

//Function for generating a generate_random number seeded on current time
double generate_random(int max){
    return (rand() % max) + 1;
}

//Function for printing the matrix
void print_matrix(double **matrix, int rows, int cols) {
    for (int i = 0; i < rows; i++) {
        for (int j = 0; j < cols; j++) {
            printf("%8.4f ", matrix[i][j]);
        }
        printf("\n");
    }
}

//Function for getting the max from a matrix (to be improved)
double max(double **mat, int size, int col){
    double max = mat[0][col];
    for (int i = 1; i < size; i++){
        max = (mat[i][col] >= max) ? mat[i][col] : max;
    }

    return max;
}

//Function for getting the min from the matrix (to be improved)
double min(double **mat, int size, int col){
    double min = mat[0][col];
    for (int i = 1; i < size; i++){
        min = (mat[i][col] < min) ? mat[i][col] : min;
    }

    return min;
}

//Function for generating a matrix given dimensions
double ** generate_matrix(int row, int col){
    double **temp = (double**)malloc(sizeof(double*)*row);

    for(int i = 0; i < row; i++){
        temp[i] = (double *)(malloc(sizeof(double) * col));
    }
    return temp;
}


//Function for computing the MMT (new mat)
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

void free_matrix(double **matrix, int rows) {
    for (int i = 0; i < rows; i++) {
        free(matrix[i]);
    }
    free(matrix);
}

//Helper functions for elapsed time sourced from stackoverflow (for higher resolution)
int64_t timestamp_now (void)
{
    struct timeval tv;
    gettimeofday (&tv, NULL);
    return (int64_t) tv.tv_sec * CLOCKS_PER_SEC + tv.tv_usec;
}

double timestamp_to_seconds (int64_t timestamp)
{
    return timestamp / (double) CLOCKS_PER_SEC;
}
