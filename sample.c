#include<stdio.h>
#include<string.h>    
#include<stdlib.h>    
#include<sys/socket.h>
#include<arpa/inet.h> 
#include<unistd.h>    
#include<sys/time.h>
#include<pthread.h> 
 

typedef struct node {
  int **ownMatrix, *new_sock;
  int slaveNo, rows, columns;
} slaves;

void printMatrix(int **matrixToPrint, int rows, int columns) {
  int i, j;
  for(i = 0; i < rows; i++) {
    for (j = 0; j < columns; j++)
      printf("%d ", matrixToPrint[i][j]);
    printf("\n");
  }
}


// the thread function
void *connection_handler(void *client) {

  //Get the socket descriptor
  slaves *slave = (slaves *) client;
  int sock = *(int *) slave->new_sock;
  int read_size;
  int i, j;
  char slaveMessage[256];
  
  int **matrixToSlave = slave->ownMatrix;
  int rows = slave->rows;
  int columns = slave->columns;
  int slaveNo = slave->slaveNo;
  printf("Slave %d entered handler.\n\n", slave->slaveNo);

  // Reset message so no trash values
  memset(slaveMessage, 0, 256);

  //Send some rows and columns to slave
  write(sock, &rows, sizeof(rows));
  write(sock, &columns, sizeof(columns));
  write(sock, &slaveNo, sizeof(slaveNo));

  // Loop until all elements are sent
  for (i = 0; i < rows; i++)
    for (j = 0; j < columns; j++)
      write(sock, &matrixToSlave[i][j], sizeof(matrixToSlave[i][j]));

  //Receive a message from slave
  read_size = recv(sock, slaveMessage, 256, 0);

  if(read_size < 0){
    perror("recv ack failed");
    pthread_exit(NULL);
  }

  else {
    printf("Message from Slave %d: %s\n", slave->slaveNo, slaveMessage);
    printf("Slave %d disconnected.\n\n", slave->slaveNo);
    fflush(stdout);
  }

  pthread_exit(NULL);
}



void slave() {
  int sock, i, j;
  struct sockaddr_in server;
  char ackMessage[256] = "ack";
  char masterIP[256];
  int **receivedMatrix, rows, columns, slaveNo;
  FILE *fp;

  fp = fopen("addresses.txt", "r");
  fscanf(fp, "%s", masterIP);
  fclose(fp);

  printf("Master IP: %s\n", masterIP);

  //Create socket
  sock = socket(AF_INET , SOCK_STREAM , 0);
  if (sock == -1) {
    perror("Could not create socket");
    return;
  }
  printf("Socket creation successful.\n");
   
  // Change IP to master's everytime you run as a slave
  server.sin_addr.s_addr = inet_addr(masterIP);
  server.sin_family = AF_INET;
  server.sin_port = htons(8888);


  //Connect to master
  if (connect(sock, (struct sockaddr *)&server , sizeof(server)) < 0) {
    perror("connect failed");
    return;
  }
  printf("Connect successful.\n");


  // Receive rows and columns, and slaveNo from master
  if(recv(sock, &rows, sizeof(rows), 0) < 0) {
    perror("recv rows failed");
    return;
  }
  if(recv(sock, &columns, sizeof(columns), 0) < 0) {
    perror("recv columns failed");
    return;
  }
  if(recv(sock, &slaveNo, sizeof(slaveNo), 0) < 0) {
    perror("recv slaveNo failed");
    return;
  }

  // Receive elements one by one (((inefficiently)))
  receivedMatrix = (int **) malloc(rows * sizeof(int *));
  for (i = 0; i < rows; i++){
    receivedMatrix[i] = (int *) malloc(columns * sizeof(int));
    for (j = 0; j < columns; j++) {
      if(recv(sock, &receivedMatrix[i][j], sizeof(receivedMatrix[i][j]), 0) < 0) {
        perror("recv Matrix failed");
        printf("Error @ [%d][%d]\n", i, j);
        return;
      }
    }
  }


  printf("\n");
  // printMatrix(receivedMatrix, rows, columns);

  printf("\nMatrix received. Sending 'ack'...\n");
  printf("%s => to send\n", ackMessage);
  //Send the acknowledgement message
  if(send(sock, ackMessage, strlen(ackMessage), 0) < 0) {
    puts("Send ack failed");
    return;
  }
  printf("'ack' sent.\n\n");
   
  close(sock);

  for (i = 0; i < rows; i++)
    free(receivedMatrix[i]);
  free(receivedMatrix);

  return;
}


// Master will use threads to handle multiple clients
void master() {

  struct timeval begin, end;
  double timeTaken;

  struct sockaddr_in server, client;
  int socket_desc, slave_sock, sockSize;
  int matrixSize, connectedSlaves, pthreadCreate, i, j, k, t;
  int submatrices, remainder, columnSize, endIndex;

  printf("Enter t: ");
  scanf("%d", &t);
  slaves arrayOfSlaves[t];
  pthread_t sniffer_thread[t];

  matrixSize = 25000;
  submatrices = matrixSize / t;
  remainder = matrixSize % t;

  // Prepare submatrices to the to-be-slaves
  srand(time(NULL));

  columnSize = submatrices;   // n / t
  endIndex = submatrices;     // the initial column end is = submatrices.
  for (i = 0; i < t; i++) {

    // last slave gets the remainder
    if (i == t - 1) {
      columnSize += remainder;
      endIndex += remainder;
    }
    arrayOfSlaves[i].ownMatrix = (int **) malloc(matrixSize * sizeof(int*));
    for (j = 0; j <  matrixSize; j++) {
      arrayOfSlaves[i].ownMatrix[j] = (int *) malloc(columnSize * sizeof(int));
      for (k = 0; k < columnSize; k++)
        arrayOfSlaves[i].ownMatrix[j][k] = rand() % 10 + 1;
    }

    endIndex += submatrices;     // increment to where the next would start
    arrayOfSlaves[i].slaveNo = i;
    arrayOfSlaves[i].rows = matrixSize;       
    arrayOfSlaves[i].columns = columnSize;      
  }

  printf("\nMatrix Prepared.\n\n");
  // int accessor = matrixSize / t;
  // for (i = 0; i < t; i++) {
  //   printf("Slave %d:\n", i);
  //   if( i == t - 1)
  //     accessor += remainder;
  //   printMatrix(arrayOfSlaves[i].ownMatrix, matrixSize, accessor);
  //   printf("\n");
  // }
  printf("\n");

  //Create socket
  socket_desc = socket(AF_INET, SOCK_STREAM, 0);
  if (socket_desc == -1){
    printf("Could not create socket\n");
    return;
  }
  printf("Socket creation successful.\n");

  //Prepare the sockaddr_in structure
  server.sin_family = AF_INET;
  server.sin_addr.s_addr = INADDR_ANY;
  server.sin_port = htons(8888);
   
  //Bind
  if(bind(socket_desc, (struct sockaddr *)&server, sizeof(server)) < 0) {
    perror("Bind failed");
    return;
  }
  printf("Binding successful\n");

  //Listen and accept and incoming connection
  listen(socket_desc, 3); 
  sockSize = sizeof(struct sockaddr_in);
  printf("Waiting for slaves...\n\n");



  gettimeofday(&begin, NULL);
  for(i = 1; i < t; i++) {

    slave_sock = accept(socket_desc, (struct sockaddr *) &client, (socklen_t *) &sockSize);
    
    printf("New slave entered. Total slaves: %d\n", i + 1);
    arrayOfSlaves[i].new_sock = malloc(1);
    *arrayOfSlaves[i].new_sock = slave_sock;

    // Send matrix through this function
    pthreadCreate = pthread_create(&sniffer_thread[i], NULL, connection_handler, (void *) &arrayOfSlaves[i]);
    if(pthreadCreate < 0) {
      perror("Unable to create thread");
      return;
    }

    printf("Handler assigned to new slave.\n");
    connectedSlaves++;
  }

  printf("\nAll slaves entered.\n\n");

  // Join the threads to terminate the program
  for(i = 1; i < t; i++){
    printf("Slave %d joined.\n", i);
    pthread_join(sniffer_thread[i], NULL);
  }

  gettimeofday(&end, NULL);
  printf("---End---\n");
  timeTaken =  (end.tv_sec - begin.tv_sec) + ((end.tv_usec - begin.tv_usec)/1000000.0);
  unsigned long long mill = 1000 * (end.tv_sec - begin.tv_sec) + (end.tv_usec - begin.tv_usec) / 1000;
  printf("Time taken: %f seconds\n", timeTaken);
  printf("Time taken: %llu milliseconds\n", mill);

  for (i = 1; i < t; i++) {
    for (j = 0; j < matrixSize; j++)
      free(arrayOfSlaves[i].ownMatrix[j]);
    free(arrayOfSlaves[i].ownMatrix);
  }

  if (slave_sock < 0) {
    perror("accept failed");
    return;
  }
}


int main(int argc , char *argv[]) {

  if (strcmp(argv[1], "0") == 0) 
    master();
  else if (strcmp(argv[1], "1") == 0)
    slave(argv[2]);
  
  return 0;

}