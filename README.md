# Proyecto 2 - Sistemas Operativos
## Simulacion de Paginacion y Segmentacion con Semaforos

**Curso:** Sistemas Operativos - ITCR  
**Profesor:** Ing. Erika Marin Schumann  
**Entrega:** 19 de mayo de 2026

---

## Descripcion

Simulacion de asignacion de memoria a procesos usando los esquemas de **Paginacion** y **Segmentacion**. Implementado en C con 4 programas separados que se comunican mediante memoria compartida (`shmget`) y semaforos POSIX nombrados.

---

## Compilar

```bash
# Compilar todos los programas
make

# Compilar uno solo
make inicializador
make productor
make espia
make finalizador

# Limpiar binarios y archivos generados
make clean
```

---

## Orden de ejecucion

Siempre deben correrse en este orden:

```
1. inicializador   ->   crea el ambiente (shm + semaforos), luego muere
2. productor       ->   genera procesos (corre en background)
3. espia           ->   observa el estado (puede correrse en cualquier momento)
4. finalizador     ->   detiene todo y libera recursos
```

---

## Ejecucion manual paso a paso

### 1. Inicializador

Pide el algoritmo y el tamano de memoria, luego termina automaticamente.

```bash
./inicializador
```

Ejemplo de sesion:
```
Seleccione algoritmo:
  0 - Paginacion
  1 - Segmentacion
Opcion: 0
Cantidad de paginas/espacios de memoria: 20
```

Con entrada automatica (sin interaccion):
```bash
# Paginacion con 20 paginas
printf "0\n20\n" | ./inicializador

# Segmentacion con 30 paginas
printf "1\n30\n" | ./inicializador
```

---

### 2. Productor

Genera procesos (threads) a intervalos aleatorios de 30-60 segundos.  
Cada proceso busca espacio, duerme 20-60 segundos y libera.  
Corre hasta que el usuario presione `Ctrl+C` o el finalizador lo detenga.

```bash
# Correr en foreground (bloquea la terminal)
./productor

# Correr en background (recomendado para usar el espia en paralelo)
./productor &

# Guardar la salida del productor a un archivo
./productor > salida_productor.txt 2>&1 &
```

**Con tiempos reducidos para pruebas** (via variables de entorno):
```bash
# Intervalo entre procesos: 3s, duracion en memoria: 5s
PROD_INTERVAL=3 PROD_DURATION=5 ./productor &

# En background guardando salida
PROD_INTERVAL=3 PROD_DURATION=5 ./productor > salida_productor.txt 2>&1 &
```

| Variable de entorno | Efecto | Valor spec |
|---|---|---|
| `PROD_INTERVAL=N` | Crea un proceso cada N segundos | 30-60s aleatorio |
| `PROD_DURATION=N` | Cada proceso ocupa memoria N segundos | 20-60s aleatorio |

---

### 3. Espia

Puede correrse en cualquier momento mientras el productor este activo.  
Comandos disponibles dentro del espia:

| Comando | Descripcion |
|---|---|
| `memoria` | Muestra el mapa de memoria celda por celda |
| `procesos` | Muestra todos los procesos agrupados por estado |
| `salir` | Cierra el espia |

```bash
# Modo interactivo
./espia

# Con comandos automaticos (sin interaccion)
printf "memoria\nsalir\n" | ./espia
printf "procesos\nsalir\n" | ./espia
printf "memoria\nprocesos\nsalir\n" | ./espia
```

---

### 4. Finalizador

Detiene el productor, elimina la memoria compartida y los semaforos.  
Debe correrse al terminar la simulacion.

```bash
./finalizador
```

---

## Flujo completo de ejemplo

### Paginacion con 20 paginas

```bash
# Terminal 1: inicializar y arrancar productor
printf "0\n20\n" | ./inicializador
./productor &

# Terminal 2: observar estado mientras corre
./espia

# Terminal 1: cuando quieras terminar
./finalizador
```

### Segmentacion con 30 paginas

```bash
printf "1\n30\n" | ./inicializador
./productor &
./espia
./finalizador
```

### Prueba rapida con tiempos reducidos

```bash
printf "0\n15\n" | ./inicializador
PROD_INTERVAL=3 PROD_DURATION=8 ./productor &
sleep 20
printf "memoria\nprocesos\nsalir\n" | ./espia
./finalizador
```

---

## Ejecucion de pruebas automatizadas

El script `test_runner.sh` corre 4 casos de prueba automaticamente y genera un reporte.

```bash
bash test_runner.sh
```

Los parametros de prueba se configuran en la seccion superior del archivo:

```bash
# Variables principales (editar para cambiar el escenario)
INTERVALO_PRUEBA=2      # segundos entre procesos (spec: 30-60s)
DURACION_PRUEBA=5       # segundos en memoria     (spec: 20-60s)
TIEMPO_EJECUCION=20     # segundos que corre cada caso

TC_PAGINAS=(32 6 40 8)  # paginas por caso de prueba
TC_ALGO=(0 0 1 1)       # 0=paginacion, 1=segmentacion
```

Casos incluidos por defecto:

| Caso | Algoritmo | Paginas | Resultado esperado |
|---|---|---|---|
| 1 | Paginacion | 32 | Sin muertos |
| 2 | Paginacion | 6 | Con muertos (espacio escaso) |
| 3 | Segmentacion | 40 | Sin muertos |
| 4 | Segmentacion | 8 | Con muertos (espacio escaso) |

Salidas generadas por el script:

```
reporte_pruebas.txt          <- reporte completo con snapshots
/tmp/bitacora_tc1.log        <- bitacora del caso 1
/tmp/bitacora_tc2.log        <- bitacora del caso 2
/tmp/bitacora_tc3.log        <- bitacora del caso 3
/tmp/bitacora_tc4.log        <- bitacora del caso 4
```

---

## Archivos del proyecto

| Archivo | Descripcion |
|---|---|
| `common.h` | Estructuras compartidas (SharedMem, ProcEntry) y constantes |
| `inicializador.c` | Crea shmget y semaforos POSIX, inicializa memoria |
| `productor.c` | Genera threads-proceso con ciclo de vida de 9 pasos |
| `espia.c` | Lee la memoria compartida y muestra estado en tiempo real |
| `finalizador.c` | Mata el productor, elimina shm y semaforos |
| `Makefile` | Compila los 4 programas |
| `test_runner.sh` | Suite de pruebas automatizadas con reporte |
| `bitacora.log` | Generado en ejecucion: registro de todas las acciones |

---

## Parametros del spec

| Parametro | Valor |
|---|---|
| Paginas por proceso (paginacion) | 1-10 aleatorio |
| Segmentos por proceso | 1-5 aleatorio |
| Espacios por segmento | 1-3 aleatorio |
| Tiempo en memoria | 20-60 segundos |
| Intervalo entre procesos | 30-60 segundos |
| Max paginas de memoria | 256 |
| Max procesos registrados | 100 |

---

## Sincronizacion

| Semaforo | Nombre en kernel | Protege |
|---|---|---|
| `sem_mem` | `/proj2_mem` | busqueda y asignacion en memoria (region critica) |
| `sem_log` | `/proj2_log` | escrituras en `bitacora.log` |
| `sem_state` | `/proj2_state` | tabla de procesos en shm |

---

## Limpiar recursos manualmente

Si el sistema quedo en estado inconsistente (crash, Ctrl+C inesperado):

```bash
# Opcion 1: usar el finalizador
./finalizador

# Opcion 2: limpiar manualmente
ipcs -m                          # ver segmentos de shm activos
ipcrm -m <shmid>                 # eliminar por id
sem_unlink /proj2_mem            # los sem se eliminan con finalizador
```