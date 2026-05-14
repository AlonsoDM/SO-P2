#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <semaphore.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <time.h>
#include "common.h"

/* Registra la configuracion inicial en la bitacora y la deja lista para los demas */
static void write_log_init(int algo, int pages) {
    FILE *f = fopen(LOG_FILE, "w");
    if (!f) { perror("fopen bitacora"); return; }
    time_t now = time(NULL);
    char ts[64];
    strftime(ts, sizeof(ts), "%Y-%m-%d %H:%M:%S", localtime(&now));
    fprintf(f, "[%s] INICIALIZADOR | Algoritmo: %s | Paginas totales: %d\n",
            ts,
            algo == ALGO_PAGING ? "Paginacion" : "Segmentacion",
            pages);
    fclose(f);
}

int main(void) {
    int algo, total_pages;

    printf("=== Inicializador de Ambiente ===\n");
    printf("Seleccione algoritmo:\n");
    printf("  0 - Paginacion\n");
    printf("  1 - Segmentacion\n");
    printf("Opcion: ");
    if (scanf("%d", &algo) != 1 || (algo != 0 && algo != 1)) {
        fprintf(stderr, "Opcion invalida.\n");
        return 1;
    }

    printf("Cantidad de paginas/espacios de memoria: ");
    if (scanf("%d", &total_pages) != 1 || total_pages <= 0 || total_pages > MAX_PAGES) {
        fprintf(stderr, "Valor invalido (1-%d).\n", MAX_PAGES);
        return 1;
    }

    /* Limpia recursos de corridas anteriores para evitar colisiones de llave */
    int old_id = shmget(SHM_KEY, 0, 0666);
    if (old_id != -1) shmctl(old_id, IPC_RMID, NULL);
    sem_unlink(SEM_MEM);
    sem_unlink(SEM_LOG);
    sem_unlink(SEM_STATE);

    /* Solicita el segmento al SO; IPC_EXCL falla si ya existe (doble arranque) */
    int shmid = shmget(SHM_KEY, sizeof(SharedMem), IPC_CREAT | IPC_EXCL | 0666);
    if (shmid == -1) { perror("shmget"); return 1; }

    /* Adjunta el segmento al espacio de direcciones de este proceso */
    SharedMem *shm = (SharedMem *)shmat(shmid, NULL, 0);
    if (shm == (void *)-1) { perror("shmat"); return 1; }

    memset(shm, 0, sizeof(SharedMem));
    shm->algorithm   = algo;
    shm->total_pages = total_pages;
    shm->num_procs   = 0;
    shm->producer_pid = 0;
    shm->initialized = 1;

    /* -1 indica celda libre; el productor escribira el sim_pid del dueno */
    for (int i = 0; i < MAX_PAGES; i++) {
        shm->memory[i]  = -1;
        shm->mem_seg[i] = -1;
    }

    shmdt(shm);

    /* Valor inicial 1: actuan como mutex binarios (semaforos POSIX nombrados) */
    sem_t *sm = sem_open(SEM_MEM,   O_CREAT | O_EXCL, 0666, 1);
    sem_t *sl = sem_open(SEM_LOG,   O_CREAT | O_EXCL, 0666, 1);
    sem_t *ss = sem_open(SEM_STATE, O_CREAT | O_EXCL, 0666, 1);

    if (sm == SEM_FAILED || sl == SEM_FAILED || ss == SEM_FAILED) {
        perror("sem_open");
        shmctl(shmid, IPC_RMID, NULL);
        return 1;
    }

    /* Cierra descriptores locales; los semaforos persisten en el kernel */
    sem_close(sm);
    sem_close(sl);
    sem_close(ss);

    /* Crea la bitacora vacia lista para que los demas programas escriban en ella */
    write_log_init(algo, total_pages);

    printf("\nAmbiente inicializado:\n");
    printf("  Algoritmo   : %s\n", algo == ALGO_PAGING ? "Paginacion" : "Segmentacion");
    printf("  Paginas     : %d\n", total_pages);
    printf("  shmid       : %d\n", shmid);
    printf("  Bitacora    : %s\n", LOG_FILE);
    printf("Inicializador terminado.\n");

    return 0;
}
