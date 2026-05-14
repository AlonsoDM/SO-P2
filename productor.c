#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <semaphore.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <signal.h>
#include <time.h>
#include "common.h"

/* Recursos globales compartidos por el hilo principal y todos los threads de proceso */
static SharedMem *shm       = NULL;
static sem_t     *sem_mem   = NULL;
static sem_t     *sem_log   = NULL;
static sem_t     *sem_state = NULL;

/* volatile: el compilador no optimiza esta variable; la lee en cada iteracion */
static volatile int keep_running = 1;

/* Contador de PID simulados; se incrementa con mutex para evitar duplicados */
static int sim_pid_counter = 1;
static pthread_mutex_t counter_mutex = PTHREAD_MUTEX_INITIALIZER;

/* Argumentos que el hilo principal pasa a cada thread de proceso */
typedef struct {
    int sim_pid;
    int proc_idx;
    int algo;
    int pages_needed;
    int num_segs;
    int spaces_per_seg;
    int duration;
} ThreadData;

/* Genera un PID simulado unico de forma segura entre multiples threads */
static int next_sim_pid(void) {
    pthread_mutex_lock(&counter_mutex);
    int p = sim_pid_counter++;
    pthread_mutex_unlock(&counter_mutex);
    return p;
}

/* Escribe un evento de asignacion o desasignacion en la bitacora (region critica) */
static void log_event(int sim_pid, const char *accion, const char *tipo,
                      int *espacios, int n) {
    sem_wait(sem_log);
    FILE *f = fopen(LOG_FILE, "a");
    if (f) {
        time_t now = time(NULL);
        char ts[64];
        strftime(ts, sizeof(ts), "%Y-%m-%d %H:%M:%S", localtime(&now));
        fprintf(f, "[%s] PID:%d | Accion:%s | Tipo:%s | Espacios:", ts, sim_pid, accion, tipo);
        for (int i = 0; i < n; i++) fprintf(f, " %d", espacios[i]);
        fprintf(f, "\n");
        fclose(f);
    }
    sem_post(sem_log);
}

/* Registra en la bitacora que el proceso murio por falta de espacio */
static void log_no_space(int sim_pid) {
    sem_wait(sem_log);
    FILE *f = fopen(LOG_FILE, "a");
    if (f) {
        time_t now = time(NULL);
        char ts[64];
        strftime(ts, sizeof(ts), "%Y-%m-%d %H:%M:%S", localtime(&now));
        fprintf(f, "[%s] PID:%d | Accion:MURIO | Tipo:sin_espacio\n", ts, sim_pid);
        fclose(f);
    }
    sem_post(sem_log);
}

/* Busca n paginas libres no contiguas; retorna 1 si las encontro todas */
static int find_pages(int n, int *result) {
    int found = 0;
    for (int i = 0; i < shm->total_pages && found < n; i++) {
        if (shm->memory[i] == -1) result[found++] = i;
    }
    return found == n;
}

/* Busca un bloque contiguo por cada segmento usando una copia temporal del mapa */
static int find_segments(int num_segs, int spaces_per_seg,
                         int *result, int *seg_ids) {
    int temp[MAX_PAGES];
    /* Copia temporal para simular asignaciones sin modificar la memoria real */
    memcpy(temp, shm->memory, sizeof(int) * shm->total_pages);

    int total = 0;
    for (int s = 0; s < num_segs; s++) {
        int count = 0, start = -1;
        for (int i = 0; i < shm->total_pages; i++) {
            if (temp[i] == -1) {
                count++;
                if (count == spaces_per_seg) {
                    start = i - spaces_per_seg + 1;
                    break;
                }
            } else {
                count = 0;
            }
        }
        /* Si no existe bloque contiguo para este segmento, falla completo */
        if (start == -1) return 0;
        for (int i = start; i < start + spaces_per_seg; i++) {
            result[total] = i;
            seg_ids[total] = s;
            total++;
            temp[i] = 1;  /* marca como ocupado en la copia para el siguiente segmento */
        }
    }
    return 1;
}

/* Implementa los 9 pasos del ciclo de vida: buscar, asignar, ejecutar y liberar */
static void *process_thread(void *arg) {
    ThreadData *td = (ThreadData *)arg;
    int spid = td->sim_pid;
    int idx  = td->proc_idx;

    /* Paso 1a: actualiza estado antes de bloquearse para que el espia lo vea */
    sem_wait(sem_state);
    shm->procs[idx].state = STATE_BLOCKED;
    sem_post(sem_state);

    printf("[PROC] PID:%d esperando semaforo de memoria...\n", spid);
    fflush(stdout);

    /* Paso 1b: pide el semaforo; solo uno a la vez puede buscar espacio */
    sem_wait(sem_mem);

    /* Paso 2: ya tiene el semaforo, cambia a SEARCHING para el espia */
    sem_wait(sem_state);
    shm->procs[idx].state = STATE_SEARCHING;
    sem_post(sem_state);

    printf("[PROC] PID:%d buscando espacio en memoria...\n", spid);
    fflush(stdout);

    int assigned[MAX_ASSIGNED];
    int seg_ids[MAX_ASSIGNED];
    int num_assigned = 0;
    int found = 0;

    if (td->algo == ALGO_PAGING) {
        found = find_pages(td->pages_needed, assigned);
        num_assigned = td->pages_needed;
    } else {
        found = find_segments(td->num_segs, td->spaces_per_seg, assigned, seg_ids);
        num_assigned = td->num_segs * td->spaces_per_seg;
    }

    if (!found) {
        /* Sin espacio: el proceso muere y registra el hecho */
        sem_wait(sem_state);
        shm->procs[idx].state = STATE_DEAD;
        sem_post(sem_state);

        /* Paso 3: escribe en bitacora antes de liberar el semaforo */
        log_no_space(spid);

        /* Paso 4: devuelve el semaforo aunque no haya podido asignar */
        sem_post(sem_mem);

        printf("[PROC] PID:%d murio - sin espacio suficiente.\n", spid);
        fflush(stdout);
        free(td);
        return NULL;
    }

    /* Escribe el sim_pid del dueno en cada celda de memoria compartida */
    for (int i = 0; i < num_assigned; i++) {
        shm->memory[assigned[i]] = spid;
        shm->mem_seg[assigned[i]] = (td->algo == ALGO_SEGMENT) ? seg_ids[i] : 0;
    }

    /* Actualiza PCB con las celdas asignadas y cambia estado a RUNNING */
    sem_wait(sem_state);
    shm->procs[idx].num_assigned = num_assigned;
    for (int i = 0; i < num_assigned; i++) {
        shm->procs[idx].assigned[i] = assigned[i];
        shm->procs[idx].seg_ids[i]  = (td->algo == ALGO_SEGMENT) ? seg_ids[i] : 0;
    }
    shm->procs[idx].state       = STATE_RUNNING;
    shm->procs[idx].assign_time = time(NULL);
    sem_post(sem_state);

    /* Paso 3: registra la asignacion en la bitacora */
    log_event(spid, "ENTRA", "asignacion", assigned, num_assigned);

    /* Paso 4: libera el semaforo; otros procesos ya pueden buscar espacio */
    sem_post(sem_mem);

    printf("[PROC] PID:%d en memoria. Espacios:", spid);
    for (int i = 0; i < num_assigned; i++) printf(" %d", assigned[i]);
    printf(". Sleep %ds.\n", td->duration);
    fflush(stdout);

    /* Paso 5: simula la ejecucion del proceso en memoria */
    sleep(td->duration);

    /* Paso 6: antes de liberar, vuelve a marcar BLOCKED para visibilidad */
    sem_wait(sem_state);
    shm->procs[idx].state = STATE_BLOCKED;
    sem_post(sem_state);

    /* Paso 6: pide el semaforo para entrar a la region critica de liberacion */
    sem_wait(sem_mem);

    /* Paso 7: marca las celdas como libres en la memoria compartida */
    for (int i = 0; i < num_assigned; i++) {
        shm->memory[assigned[i]]  = -1;
        shm->mem_seg[assigned[i]] = -1;
    }

    sem_wait(sem_state);
    shm->procs[idx].state        = STATE_DONE;
    shm->procs[idx].release_time = time(NULL);
    sem_post(sem_state);

    /* Paso 8: registra la desasignacion en la bitacora */
    log_event(spid, "SALE", "desasignacion", assigned, num_assigned);

    /* Paso 9: devuelve el semaforo al finalizar la region critica */
    sem_post(sem_mem);

    printf("[PROC] PID:%d termino su ejecucion.\n", spid);
    fflush(stdout);

    free(td);
    return NULL;
}

/* Captura SIGINT y SIGTERM para terminar el loop principal de forma limpia */
static void handle_signal(int sig) {
    (void)sig;
    keep_running = 0;
}

int main(void) {
    srand((unsigned)time(NULL));
    signal(SIGTERM, handle_signal);
    signal(SIGINT,  handle_signal);

    /* Busca el segmento existente sin crearlo (el inicializador ya lo creo) */
    int shmid = shmget(SHM_KEY, sizeof(SharedMem), 0666);
    if (shmid == -1) {
        fprintf(stderr, "Error: ejecute inicializador primero.\n");
        return 1;
    }
    shm = (SharedMem *)shmat(shmid, NULL, 0);
    if (shm == (void *)-1) { perror("shmat"); return 1; }
    if (!shm->initialized) {
        fprintf(stderr, "Memoria no inicializada.\n");
        return 1;
    }

    /* Abre semaforos ya existentes sin crearlos (O_CREAT omitido) */
    sem_mem   = sem_open(SEM_MEM,   0);
    sem_log   = sem_open(SEM_LOG,   0);
    sem_state = sem_open(SEM_STATE, 0);
    if (sem_mem == SEM_FAILED || sem_log == SEM_FAILED || sem_state == SEM_FAILED) {
        perror("sem_open");
        return 1;
    }

    /* Guarda el PID real para que el finalizador pueda enviarnos una senal */
    shm->producer_pid = getpid();

    /* Archivo auxiliar: mas confiable que shm si el proceso muere inesperadamente */
    FILE *pf = fopen(PID_FILE, "w");
    if (pf) { fprintf(pf, "%d\n", getpid()); fclose(pf); }

    int algo = shm->algorithm;
    printf("=== Productor de Procesos ===\n");
    printf("Algoritmo: %s\n", algo == ALGO_PAGING ? "Paginacion" : "Segmentacion");
    printf("Memoria total: %d paginas\n", shm->total_pages);
    printf("Creando procesos... (Ctrl+C para detener)\n\n");
    fflush(stdout);

    while (keep_running) {
        /* PROD_INTERVAL sobreescribe el intervalo spec para pruebas automatizadas */
        const char *env_int = getenv("PROD_INTERVAL");
        int interval = env_int ? atoi(env_int) : (30 + rand() % 31);

        /* Espera en pasos de 1s para no ignorar senales durante el sleep largo */
        for (int i = 0; i < interval && keep_running; i++) sleep(1);
        if (!keep_running) break;

        /* Protege num_procs con semaforo para evitar condicion de carrera */
        sem_wait(sem_state);
        if (shm->num_procs >= MAX_PROCS) {
            sem_post(sem_state);
            printf("[PROD] Tabla de procesos llena, esperando...\n");
            fflush(stdout);
            continue;
        }
        int idx   = shm->num_procs++;
        int spid  = next_sim_pid();
        int pages_needed = 0, num_segs = 0, spaces_per_seg = 0;

        /* PROD_DURATION sobreescribe la duracion spec para pruebas automatizadas */
        const char *env_dur = getenv("PROD_DURATION");
        int duration = env_dur ? atoi(env_dur) : (20 + rand() % 41);

        if (algo == ALGO_PAGING) {
            /* Paginacion: entre 1 y 10 paginas por proceso (spec) */
            pages_needed = 1 + rand() % 10;
        } else {
            /* Segmentacion: 1-5 segmentos, 1-3 espacios por segmento (spec) */
            num_segs      = 1 + rand() % MAX_SEG;
            spaces_per_seg = 1 + rand() % MAX_SEG_SPC;
        }

        /* Inicializa el PCB en la tabla compartida antes de lanzar el thread */
        ProcEntry *pe = &shm->procs[idx];
        memset(pe, 0, sizeof(ProcEntry));
        pe->sim_pid        = spid;
        pe->state          = STATE_NONE;
        pe->algo           = algo;
        pe->pages_needed   = pages_needed;
        pe->num_segs       = num_segs;
        pe->spaces_per_seg = spaces_per_seg;
        pe->duration       = duration;
        sem_post(sem_state);

        /* Copia los parametros al heap para que el thread los tenga de forma independiente */
        ThreadData *td = malloc(sizeof(ThreadData));
        td->sim_pid        = spid;
        td->proc_idx       = idx;
        td->algo           = algo;
        td->pages_needed   = pages_needed;
        td->num_segs       = num_segs;
        td->spaces_per_seg = spaces_per_seg;
        td->duration       = duration;

        pthread_t t;
        if (pthread_create(&t, NULL, process_thread, td) != 0) {
            perror("pthread_create");
            free(td);
            continue;
        }
        /* Detach: el thread libera sus recursos al terminar sin necesidad de join */
        pthread_detach(t);

        if (algo == ALGO_PAGING) {
            printf("[PROD] PID:%d creado | Paginas:%d | Duracion:%ds\n",
                   spid, pages_needed, duration);
        } else {
            printf("[PROD] PID:%d creado | Segmentos:%d x %d espacios | Duracion:%ds\n",
                   spid, num_segs, spaces_per_seg, duration);
        }
        fflush(stdout);
    }

    printf("\n[PROD] Productor detenido.\n");
    fflush(stdout);

    sem_close(sem_mem);
    sem_close(sem_log);
    sem_close(sem_state);
    shmdt(shm);
    remove(PID_FILE);
    return 0;
}
