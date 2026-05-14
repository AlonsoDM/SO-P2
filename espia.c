#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <semaphore.h>
#include <fcntl.h>
#include <time.h>
#include "common.h"

/* Convierte el codigo de estado a texto legible para la consola */
static const char *state_str(int s) {
    switch (s) {
        case STATE_NONE:      return "NUEVO";
        case STATE_BLOCKED:   return "BLOQUEADO";
        case STATE_SEARCHING: return "BUSCANDO";
        case STATE_RUNNING:   return "EN_MEMORIA";
        case STATE_DEAD:      return "MUERTO";
        case STATE_DONE:      return "TERMINADO";
        default:              return "DESCONOCIDO";
    }
}

/* Muestra celda por celda quien ocupa cada posicion de memoria */
static void show_memory(SharedMem *shm) {
    /* Adquiere sem_state para que la lectura sea consistente con el productor */
    sem_t *ss = sem_open(SEM_STATE, 0);
    sem_wait(ss);

    printf("\n=== ESTADO DE MEMORIA (%s) ===\n",
           shm->algorithm == ALGO_PAGING ? "Paginacion" : "Segmentacion");
    printf("Total de paginas/espacios: %d\n\n", shm->total_pages);

    int cols = 8;
    for (int i = 0; i < shm->total_pages; i++) {
        /* Imprime el rango al inicio de cada fila */
        if (i % cols == 0) printf("[%3d-%3d] ", i, i + cols - 1 < shm->total_pages ? i + cols - 1 : shm->total_pages - 1);

        if (shm->memory[i] == -1) {
            printf("[ libre ] ");
        } else if (shm->algorithm == ALGO_SEGMENT) {
            /* Para segmentacion muestra PID y numero de segmento */
            printf("[P%d:S%d  ] ", shm->memory[i], shm->mem_seg[i]);
        } else {
            printf("[P%-5d] ", shm->memory[i]);
        }

        if ((i + 1) % cols == 0 || i == shm->total_pages - 1) printf("\n");
    }

    /* Cuenta celdas libres para dar un resumen rapido de disponibilidad */
    int libre = 0;
    for (int i = 0; i < shm->total_pages; i++)
        if (shm->memory[i] == -1) libre++;
    printf("\nEspacios libres: %d / %d\n", libre, shm->total_pages);

    sem_post(ss);
    sem_close(ss);
}

/* Lista los procesos agrupados por estado para facilitar la lectura del usuario */
static void show_processes(SharedMem *shm) {
    sem_t *ss = sem_open(SEM_STATE, 0);
    sem_wait(ss);

    printf("\n=== ESTADO DE PROCESOS ===\n");

    printf("\n--- En memoria (durmiendo) ---\n");
    int any = 0;
    for (int i = 0; i < shm->num_procs; i++) {
        ProcEntry *p = &shm->procs[i];
        if (p->state == STATE_RUNNING) {
            printf("  PID:%d | Duracion:%ds", p->sim_pid, p->duration);
            if (p->assign_time) {
                /* Calcula cuanto lleva en memoria desde que fue asignado */
                time_t elapsed = time(NULL) - p->assign_time;
                printf(" | En memoria: %lds", elapsed);
            }
            printf("\n");
            any = 1;
        }
    }
    if (!any) printf("  (ninguno)\n");

    /* Solo puede haber un proceso en este estado por la exclusion mutua del semaforo */
    printf("\n--- Buscando espacio ---\n");
    any = 0;
    for (int i = 0; i < shm->num_procs; i++) {
        if (shm->procs[i].state == STATE_SEARCHING) {
            printf("  PID:%d\n", shm->procs[i].sim_pid);
            any = 1;
        }
    }
    if (!any) printf("  (ninguno)\n");

    /* Bloqueados: esperan adquirir sem_mem antes de entrar o antes de liberar */
    printf("\n--- Bloqueados (esperando region critica) ---\n");
    any = 0;
    for (int i = 0; i < shm->num_procs; i++) {
        if (shm->procs[i].state == STATE_BLOCKED) {
            printf("  PID:%d\n", shm->procs[i].sim_pid);
            any = 1;
        }
    }
    if (!any) printf("  (ninguno)\n");

    printf("\n--- Muertos (sin espacio) ---\n");
    any = 0;
    for (int i = 0; i < shm->num_procs; i++) {
        if (shm->procs[i].state == STATE_DEAD) {
            printf("  PID:%d\n", shm->procs[i].sim_pid);
            any = 1;
        }
    }
    if (!any) printf("  (ninguno)\n");

    printf("\n--- Terminados exitosamente ---\n");
    any = 0;
    for (int i = 0; i < shm->num_procs; i++) {
        if (shm->procs[i].state == STATE_DONE) {
            printf("  PID:%d\n", shm->procs[i].sim_pid);
            any = 1;
        }
    }
    if (!any) printf("  (ninguno)\n");

    /* Tabla resumen con todos los procesos registrados desde el inicio */
    printf("\n--- Tabla completa (%d procesos) ---\n", shm->num_procs);
    printf("%-8s %-12s %-10s %-8s\n", "PID", "Estado", "Espacios", "Dur(s)");
    printf("%-8s %-12s %-10s %-8s\n", "---", "------", "-------", "------");
    for (int i = 0; i < shm->num_procs; i++) {
        ProcEntry *p = &shm->procs[i];
        printf("%-8d %-12s %-10d %-8d\n",
               p->sim_pid, state_str(p->state),
               p->num_assigned, p->duration);
    }

    sem_post(ss);
    sem_close(ss);
}

int main(void) {
    /* SHM_RDONLY: el espia no modifica la memoria, solo la observa */
    int shmid = shmget(SHM_KEY, sizeof(SharedMem), 0666);
    if (shmid == -1) {
        fprintf(stderr, "Error: no hay memoria compartida. Ejecute inicializador primero.\n");
        return 1;
    }

    SharedMem *shm = (SharedMem *)shmat(shmid, NULL, SHM_RDONLY);
    if (shm == (void *)-1) { perror("shmat"); return 1; }
    if (!shm->initialized) {
        fprintf(stderr, "Memoria no inicializada.\n");
        return 1;
    }

    printf("=== Espia del Sistema ===\n");
    printf("Algoritmo: %s | Memoria: %d paginas\n\n",
           shm->algorithm == ALGO_PAGING ? "Paginacion" : "Segmentacion",
           shm->total_pages);
    printf("Comandos: memoria | procesos | salir\n");

    char line[64];
    while (1) {
        printf("> ");
        fflush(stdout);
        if (!fgets(line, sizeof(line), stdin)) break;
        line[strcspn(line, "\n")] = '\0';

        if (strcmp(line, "memoria") == 0) {
            show_memory(shm);
        } else if (strcmp(line, "procesos") == 0) {
            show_processes(shm);
        } else if (strcmp(line, "salir") == 0) {
            break;
        } else if (strlen(line) > 0) {
            printf("Comandos: memoria | procesos | salir\n");
        }
    }

    shmdt(shm);
    return 0;
}
