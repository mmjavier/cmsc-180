/*
@author: Myko Jefferson M. Javier
@date: January 27, 2026
@section: CMSC 180 - CD3L
@code-desc: Implementing Min-Max Transformation on an nxn matrix in C
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
    double **matrix;
    int n;           // matrix dimension (rows)
    int start_col;   // starting column for this thread
    int num_cols;    // number of columns to process
} thread_data_t;

//Function Prototypes
void mmt(double ** mat, int n,  int m);
void *thread_mmt(void *arg);

//Helper Functions
double min (double **mat, int size, int col);
double max (double **mat, int size, int col);
double generate_random(int max);
void print_matrix(double **matrix, int n);
double ** generate_matrix(int row, int col);
int64_t timestamp_now (void);
double timestamp_to_seconds (int64_t timestamp);

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
    thread_data_t *thread_data = (thread_data_t *)malloc(sizeof(thread_data_t) * t);
    
    if (threads == NULL || thread_data == NULL) {
        fprintf(stderr, "Error: Failed to allocate memory for thread structures\n");
        for (int i = 0; i < n; i++){
            free(matrix[i]);
        }
        free(matrix);
        exit(1);
    }
    
    //Calculate columns per thread
    int cols_per_thread = n / t;
    int remaining_cols = n % t;
    
    //Log time start
    int64_t start = timestamp_now();
    
    //Create t threads
    int current_col = 0;
    for (int i = 0; i < t; i++){
        thread_data[i].matrix = matrix;
        thread_data[i].n = n;
        thread_data[i].start_col = current_col;
        thread_data[i].num_cols = cols_per_thread + (i < remaining_cols ? 1 : 0);
        
        pthread_create(&threads[i], NULL, thread_mmt, &thread_data[i]);
        
        current_col += thread_data[i].num_cols;
    }
    
    //Wait for all threads to complete
    for (int i = 0; i < t; i++){
        pthread_join(threads[i], NULL);
    }
    
    int64_t end = timestamp_now();
    printf("Elapsed Time:\n%lf\n", timestamp_to_seconds(end - start));

    //Cleanup
    free(threads);
    free(thread_data);
    for (int i = 0; i < n; i++){
        free(matrix[i]);
    }
    free(matrix);
    
    return 0;
}

//Thread function for MMT computation
void *thread_mmt(void *arg){
    thread_data_t *data = (thread_data_t *)arg;
    
    //Process columns from start_col to start_col + num_cols
    for (int i = data->start_col; i < data->start_col + data->num_cols; i++){
        double colMax = max(data->matrix, data->n, i);
        double colMin = min(data->matrix, data->n, i);
        
        //Apply min-max normalization to this column
        for (int j = 0; j < data->n; j++){
            data->matrix[j][i] = (data->matrix[j][i] - colMin) / (colMax - colMin);
        }
    }
    
    return NULL;
}

//Function for generating a generate_random number seeded on current time
double generate_random(int max){
    return (rand() % max) + 1;
}

//Function for printing a matrix
void print_matrix(double **matrix, int n){
    for (int i = 0; i < n; i++){
        for (int j = 0; j < n; j++){
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

//Function for generating a matrix given dimensions
double ** generate_matrix(int row, int col){
    double **temp = (double**)malloc(sizeof(double*)*row);

    for(int i = 0; i < row; i++){
        temp[i] = (double *)(malloc(sizeof(double) * col));
    }

    return temp;
}
//Function for getting the min from the matrix (to be improved)
double min(double **mat, int size, int col){
    double min = mat[0][col];
    for (int i = 1; i < size; i++){
        min = (mat[i][col] < min) ? mat[i][col] : min;
    }

    return min;
}

//Function for computing the MMT (new mat)
void mmt(double ** matrix, int n, int m){

    double colMax, colMin; 

    for (int i=0; i < n; i++){
        colMax = max(matrix, n, i);
        colMin = min(matrix, n, i);

        for (int j = 0; j < n; j++){
            matrix[j][i] = (matrix[j][i] - colMin) / (colMax - colMin);
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



