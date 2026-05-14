#ifndef COMMON_H
#define COMMON_H

#include <sys/types.h>
#include <time.h>

/* Llave unica para identificar el segmento de memoria compartida entre programas */
#define SHM_KEY       0x5054

/* Nombres de semaforos POSIX: deben comenzar con '/' para ser inter-proceso */
#define SEM_MEM       "/proj2_mem"    /* controla acceso exclusivo a la memoria */
#define SEM_LOG       "/proj2_log"    /* serializa escrituras en la bitacora    */
#define SEM_STATE     "/proj2_state"  /* protege la tabla de procesos           */

#define LOG_FILE      "bitacora.log"
#define PID_FILE      "/tmp/proj2_producer.pid"  /* PID real del productor para el finalizador */

/* Identificadores de algoritmo de asignacion de memoria */
#define ALGO_PAGING   0
#define ALGO_SEGMENT  1

/* Estados del ciclo de vida de un proceso simulado */
#define STATE_NONE      0  /* recien registrado, aun no activo        */
#define STATE_BLOCKED   1  /* esperando adquirir el semaforo          */
#define STATE_SEARCHING 2  /* dentro de la region critica, buscando   */
#define STATE_RUNNING   3  /* en memoria, ejecutando su sleep         */
#define STATE_DEAD      4  /* termino sin encontrar espacio           */
#define STATE_DONE      5  /* completo exitosamente y libero memoria  */

#define MAX_PAGES     256  /* capacidad maxima del arreglo de memoria */
#define MAX_PROCS     100  /* maxima cantidad de procesos registrados */
#define MAX_SEG       5    /* segmentos maximos por proceso           */
#define MAX_SEG_SPC   3    /* espacios contiguos maximos por segmento */
#define MAX_ASSIGNED  (MAX_SEG * MAX_SEG_SPC)

/* PCB simulado: guarda todo lo necesario para identificar y rastrear un proceso */
typedef struct {
    int    sim_pid;                  /* identificador simulado del proceso      */
    int    state;                    /* estado actual segun los defines arriba  */
    int    algo;                     /* algoritmo con que fue creado            */
    int    pages_needed;             /* paginas requeridas (paginacion)         */
    int    num_segs;                 /* cantidad de segmentos (segmentacion)    */
    int    spaces_per_seg;           /* espacios por segmento (segmentacion)    */
    int    assigned[MAX_ASSIGNED];   /* indices de celdas asignadas en memoria  */
    int    seg_ids[MAX_ASSIGNED];    /* a que segmento pertenece cada celda     */
    int    num_assigned;             /* total de celdas asignadas               */
    int    duration;                 /* tiempo de sleep en segundos             */
    time_t assign_time;              /* momento en que entro a memoria          */
    time_t release_time;             /* momento en que libero memoria           */
} ProcEntry;

/* Region de memoria compartida completa: memoria simulada + tabla de procesos */
typedef struct {
    int      initialized;            /* bandera: el inicializador ya corrio     */
    int      algorithm;              /* ALGO_PAGING o ALGO_SEGMENT              */
    int      total_pages;            /* tamano configurado por el usuario       */
    pid_t    producer_pid;           /* PID real del productor (para finalizador) */
    int      memory[MAX_PAGES];      /* -1 = libre, sim_pid = dueno            */
    int      mem_seg[MAX_PAGES];     /* id de segmento de cada celda           */
    int      num_procs;              /* procesos registrados hasta el momento   */
    ProcEntry procs[MAX_PROCS];      /* tabla de PCBs                           */
} SharedMem;

#endif
