#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <sys/ipc.h>
#include <sys/shm.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <semaphore.h>

#include <time.h>

#define BUFFER_SIZE 1000    // buffer size is for reading lines from file
                            // ( because the file can contain lines with > 100 characters
                            // but the line is requested will have at most 100 characters
#define LINE_SIZE 100
#define PERMS 0666


typedef struct shared_memory{
    int random_line_number;             // the number of the line which will be requested
    char requested_line[LINE_SIZE];     // the line itself which will be returnet

    // 3 unnamed semaphores as part of the shared memory
    sem_t client_to_other_clients;
    sem_t client_to_server;
    sem_t server_to_client;

} shared_memory;


int main(int argc, char* argv[]){

    // get line arguments
    char* file_name = argv[1];
    int number_of_childs = atoi(argv[2]);   // K
    int number_of_requests = atoi(argv[3]); // N

    // A File with name file_name is given by the command line arguments, open it and count it's lines //
    FILE *file = fopen(file_name, "r");

    if ( file == NULL ){
        printf("Could not open file with name: '%s' \n", file_name);
        exit(EXIT_FAILURE);
    }

    int number_of_lines = 0;
    char line[LINE_SIZE];
    while ( fgets(line, BUFFER_SIZE, file) != NULL ){
        number_of_lines++;
    }
    fclose(file);

     // create the shared memory. Since it's done before fork() both parent and child will have access
     // to the same shared memory, as so to the number_of_lines and number_of_requests (they don't need to be passed to the childs)
    int segment_id;
    shared_memory* segment;

    segment_id = shmget(IPC_PRIVATE, sizeof(shared_memory), IPC_CREAT | PERMS);
    if ( segment_id == -1 ){
        perror("Shared Memory Create");
        exit(EXIT_FAILURE);
    }

    segment = shmat(segment_id, NULL, 0);   // attach to shared memory
    if ( segment == (void *) -1 ){
        perror("Shared Memory Attach");
        exit(EXIT_FAILURE);
    }

    if ( sem_init(&(segment->client_to_other_clients), 1, 1) == -1 ){
        perror("Initialize Semaphore");
        exit(EXIT_FAILURE);
    }

    if ( sem_init(&segment->client_to_server, 1, 0) == -1 ){
        perror("Initialize Semaphore");
        exit(EXIT_FAILURE);
    }

    if ( sem_init(&segment->server_to_client, 1, 0) == -1 ){
        perror("Initialize Semaphore");
        exit(EXIT_FAILURE);
    }

    pid_t pids[number_of_childs];

    for ( int i=0; i < number_of_childs; i++ ){
        pids[i] = fork();

        if ( pids[i] == -1 ){
            perror("fork");
            exit(EXIT_FAILURE);
        }

        if ( pids[i] == 0 )
            break;            // child must break loop, or there will be produced 2^K children
    }

    int is_child = 0;
    for (int i=0; i<number_of_childs; i++){
        if ( pids[i]==0 ){
            is_child = 1;
        }
    }

    if (is_child){
        // executing child process aka CLIENT...
        srand(time(NULL) + getpid()); // because children are created at the same time the seed will be same for all of them

        float run_time[number_of_requests];
        float total = 0;

        usleep(1);
        for (int i=0; i<number_of_requests; i++){

            sem_wait(&segment->client_to_other_clients);    // block all other clients

            //  -------- CRITICAL SECTION --------- //

            clock_t start, end;
            start = clock();

            // send request
            segment->random_line_number = rand() % number_of_lines + 1;
            printf("Client with ID %d Requesting: Deliver line %d ... \n", getpid(), segment->random_line_number);

            sem_post(&segment->server_to_client);           // unblock server to take the request

            sem_wait(&segment->client_to_server);           // wait till server responds

            end = clock();

            run_time[i] = ((float)(end-start))/CLOCKS_PER_SEC;
            total += run_time[i];

            printf("Client with ID %d Printing line: %s \n\n", getpid(), segment->requested_line);

            sem_post(&segment->client_to_other_clients);    // other clients now can make a reqest

            usleep(1);  // sleep for milliseconds to randomize the next transaction
        }
        sleep(1);
        printf("Average waiting time of child with ID %d is: %.10f\n\n", getpid(), total/number_of_requests);
    }
    else{
        // executing parent process aka SERVER...

        for( int i=0; i < number_of_childs*number_of_requests; i++ ){

            sem_wait(&segment->server_to_client);       // block server till he gets a request

            // open and read file, find the requested line and write it to the shared memory.
            file = fopen(file_name, "r");
            if ( file == NULL ){
                printf("Could not open file %s", file_name);
                exit(EXIT_FAILURE);
            }

            int count = 1;
            char line[BUFFER_SIZE];

            while ( fgets(line, BUFFER_SIZE, file) != NULL ){
                if ( count == segment->random_line_number ){
                    printf("Server Delivering Line... \n");
                    memcpy(segment->requested_line, line, LINE_SIZE);
                    break;
                }
                else
                    count++;
            }
            fclose(file);

            sem_post(&segment->client_to_server);   // unblock client to get the response
        }

        for (int i=0; i<number_of_childs; i++){
            wait(NULL);
        }

        // delete semaphores and shared memory
        if ( sem_destroy(&segment->client_to_other_clients) == -1 ){
            perror("Semaphore destroy");
            exit(EXIT_FAILURE);
        }

        if ( sem_destroy(&segment->client_to_server) == -1 ){
            perror("Semaphore destroy");
            exit(EXIT_FAILURE);
        }

        if ( sem_destroy(&segment->server_to_client) == -1 ){
            perror("Semaphore destroy");
            exit(EXIT_FAILURE);
        }

        if ( shmdt((void *) segment) == -1 ){
            perror("Shared Memory Detach");
            exit(EXIT_FAILURE);
        }

        if ( shmctl(segment_id, IPC_RMID, 0) == -1 ){
            perror("Shared Memory Remove");
            exit(EXIT_FAILURE);
        }

        return 0;
    }
}