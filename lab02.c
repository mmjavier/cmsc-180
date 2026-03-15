/*
@author: Myko Jefferson M. Javier
@date: January 27, 2026
@section: CMSC 180 - CD3L
@code-desc: Implementing Min-Max Transformation on an nxn matrix in C using Threads
*/

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <sys/time.h>
#include <unistd.h>
#include <time.h>
#include <pthread.h>

//Thread data structure
typedef struct {
    double **matrix;     // Pointer to the full matrix
    int id;             // Thread Name
    int n;              // Matrix dimension (n x n)
    int start_col;      // Starting column for this thread
    int num_cols;       // Number of columns to process (n/t)
} args;

//Function Prototypes
void mmt(double **mat, int n, int start_col, int num_cols);
void *thread_mmt(void *arg);

//Helper Functions
double min (double **mat, int n, int col);
double max (double **mat, int n, int col);

double generate_random(int max);
void print_matrix(double **matrix, int row, int start_col, int cols_to_print);
double **generate_matrix(int row, int col);
int64_t timestamp_now (void);
double timestamp_to_seconds (int64_t timestamp);
void transform_matrix(double **mat, int n);

//Main Function
int main(int argc, char **argv){
    //Variables
    int n, t;
    double **matrix;

    //Check if a file is inputed from the exec call
    if (argc < 2){
        //Ask user for input
        printf("Enter dimension (n): ");
        scanf("%d", &n);
        printf("Enter thread count (t): ");
        scanf("%d", &t);

        //Initialize the nxn matrix
        matrix = generate_matrix(n, n);

        //Create a matrix of size N with generate_random values from 1-100
        srand(time(NULL)); //Seed Time

        //Loop for generating Random values for the nxn matrix
        for (int i = 0; i < n; i++){
            for(int j = 0; j < n; j++){
                matrix[i][j] = generate_random(100);
            }
        }
    }else{
        FILE *fptr = fopen(argv[1], "r");

        //First line is dimension, second is thread count
        fscanf(fptr, "%d", &n);
        fscanf(fptr, "%d", &t);

        //Allocate Matrix of size n
        matrix = generate_matrix(n,n);

        for(int i = 0; i < n; i++){
            for(int j = 0; j < n; j++){
                fscanf(fptr, "%lf", &matrix[i][j]);
            }
        }
        
        fclose(fptr);
    }

    //Create thread data structures
    pthread_t *threads = (pthread_t *)malloc(sizeof(pthread_t) * t);
    args *arguments = (args *)malloc(sizeof(args) * t);
    
    if (threads == NULL || arguments == NULL) {
        printf("Error: Failed to allocate memory for thread structures\n");
        free(matrix);
        return 0;
    }
    
    //Calculate columns per thread (divide matrix column-wise into t submatrices)
    int cols_per_thread = n / t;
    int remaining_cols = n % t;
    
    //Log time start
    int64_t start = timestamp_now();
    
    //Create t threads, each processes n/t columns of the full matrix
    int current_col = 0;
    for (int i = 0; i < t; i++){
        int thread_cols = cols_per_thread + (i < remaining_cols ? 1 : 0);
        
        arguments[i].matrix = matrix;
        arguments[i].n = n;
        arguments[i].id = i;
        arguments[i].start_col = current_col;
        arguments[i].num_cols = thread_cols;  // n/t columns
        
        pthread_create(&threads[i], NULL, thread_mmt, &arguments[i]);
        
        current_col += thread_cols;
    }
    
    //Wait for all threads to complete
    for (int i = 0; i < t; i++){
        pthread_join(threads[i], NULL);
    }
    
    int64_t end = timestamp_now();
    printf("Elapsed Time:\n%lf\n", timestamp_to_seconds(end - start));
    
    //Cleanup
    free(threads);
    free(arguments);
    free(matrix);
    
    return 0;
}

//Function for transposing the matrix
void transpose_matrix(double *matrix, int n){
    double temp;
    for (int i = 0; i < n; i++){
        for (int j = i + 1; j < n; j++){
            
            //Calculate 1d indices
            int index1 = i * n + j;
            int index2 = j * n + i;

            //Swap
            temp = matrix[index1];
            matrix[index1] = matrix[index2];
            matrix[index2] = temp;
        }
    }
}

//Thread function - processes n/t columns of the matrix
void *thread_mmt(void *arg){
    args *data = (args *)arg;
    
    // Call mmt on this thread's column range mmt for columns [start_col, start_col + num_cols)
    // printf("Thread %d\n", data->id);
    mmt(data->matrix, data->n, data->start_col, data->num_cols);

    // print_matrix(data->matrix, data->n, data->start_col, data->num_cols);
    return NULL;
}

//Function for generating a generate_random number seeded on current time
double generate_random(int max){
    return (rand() % max) + 1;
}

//Function for printing a matrix
void print_matrix(double **matrix, int row, int start_col, int cols_to_print){
    for (int i = 0; i < row; i++){
        for (int j = start_col; j < start_col + cols_to_print; j++){
            printf(" %0.2g\t",  matrix[i][j]);
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

//Function for computing the MMT (new mat)
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



