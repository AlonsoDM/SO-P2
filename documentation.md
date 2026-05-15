# Documentación del Proyecto 2 — Simulación Paginación y Segmentación

## Portada

- **Título:** Simulación Paginación y Segmentación
- **Curso:** Sistemas Operativos
- **Proyecto:** Proyecto 2
- **Autor:** Alonso Durán Muñoz - 2023044780, Gabriel Solano Coronado - 2019033687, Jesús Valverde Ureña - 2022437462
- **Fecha:** 19 Mayo 2026

## Índice

1. [Introducción y Problema](#1-introducción-y-problema)
2. [Estrategia de solución](#2-estrategia-de-solución)
3. [Investigación: mmap vs shmget](#3-investigación-mmap-vs-shmget)
4. [Sincronización y Semáforos](#4-sincronización-y-semáforos)
5. [Análisis de resultados](#5-análisis-de-resultados)
6. [Casos de prueba](#6-casos-de-prueba)
7. [Resumen de experiencias](#7-resumen-de-experiencias)
8. [Compilación y Ejecución](#8-compilación-y-ejecución)
9. [Bibliografía](#9-bibliografía)

## 1. Introducción y Problema

Este proyecto resuelve un problema de sincronización de procesos simulando la asignación de memoria mediante los esquemas de **paginación** y **segmentación**. El problema consiste en cuatro programas independientes (`inicializador`, `productor`, `espía`, y `finalizador`) que deben compartir un estado global utilizando memoria compartida. Se simulan procesos que solicitan espacio, se bloquean si no lo hay, utilizan la memoria por un tiempo, y luego liberan los recursos, registrando de manera concurrente los eventos en una bitácora compartida.

## 2. Estrategia de solución

El sistema se implementó en C para Linux, dividiendo la funcionalidad en 4 componentes principales que se comunican mediante memoria compartida (SysV IPC) y se sincronizan con Semáforos POSIX nombrados.

- **`common.h`**: Define la llave de memoria compartida (`0x5054`), los nombres de los semáforos, y la estructura principal `SharedMem` que representa la memoria (arreglo de enteros) y la tabla de control de procesos (PCBs).
- **`inicializador.c`**: Solicita la memoria al sistema operativo, inicializa las estructuras y crea los semáforos requeridos.
- **`productor.c`**: Proceso principal que genera hilos (`pthreads`) simulando procesos que intentan acceder a la memoria bajo algoritmos de Paginación o Segmentación.
- **`espia.c`**: Proceso observador de solo lectura (`SHM_RDONLY`) que lee la memoria compartida y reporta su estado actual.
- **`finalizador.c`**: Detiene ordenadamente al productor, limpia la memoria compartida (`IPC_RMID`) y elimina los semáforos vinculados (`sem_unlink`).

## 3. Investigación: mmap vs shmget

Ambas son llamadas al sistema en Linux utilizadas para memoria compartida, pero tienen enfoques y casos de uso diferentes:

- **`shmget` (System V IPC):** Solicita al kernel un bloque de memoria compartida identificado de manera global por una llave (key numérica). Es ideal para compartir memoria entre procesos totalmente independientes y sin relación de parentesco. Su ciclo de vida persiste en el kernel hasta que es eliminado explícitamente usando `shmctl`.
- **`mmap` (POSIX):** Su diseño original es para mapear archivos o dispositivos físicos en el espacio de memoria virtual de un proceso. Aunque puede usarse para memoria compartida anónima (con `MAP_ANONYMOUS | MAP_SHARED`), esto suele estar restringido a procesos emparentados (padre-hijo) luego de un `fork()`, a menos que se mapee un archivo real en disco.

**Justificación:** Se utilizó `shmget` dado que nuestros 4 programas (`inicializador`, `productor`, etc.) son ejecutables compilados de manera separada, ejecutados en momentos distintos y sin ninguna jerarquía padre-hijo. La provisión de una llave global (`SHM_KEY`) en System V es la solución natural para este escenario.

## 4. Sincronización y Semáforos

**Tipo de Semáforos Utilizados:** Se implementaron **Semáforos POSIX Nombrados** mediante la función `sem_open`.
**¿Por qué?:** A diferencia de los semáforos POSIX anónimos (limitados a hilos de un mismo proceso o forzados a vivir dentro del bloque de memoria compartida) y los semáforos de System V (que tienen una API más engorrosa), los semáforos nombrados se identifican con una cadena de texto (ej. `"/proj2_mem"`) a nivel del sistema. Esto permite que programas independientes puedan acceder a un mismo semáforo de manera directa y concurrente.

**Logro de la sincronización:**
Se implementaron tres semáforos inicializados en valor 1 para que actúen como exclusión mutua (mutex binarios):

- **`SEM_MEM`**: Protege la región crítica de asignación. Asegura que cuando un hilo del Productor busca espacio libre y marca la memoria, ningún otro proceso interfiera, evitando colisiones de memoria.
- **`SEM_LOG`**: Serializa las escrituras en el archivo `bitacora.log`, previniendo la corrupción de los textos cuando varios hilos intentan registrar eventos al mismo tiempo.
- **`SEM_STATE`**: Protege la tabla de PCBs, asegurando lecturas consistentes por parte del proceso Espía mientras el Productor actualiza los estados.

## 5. Análisis de resultados

**Qué sirve:** - La sincronización multi-proceso e inter-hilos es estable. Las pruebas con alta concurrencia de hilos generados por el productor no corrompieron la estructura `SharedMem`.

- El algoritmo de **Paginación** logró mapear eficientemente fragmentos dispersos en memoria, lo que demostró una mayor tolerancia a la fragmentación.
- El módulo **Finalizador** cerró el sistema liberando correctamente los segmentos en el kernel, evadiendo fugas de memoria, confirmado mediante el comando `ipcs`.

**Aspectos Relevantes (Qué se podría mejorar):**
El diseño con `pthreads` e IPC requiere mucho cuidado en la sección crítica. Observamos que si un proceso falla a la mitad de su ejecución mientras sostiene `sem_wait(SEM_MEM)`, provocaría un interbloqueo (*deadlock*) en todo el sistema. Además, en el esquema de segmentación, los procesos grandes experimentaron "inanición" al no hallar bloques contiguos frecuentemente.

## 6. Casos de prueba

Para la validación del sistema se desarrolló un script automatizado (`test_runner.sh`) que evalúa el comportamiento del productor bajo distintas condiciones de estrés, reduciendo los tiempos de sleep e intervalos para agilizar la prueba (20 segundos de ejecución por caso).

* **Caso 1: Paginación con Espacio Suficiente (`Paginacion_EspacioSuficiente`)**

    * **Qué se quería probar:** El correcto mapeo de páginas no contiguas cuando hay memoria de sobra.
    * **Cómo se hizo:** Se ejecutó el algoritmo de Paginación asignando un total de páginas holgado frente a las peticiones del Productor.
    * **Resultado:** Todos los procesos lograron entrar a la memoria, se registraron correctamente en la bitácora compartida y finalizaron sin entrar a estado `MUERTO`.

* **Caso 2: Paginación con Espacio Escaso (`Paginacion_EspacioEscaso`)**

    * **Qué se quería probar:** La capacidad del algoritmo para aprovechar fragmentos dispersos y su comportamiento cuando la memoria se agota.
    * **Cómo se hizo:** Se corrió el sistema con Paginación, pero limitando drásticamente el total de páginas.
    * **Resultado:** Se comprobó que los procesos aprovechan espacios separados. Sin embargo, por la escasez, algunos procesos entraron en inanición o fueron rechazados correctamente, validando la protección de la sección crítica.

* **Caso 3: Segmentación con Espacio Suficiente (`Segmentacion_EspacioSuficiente`)**
    * **Qué se quería probar:** La asignación estricta de bloques contiguos en condiciones óptimas.
    * **Cómo se hizo:** Se configuró el algoritmo de Segmentación con una memoria lo suficientemente amplia.
    * **Resultado:** Múltiples segmentos fueron asignados y liberados de forma exitosa usando la memoria compartida sin generar colisiones entre procesos.

* **Caso 4: Segmentación con Espacio Escaso (`Segmentacion_EspacioEscaso`)**
    * **Qué se quería probar:** El fallo de asignación por fragmentación externa (cuando hay espacio libre total, pero no contiguo).
    * **Cómo se hizo:** Segmentación con un límite de memoria muy estricto.
    * **Resultado:** Varios procesos entraron a estado `MUERTO` y lo reportaron en el `.log` al no encontrar bloques contiguos del tamaño necesario, comprobando así la desventaja teórica de la segmentación frente a la paginación.

## 7. Resumen de experiencias

El desarrollo del proyecto fue retador, particularmente en la coordinación de la llamada IPC junto al multihilo (`pthreads`). Aprendimos de forma aplicada la diferencia arquitectónica entre la Paginación, que fragmenta la información pero mitiga los problemas de espacio, frente a la Segmentación, que es más estricta en su acomodo. A nivel de SO, dominar los *System Calls* de memoria compartida en Linux deja una comprensión invaluable sobre el manejo a bajo nivel de la memoria.

## 8. Compilación y Ejecución

**Cómo compilar:**
Se deben vincular las librerías `pthread` (para la gestión de hilos) y `rt` (para acceder a semáforos POSIX).

```bash

gcc -o inicializador inicializador.c common.h -lpthread -lrt
gcc -o productor productor.c common.h -lpthread -lrt
gcc -o espia espia.c common.h -lpthread -lrt
gcc -o finalizador finalizador.c common.h -lpthread -lrt

```

**Cómo ejecutar:**
Cada programa debe correrse de forma secuencial y/o en terminales distintas.

1. ./inicializador (Pide parámetros iniciales, reserva la memoria y configura semáforos)

2. ./productor (Comienza a simular procesos)

3. ./espia (Ejecutar en otra pestaña para monitoreo y snapshot de memoria)

4. ./finalizador (Ejecutar al final para terminar y limpiar IPC)

**Ejecución Automatizada (Testing):**
El proyecto incluye un script en Bash para correr los escenarios de prueba de forma automatizada y desatendida. Para utilizarlo, se deben dar permisos de ejecución y correr el archivo:

```bash
chmod +x test_runner.sh
./test_runner.sh
```

## 9. Bibliografía

Silberschatz, A., Galvin, P. B., & Gagne, G. (2018). Operating System Concepts (10th ed.). Wiley.

Kerrisk, M. (2010). The Linux Programming Interface. No Starch Press.

Linux Man Pages: man shmget, man sem_overview, man pthread_create.
