#include <mpi.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unordered_map>
#include <vector>

#define TRACKER_RANK 0
#define MAX_FILES 10
#define MAX_FILENAME 15
#define HASH_SIZE 32
#define MAX_CHUNKS 100

using namespace std;

// tipuri de mesaje catre tracker 
enum Message {
    DOWNLOAD = 1,
    UPDATE = 2,
    FINISHED = 3,
    TERMINATED = 4
};

struct chunk {
    int position;
    char hash[HASH_SIZE];
};

// informatiile unui fisier detinut
struct client_file {
    char name[MAX_FILENAME];
    int num_chunks;
    struct chunk chunks[MAX_CHUNKS];
};

// informatiile unui fisier ce vrea sa fie descarcat
struct peer_file {
    int peer;
    int num_chunks;
    struct chunk chunks[MAX_CHUNKS];
};

// numarul de fisiere detinute / dorite
int have_files, want_files;
// fisierele detinute
struct client_file files[MAX_FILES];
// fisierele dorite
struct client_file download_files[MAX_FILES];

void *download_thread_func(void *arg)
{
    int rank = *(int*) arg;
    MPI_Status mpi_status;

    // Descarcare
    for (int i = 0; i < want_files; i++) {
        int final_chunk = 0;

        // 1. cere tracker-ului lista de seeds/peers pentru fisierul dorit
        Message message = DOWNLOAD;
        MPI_Send(&message, 1, MPI_INT, TRACKER_RANK, 0, MPI_COMM_WORLD);
        MPI_Send(download_files[i].name, MAX_FILENAME, MPI_CHAR, TRACKER_RANK, 0, MPI_COMM_WORLD);

        int no_peers;
        vector<struct peer_file> peers;
        MPI_Recv(&no_peers, 1, MPI_INT, TRACKER_RANK, 0, MPI_COMM_WORLD, &mpi_status);

        for (int j = 0; j < no_peers; j++) {
            struct peer_file received_file;
            MPI_Recv(&received_file.peer, 1, MPI_INT, TRACKER_RANK, 0, MPI_COMM_WORLD, &mpi_status);
            MPI_Recv(&received_file.num_chunks, 1, MPI_INT, TRACKER_RANK, 0, MPI_COMM_WORLD, &mpi_status);

            if (received_file.num_chunks > final_chunk) {
                final_chunk = received_file.num_chunks;
            }

            for (int k = 0; k < received_file.num_chunks; k++) {
                MPI_Recv(&received_file.chunks[k].position, 1, MPI_INT, TRACKER_RANK, 0, MPI_COMM_WORLD, &mpi_status);
                MPI_Recv(received_file.chunks[k].hash, HASH_SIZE, MPI_CHAR, TRACKER_RANK, 0, MPI_COMM_WORLD, &mpi_status);
            }

            peers.push_back(received_file);
        }

        // 2. incepe sa trimita cereri catre peers/seeds
        int current_peer = 0;
        int update = 0;

        for (int current_chunk = 0; current_chunk < final_chunk; current_chunk++) {
            int ok = 0;

            while (ok == 0) { 
                if (peers[current_peer].num_chunks > current_chunk) {
                    // 3. pentru fiecare segment dorit se realizeaza pasii
                    // b. ii trimite acelui seed/peer o cerere pentru segment
                    MPI_Send(download_files[i].name, MAX_FILENAME, MPI_CHAR, peers[current_peer].peer, 1, MPI_COMM_WORLD);
                    MPI_Send(&current_chunk, 1, MPI_INT, peers[current_peer].peer, 3, MPI_COMM_WORLD);

                    // c. asteapta sa primeasca de la seed/peer segmentul cerut
                    char hash[HASH_SIZE];
                    MPI_Recv(hash, HASH_SIZE, MPI_CHAR, peers[current_peer].peer, 2, MPI_COMM_WORLD, &mpi_status);

                    // d. marcheaza segmentul ca primit
                    download_files[i].chunks[current_chunk].position = current_chunk + 1;
                    strncpy(download_files[i].chunks[current_chunk].hash, hash, HASH_SIZE);

                    ok = 1;
                }

                // a. alege un seed/peer care detine segmentul cautat
                ++current_peer;
                if (current_peer == no_peers) {
                    current_peer = 0;
                }
            }

            ++update;
            if (update == 10) {
                // Actualizare
                // 1. Informeaza tracker-ul despre segmentele pe care le detine
                download_files[i].num_chunks = current_chunk + 1;

                Message message = UPDATE;
                MPI_Send(&message, 1, MPI_INT, TRACKER_RANK, 0, MPI_COMM_WORLD);
                MPI_Send(download_files[i].name, MAX_FILENAME, MPI_CHAR, TRACKER_RANK, 0, MPI_COMM_WORLD);
                MPI_Send(&download_files[i].num_chunks, 1, MPI_INT, TRACKER_RANK, 0, MPI_COMM_WORLD);

                for (int j = 0; j < download_files[i].num_chunks; j++) {
                    MPI_Send(&download_files[i].chunks[j].position, 1, MPI_INT, TRACKER_RANK, 0, MPI_COMM_WORLD);
                    MPI_Send(download_files[i].chunks[j].hash, HASH_SIZE, MPI_CHAR, TRACKER_RANK, 0, MPI_COMM_WORLD);
                }

                // 2. Reia pasii de la sectiunea de descarcare
                peers.clear();

                Message message2 = DOWNLOAD;
                MPI_Send(&message2, 1, MPI_INT, TRACKER_RANK, 0, MPI_COMM_WORLD);
                MPI_Send(download_files[i].name, MAX_FILENAME, MPI_CHAR, TRACKER_RANK, 0, MPI_COMM_WORLD);

                MPI_Recv(&no_peers, 1, MPI_INT, TRACKER_RANK, 0, MPI_COMM_WORLD, &mpi_status);

                for (int j = 0; j < no_peers; j++) {
                    struct peer_file received_file;
                    MPI_Recv(&received_file.peer, 1, MPI_INT, TRACKER_RANK, 0, MPI_COMM_WORLD, &mpi_status);
                    MPI_Recv(&received_file.num_chunks, 1, MPI_INT, TRACKER_RANK, 0, MPI_COMM_WORLD, &mpi_status);

                    if (received_file.num_chunks > final_chunk) {
                        final_chunk = received_file.num_chunks;
                    }

                    for (int k = 0; k < received_file.num_chunks; k++) {
                        MPI_Recv(&received_file.chunks[k].position, 1, MPI_INT, TRACKER_RANK, 0, MPI_COMM_WORLD, &mpi_status);
                        MPI_Recv(received_file.chunks[k].hash, HASH_SIZE, MPI_CHAR, TRACKER_RANK, 0, MPI_COMM_WORLD, &mpi_status);
                    }

                    peers.push_back(received_file);
                }

                update = 0;
            }
        }

        // Finalizare descarcare fisier
        // 1. Informeaza tracker-ul ca are tot fisierul
        download_files[i].num_chunks = final_chunk;

        Message msg2 = FINISHED;
        MPI_Send(&msg2, 1, MPI_INT, TRACKER_RANK, 0, MPI_COMM_WORLD);
        MPI_Send(download_files[i].name, MAX_FILENAME, MPI_CHAR, TRACKER_RANK, 0, MPI_COMM_WORLD);
        MPI_Send(&download_files[i].num_chunks, 1, MPI_INT, TRACKER_RANK, 0, MPI_COMM_WORLD);

        for (int j = 0; j < download_files[i].num_chunks; j++) {
            MPI_Send(&download_files[i].chunks[j].position, 1, MPI_INT, TRACKER_RANK, 0, MPI_COMM_WORLD);
            MPI_Send(download_files[i].chunks[j].hash, HASH_SIZE, MPI_CHAR, TRACKER_RANK, 0, MPI_COMM_WORLD);
        }

        // 2. Salveaza fisierul intr-un fisier de iesire
        char *char_rank = (char *) malloc(sizeof(char) * MAX_FILENAME);
        sprintf(char_rank, "%d", rank);

        char *close_file = (char *) malloc(sizeof(char) * MAX_FILENAME);
        strcpy(close_file, "client");
        strcat(close_file, char_rank);
        strcat(close_file, "_");
        strcat(close_file, download_files[i].name);

        FILE *f2 = fopen(close_file, "w");
        if (f2 == NULL) {
            printf("Eroare la fopen\n");
            exit(-1);
        }

        for (int j = 0; j < download_files[i].num_chunks; j++) {
            char print_hash[HASH_SIZE + 1];
            strncpy(print_hash, download_files[i].chunks[j].hash, HASH_SIZE);
            print_hash[HASH_SIZE] = '\0';
            fprintf(f2, "%s\n", print_hash);
        }

        if (rank == 1) {
            printf("Am descarcat fisierul %s\n", download_files[i].name);
            cout << download_files[i].num_chunks << endl;

            for (int j = 0; j < download_files[i].num_chunks; j++) {
                char print_hash[HASH_SIZE + 1];
                strncpy(print_hash, download_files[i].chunks[j].hash, HASH_SIZE);
                print_hash[HASH_SIZE] = '\0';
                printf("%s\n", print_hash);
            }
        }

        fclose(f2);
        free(char_rank);
        free(close_file);        
    }

    // Finalizare descarcare toate fisierele
    // 1. Ii trimite tracker-ului informatia ca a terminat toate descarcarile
    Message msg = TERMINATED;
    MPI_Send(&msg, 1, MPI_INT, TRACKER_RANK, 0, MPI_COMM_WORLD);

    // 2. Inchide firul de executie de download
    return NULL;
}

void *upload_thread_func(void *arg)
{
    MPI_Status status;
    
    // Primire de mesaje de la alti clienti
    while(true) {
        char filename[MAX_FILENAME];
        int chunk;
        int found = 0;

        // primeste numele fisierului si id-ul segmentului dorit
        MPI_Recv(filename, MAX_FILENAME, MPI_CHAR, MPI_ANY_SOURCE, 1, MPI_COMM_WORLD, &status);

        if (strcmp(filename, "STOP") == 0) {
            break;
        }

        MPI_Recv(&chunk, 1, MPI_INT, status.MPI_SOURCE, 3, MPI_COMM_WORLD, &status);
        char hash[HASH_SIZE];

        // cauta hash-ul corespunzator
        for (int i = 0; i < have_files; i++) {
            if (strcmp(files[i].name, filename) == 0) {
                strncpy(hash, files[i].chunks[chunk].hash, HASH_SIZE);
                found = 1;
                break;
            }
        }

        if (found == 0) {
            for (int i = 0; i < want_files; i++) {
                if (strcmp(download_files[i].name, filename) == 0) {
                    strncpy(hash, download_files[i].chunks[chunk].hash, HASH_SIZE);
                    break;
                }
            }
        }
        
        // trimite hash-ul
        MPI_Send(hash, HASH_SIZE, MPI_CHAR, status.MPI_SOURCE, 2, MPI_COMM_WORLD);
    }
    
    return NULL;
}

void tracker(int numtasks, int rank) {
    // swarm-ul fisierelor
    unordered_map<string, vector<int>> has_files;
    unordered_map<int, vector<struct client_file>> total_files;
    
    MPI_Status status;

    // Initializare
    for (int i = 1; i < numtasks; i++) {
        // 1. asteapta mesajul initial de la fiecare client
        int have_files;
        MPI_Recv(&have_files, 1, MPI_INT, i, 0, MPI_COMM_WORLD, &status);

        // 2. trece clientul in swarm-ul fisierului
        for (int j = 0; j < have_files; j++) {
            struct client_file file;
            MPI_Recv(file.name, MAX_FILENAME, MPI_CHAR, i, 0, MPI_COMM_WORLD, &status);
            MPI_Recv(&file.num_chunks, 1, MPI_INT, i, 0, MPI_COMM_WORLD, &status);

            for (int k = 0; k < file.num_chunks; k++) {
                MPI_Recv(&file.chunks[k].position, 1, MPI_INT, i, 0, MPI_COMM_WORLD, &status);
                MPI_Recv(file.chunks[k].hash, HASH_SIZE, MPI_CHAR, i, 0, MPI_COMM_WORLD, &status);
            }

            has_files[file.name].push_back(i);
            total_files[i].push_back(file);
        }
    }

    // 3. cand a primit de la toti clientii, raspunde ACK
    for (int i = 1; i < numtasks; i++) {
        MPI_Send("ACK", 5, MPI_CHAR, i, 0, MPI_COMM_WORLD);
    }

    // Primire mesaje de la clienti
    int finished = 0;
    
    // cat timp mai exista clienti care nu au terminat descarcarea
    while (finished < numtasks - 1) {
        Message value;
        MPI_Recv(&value, 1, MPI_INT, MPI_ANY_SOURCE, 0, MPI_COMM_WORLD, &status);

        if (value == DOWNLOAD) {
            // cerere pentru un fisier
            char name[MAX_FILENAME];
            MPI_Recv(name, MAX_FILENAME, MPI_CHAR, status.MPI_SOURCE, 0, MPI_COMM_WORLD, &status);

            int peers = has_files[name].size();
            MPI_Send(&peers, 1, MPI_INT, status.MPI_SOURCE, 0, MPI_COMM_WORLD);

            for (int i = 0; i < has_files[name].size(); i++) {
                int client = has_files[name][i];
                MPI_Send(&client, 1, MPI_INT, status.MPI_SOURCE, 0, MPI_COMM_WORLD);
                
                for (int j = 0; j < total_files[client].size(); j++) {
                    if (strcmp(total_files[client][j].name, name) == 0) {
                        int no_chunks = total_files[client][j].num_chunks;
                        MPI_Send(&no_chunks, 1, MPI_INT, status.MPI_SOURCE, 0, MPI_COMM_WORLD);

                        for (int k = 0; k < no_chunks; k++) {
                            MPI_Send(&total_files[client][j].chunks[k].position, 1, MPI_INT, status.MPI_SOURCE, 0, MPI_COMM_WORLD);
                            MPI_Send(total_files[client][j].chunks[k].hash, HASH_SIZE, MPI_CHAR, status.MPI_SOURCE, 0, MPI_COMM_WORLD);
                        }
                    }
                }
            }
        } else if (value == UPDATE) {
            // actualizare de la client
            struct client_file file;
            MPI_Recv(file.name, MAX_FILENAME, MPI_CHAR, status.MPI_SOURCE, 0, MPI_COMM_WORLD, &status);
            MPI_Recv(&file.num_chunks, 1, MPI_INT, status.MPI_SOURCE, 0, MPI_COMM_WORLD, &status);

            for (int i = 0; i < file.num_chunks; i++) {
                MPI_Recv(&file.chunks[i].position, 1, MPI_INT, status.MPI_SOURCE, 0, MPI_COMM_WORLD, &status);
                MPI_Recv(file.chunks[i].hash, HASH_SIZE, MPI_CHAR, status.MPI_SOURCE, 0, MPI_COMM_WORLD, &status);
            }

            // verificam daca fisierul exista deja in swarm
            int found = 0;

            for (int i = 0; i < has_files[file.name].size(); i++) {
                int client = has_files[file.name][i];

                if (client == status.MPI_SOURCE) {
                    found = 1;
                    break;
                }
            }

            if (found == 0) {
                has_files[file.name].push_back(status.MPI_SOURCE);
            } else {
                for (int i = 0; i < total_files[status.MPI_SOURCE].size(); i++) {
                    if (strcmp(total_files[status.MPI_SOURCE][i].name, file.name) == 0) {
                        total_files[status.MPI_SOURCE].erase(total_files[status.MPI_SOURCE].begin() + i);
                        break;
                    }
                }
            }

            total_files[status.MPI_SOURCE].push_back(file);
        } else if (value == FINISHED) {
            // finalizare descarcare fisier
            struct client_file file;
            MPI_Recv(file.name, MAX_FILENAME, MPI_CHAR, status.MPI_SOURCE, 0, MPI_COMM_WORLD, &status);
            MPI_Recv(&file.num_chunks, 1, MPI_INT, status.MPI_SOURCE, 0, MPI_COMM_WORLD, &status);

            for (int i = 0; i < file.num_chunks; i++) {
                MPI_Recv(&file.chunks[i].position, 1, MPI_INT, status.MPI_SOURCE, 0, MPI_COMM_WORLD, &status);
                MPI_Recv(file.chunks[i].hash, HASH_SIZE, MPI_CHAR, status.MPI_SOURCE, 0, MPI_COMM_WORLD, &status);
            }

            // verificam daca fisierul exista deja in swarm
            int found = 0;

            for (int i = 0; i < has_files[file.name].size(); i++) {
                int client = has_files[file.name][i];

                if (client == status.MPI_SOURCE) {
                    found = 1;
                    break;
                }
            }

            if (found == 0) {
                has_files[file.name].push_back(status.MPI_SOURCE);
            } else {
                for (int i = 0; i < total_files[status.MPI_SOURCE].size(); i++) {
                    if (strcmp(total_files[status.MPI_SOURCE][i].name, file.name) == 0) {
                        total_files[status.MPI_SOURCE].erase(total_files[status.MPI_SOURCE].begin() + i);
                        break;
                    }
                }
            }

            total_files[status.MPI_SOURCE].push_back(file);
        } else if (value == TERMINATED) {
            // finalizare descarcare toate fisierele
            finished++;
        }
    }

    // Trimitere mesaje de finalizare
    for (int i = 1; i < numtasks; i++) {
        char msg[5] = "STOP";
        MPI_Send(msg, 5, MPI_CHAR, i, 1, MPI_COMM_WORLD);
    }
}

void peer(int numtasks, int rank) {
    pthread_t download_thread;
    pthread_t upload_thread;
    void *status;
    int r;
    MPI_Status mpi_status;

    // Initializare
    // 1. citeste fisierul de intrare
    char *char_rank = (char *) malloc(sizeof(char) * MAX_FILENAME);
    sprintf(char_rank, "%d", rank);

    char *file = (char *) malloc(sizeof(char) * MAX_FILENAME);
    strcpy(file, "in");
    strcat(file, char_rank);
    strcat(file, ".txt");

    FILE *f = fopen(file, "r");
    if (f == NULL) {
        printf("Eroare la fopen\n");
        exit(-1);
    }

    fscanf(f, "%d", &have_files);

    for (int i = 0; i < have_files; i++) {
        fscanf(f, "%s", files[i].name);
        fscanf(f, "%d", &files[i].num_chunks);
        
        for (int j = 0; j < files[i].num_chunks; j++) {
            files[i].chunks[j].position = j;
            fscanf(f, "%s", files[i].chunks[j].hash);
        }
    }

    fscanf(f, "%d", &want_files);

    for (int i = 0; i < want_files; i++) {
        fscanf(f, "%s", download_files[i].name);
    }

    // 2. ii spune tracker-ului ce fisiere are
    MPI_Send(&have_files, 1, MPI_INT, TRACKER_RANK, 0, MPI_COMM_WORLD);
    
    for (int i = 0; i < have_files; i++) {
        MPI_Send(files[i].name, MAX_FILENAME, MPI_CHAR, TRACKER_RANK, 0, MPI_COMM_WORLD);
        MPI_Send(&files[i].num_chunks, 1, MPI_INT, TRACKER_RANK, 0, MPI_COMM_WORLD);

        for (int j = 0; j < files[i].num_chunks; j++) {
            MPI_Send(&files[i].chunks[j].position, 1, MPI_INT, TRACKER_RANK, 0, MPI_COMM_WORLD);
            MPI_Send(files[i].chunks[j].hash, HASH_SIZE, MPI_CHAR, TRACKER_RANK, 0, MPI_COMM_WORLD);
        }
    }

    // 3. asteapta raspuns de la tracker
    char response[5];
    MPI_Recv(response, 5, MPI_CHAR, TRACKER_RANK, 0, MPI_COMM_WORLD, &mpi_status);

    r = pthread_create(&download_thread, NULL, download_thread_func, (void *) &rank);
    if (r) {
        printf("Eroare la crearea thread-ului de download\n");
        exit(-1);
    }

    r = pthread_create(&upload_thread, NULL, upload_thread_func, (void *) &rank);
    if (r) {
        printf("Eroare la crearea thread-ului de upload\n");
        exit(-1);
    }

    r = pthread_join(download_thread, &status);
    if (r) {
        printf("Eroare la asteptarea thread-ului de download\n");
        exit(-1);
    }

    r = pthread_join(upload_thread, &status);
    if (r) {
        printf("Eroare la asteptarea thread-ului de upload\n");
        exit(-1);
    }

    fclose(f);
    free(char_rank);
    free(file);
}
 
int main (int argc, char *argv[]) {
    int numtasks, rank;
 
    int provided;
    MPI_Init_thread(&argc, &argv, MPI_THREAD_MULTIPLE, &provided);
    if (provided < MPI_THREAD_MULTIPLE) {
        fprintf(stderr, "MPI nu are suport pentru multi-threading\n");
        exit(-1);
    }
    MPI_Comm_size(MPI_COMM_WORLD, &numtasks);
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);

    if (rank == TRACKER_RANK) {
        tracker(numtasks, rank);
    } else {
        peer(numtasks, rank);
    }

    MPI_Finalize();
}
