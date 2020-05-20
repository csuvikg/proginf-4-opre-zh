#include <stdio.h>
#include <sys/types.h>
#include <unistd.h>
#include <stdlib.h>
#include <time.h>
#include <string.h>
#include <sys/wait.h>
#include <signal.h>
#include <sys/ipc.h>
#include <sys/msg.h>
#include <sys/shm.h>
#include <sys/sem.h>
#include <sys/stat.h>
#include <stdbool.h>

#define MEMSIZE 1024

struct message {
    long mtype;
    char mtext[1024];
};

int send(int mq) {
    int num = rand() % 100000;
    time_t t = time(NULL);
    struct tm tm = *localtime(&t);
    char text[100];
    sprintf(text, "%d-%02d-%02d: %d", tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday, num);
    printf("Sending status: %s\n", text);

    const struct message msg = {1, text};

    int status;
    status = msgsnd(mq, &msg, strlen(text) + 1, 0);
    if (status < 0)
        perror("msgsnd");
    else printf("Message of status is sent\n");
    return 0;
}

int create_sem(const char *pathname, int sem_value) {
    int semid;
    key_t key;

    key = ftok(pathname, 1);
    if ((semid = semget(key, 1, IPC_CREAT | S_IRUSR | S_IWUSR)) < 0)
        perror("semget");
    if (semctl(semid, 0, SETVAL, sem_value) < 0)
        perror("semctl");

    return semid;
}

void sem_op(int semid, int op) {
    struct sembuf operation;

    operation.sem_num = 0;
    operation.sem_op = op;
    operation.sem_flg = 0;

    if (semop(semid, &operation, 1) < 0)
        perror("semop");
}

void delete_sem(int semid) {
    semctl(semid, 0, IPC_RMID);
}

void app_start_handler(int signumber) {
    printf("The app has started.\n");
}

int main(int argc, char *argv[]) {
    srand(time(NULL));
    pid_t user = fork();
    if (user == -1) {
        perror("Error forking process!");
        exit(EXIT_FAILURE);
    }

    // sem
    key_t sem_key;
    int sh_mem_id, semid;
    char *s;

    sem_key = ftok(argv[0], 1);
    sh_mem_id = shmget(sem_key, MEMSIZE, IPC_CREAT | S_IRUSR | S_IWUSR);
    s = shmat(sh_mem_id, NULL, 0);
    semid = create_sem(argv[0], 0);

    if (user > 0) {
        // service
        sem_op(semid, -1);
        printf("Status: %s\n", s);
        sem_op(semid, 1);
        shmdt(s);
        delete_sem(semid);
        shmctl(sh_mem_id, IPC_RMID, NULL);

        wait(NULL);
        printf("Service exiting\n");
    } else {
        signal(SIGTERM, app_start_handler);

        int login[2];
        char login_data[50];

        if (pipe(login) == -1) {
            perror("Error opening pipe!");
            exit(EXIT_FAILURE);
        }

        key_t key;
        int shm_id;
        char *image_storage;

        key = ftok(argv[0], 1);
        shm_id = shmget(key, 512, IPC_CREAT | S_IRUSR | S_IWUSR);
        image_storage = shmat(shm_id, NULL, 0);

        int mq, status;
        key_t mq_key;
        mq_key = ftok(argv[0], 1);
        mq = msgget(mq_key, 0600 | IPC_CREAT);

        pid_t app = fork();
        if (app == -1) {
            perror("Error forking process!");
            exit(EXIT_FAILURE);
        }

        if (app > 0) {
            // user
            pause();

            close(login[0]);
            write(login[1], "felhasznalo123, almafa-fradi", 50);
            close(login[1]);
            fflush(NULL);

            sleep(1);
            printf("Read from shared memory: %s", image_storage);
            shmdt(image_storage);

            bool is_acceptable = rand() % 2;
            printf("Image is %sacceptable.\n", is_acceptable ? "" : "not ");

            if (is_acceptable) {
                send(mq);
            } else {
                printf("Exiting...\n");
                exit(0);
            }

            wait(NULL);
            printf("User exiting\n");
        } else {
            // app
            sleep(1);
            kill(getppid(), SIGTERM);

            close(login[1]);
            read(login[0], login_data, 50);
            printf("User signed in with: %s\n", login_data);
            close(login[0]);

            char image[] = "<image in memory about standing of meter>\n";
            strcpy(image_storage, image);
            shmdt(image_storage);

            sleep(3);
            struct message msg;
            int st;
            st = msgrcv(mq, &msg, 1024, 1, 0);

            if (st < 0)
                perror("msgsnd");
            else {
                printf("Message received. Code: %ld\nContent:  %s\n", msg.mtype, msg.mtext);
                strcpy(s, msg.mtext);
                sem_op(semid, 1);
                shmdt(s);
            }

            printf("App exiting\n");
        }
    }
    return 0;
}
