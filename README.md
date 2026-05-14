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

---

### 2. Productor

Genera procesos (threads) a intervalos aleatorios de 30-60 segundos.  
Cada proceso busca espacio, duerme 20-60 segundos y libera.  
Corre hasta que el usuario presione `Ctrl+C` o el finalizador lo detenga.

```bash
# Correr en foreground (bloquea la terminal)
./productor
```

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
