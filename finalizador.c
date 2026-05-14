#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <semaphore.h>
#include <fcntl.h>
#include <time.h>
#include "common.h"

/* Agrega el registro de cierre a la bitacora antes de que el sistema se desmonte */
static void write_log_fin(void) {
    sem_t *sl = sem_open(SEM_LOG, 0);
    if (sl == SEM_FAILED) return;
    sem_wait(sl);
    FILE *f = fopen(LOG_FILE, "a");
    if (f) {
        time_t now = time(NULL);
        char ts[64];
        strftime(ts, sizeof(ts), "%Y-%m-%d %H:%M:%S", localtime(&now));
        fprintf(f, "[%s] FINALIZADOR | Sistema detenido y recursos liberados.\n", ts);
        fclose(f);
    }
    sem_post(sl);
    sem_close(sl);
}

int main(void) {
    printf("=== Finalizador del Sistema ===\n");

    /* Adjunta shm para leer el PID real del productor antes de destruirla */
    int shmid = shmget(SHM_KEY, sizeof(SharedMem), 0666);
    if (shmid == -1) {
        fprintf(stderr, "No se encontro memoria compartida. El sistema puede no estar activo.\n");
    } else {
        SharedMem *shm = (SharedMem *)shmat(shmid, NULL, 0);
        if (shm != (void *)-1) {
            pid_t prod_pid = shm->producer_pid;
            shmdt(shm);

            /* El archivo es mas confiable si el productor murio sin actualizar shm */
            FILE *pf = fopen(PID_FILE, "r");
            if (pf) {
                if (fscanf(pf, "%d", &prod_pid) != 1) prod_pid = 0;
                fclose(pf);
            }

            if (prod_pid > 0) {
                printf("Enviando SIGTERM al productor (PID real: %d)...\n", prod_pid);
                if (kill(prod_pid, SIGTERM) == 0) {
                    /* Da 3 segundos para que el productor termine limpiamente */
                    printf("Productor notificado. Esperando 3 segundos...\n");
                    sleep(3);
                    /* kill con senal 0 verifica existencia sin enviar senal real */
                    if (kill(prod_pid, 0) == 0) {
                        kill(prod_pid, SIGKILL);
                        printf("Productor eliminado con SIGKILL.\n");
                    }
                } else {
                    printf("Productor no encontrado (ya termino).\n");
                }
            }
            remove(PID_FILE);
        }

        /* Escribe en la bitacora antes de destruir el semaforo de log */
        write_log_fin();

        /* IPC_RMID: marca el segmento para eliminacion inmediata por el kernel */
        if (shmctl(shmid, IPC_RMID, NULL) == 0) {
            printf("Memoria compartida eliminada (shmid: %d).\n", shmid);
        } else {
            perror("shmctl IPC_RMID");
        }
    }

    /* sem_unlink elimina el nombre del semaforo; el objeto persiste hasta que todos lo cierren */
    int r1 = sem_unlink(SEM_MEM);
    int r2 = sem_unlink(SEM_LOG);
    int r3 = sem_unlink(SEM_STATE);
    printf("Semaforos eliminados:%s%s%s\n",
           r1 == 0 ? " mem" : "",
           r2 == 0 ? " log" : "",
           r3 == 0 ? " state" : "");

    printf("Finalizador terminado. Recursos liberados.\n");
    return 0;
}
