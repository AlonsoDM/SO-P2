#!/bin/bash
# =============================================================================
# CONFIGURACION DE PRUEBAS
# Todos los parametros se definen aqui. Modificar esta seccion para
# cambiar los datos de prueba sin tocar el resto del script.
# =============================================================================

# --- Tiempos de prueba (en segundos) ---
# Estos sobreescriben los tiempos del spec (30-60s intervalo, 20-60s duracion)
# via variables de entorno PROD_INTERVAL y PROD_DURATION
INTERVALO_PRUEBA=2      # tiempo entre creacion de procesos
DURACION_PRUEBA=5       # tiempo que cada proceso ocupa la memoria

# --- Tiempo que corre el productor en cada caso de prueba ---
TIEMPO_EJECUCION=20     # segundos totales que se deja correr el productor

# --- Definicion de casos de prueba ---
# Formato: TC_NOMBRE TC_ALGORITMO TC_PAGINAS TC_DESCRIPCION TC_ESPERADO_MUERTOS
# ALGORITMO: 0=Paginacion 1=Segmentacion
# TC_ESPERADO_MUERTOS: "si" si se esperan procesos muertos, "no" si no

declare -a TC_NOMBRE=("Paginacion_EspacioSuficiente"
                      "Paginacion_EspacioEscaso"
                      "Segmentacion_EspacioSuficiente"
                      "Segmentacion_EspacioEscaso")

declare -a TC_ALGO=(0 0 1 1)

declare -a TC_PAGINAS=(32 6 40 8)

declare -a TC_DESC=(
    "Paginacion con 32 paginas: todos los procesos deben poder asignarse"
    "Paginacion con solo 6 paginas: procesos deben morir por falta de espacio"
    "Segmentacion con 40 paginas: espacio suficiente para todos los segmentos"
    "Segmentacion con solo 8 paginas: segmentos no caben, procesos mueren"
)

declare -a TC_ESPERA_MUERTOS=("no" "si" "no" "si")

# --- Archivo de reporte ---
REPORTE="reporte_pruebas.txt"

# =============================================================================
# FUNCIONES AUXILIARES (no modificar)
# =============================================================================

PASS=0
FAIL=0

log() { echo "$1" | tee -a "$REPORTE"; }

separador() { log ""; log "$(printf '=%.0s' {1..70})"; }

separador_menor() { log "$(printf -- '-%.0s' {1..70})"; }

limpiar_ambiente() {
    ./finalizador > /dev/null 2>&1
    sleep 1
}

ejecutar_caso() {
    local idx=$1
    local nombre="${TC_NOMBRE[$idx]}"
    local algo="${TC_ALGO[$idx]}"
    local paginas="${TC_PAGINAS[$idx]}"
    local desc="${TC_DESC[$idx]}"
    local espera_muertos="${TC_ESPERA_MUERTOS[$idx]}"
    local algo_str="Paginacion"
    [ "$algo" -eq 1 ] && algo_str="Segmentacion"

    separador
    log ""
    log "CASO $((idx+1)): $nombre"
    log "  Descripcion : $desc"
    log "  Algoritmo   : $algo_str"
    log "  Paginas     : $paginas"
    log "  Intervalo   : ${INTERVALO_PRUEBA}s entre procesos (spec: 30-60s)"
    log "  Duracion    : ${DURACION_PRUEBA}s en memoria   (spec: 20-60s)"
    log "  Tiempo total: ${TIEMPO_EJECUCION}s de ejecucion"
    separador_menor

    # Inicializar ambiente
    SALIDA_INIT=$(printf "%d\n%d\n" "$algo" "$paginas" | ./inicializador 2>&1)
    if [ $? -ne 0 ]; then
        log "  [FALLO] El inicializador no pudo arrancar:"
        log "  $SALIDA_INIT"
        FAIL=$((FAIL+1))
        return
    fi

    # Arrancar productor en background con tiempos de prueba
    PROD_LOG="/tmp/prod_tc${idx}.log"
    PROD_INTERVAL=$INTERVALO_PRUEBA \
    PROD_DURATION=$DURACION_PRUEBA \
    ./productor > "$PROD_LOG" 2>&1 &
    PROD_PID=$!

    # Esperar el tiempo configurado
    sleep "$TIEMPO_EJECUCION"

    # Tomar snapshot con espia antes de detener
    ESPIA_LOG="/tmp/espia_tc${idx}.log"
    printf "memoria\nprocesos\nsalir\n" | ./espia > "$ESPIA_LOG" 2>&1

    # Detener productor y limpiar
    kill -TERM "$PROD_PID" 2>/dev/null
    sleep 2
    limpiar_ambiente

    # Analizar resultados (grep -c ya devuelve 0 cuando no hay coincidencias)
    local terminados muertos creados libres
    terminados=$(grep -c "termino su ejecucion" "$PROD_LOG" 2>/dev/null); terminados=${terminados:-0}
    muertos=$(grep -c "murio - sin espacio" "$PROD_LOG" 2>/dev/null);     muertos=${muertos:-0}
    creados=$(grep -c "^\[PROD\] PID:" "$PROD_LOG" 2>/dev/null);          creados=${creados:-0}
    libres=$(grep "Espacios libres:" "$ESPIA_LOG" 2>/dev/null | tail -1 | awk '{print $3}')
    libres=${libres:-"?"}

    log ""
    log "  RESULTADOS:"
    log "    Procesos creados     : $creados"
    log "    Terminados con exito : $terminados"
    log "    Muertos sin espacio  : $muertos"
    log "    Paginas libres al fin: $libres / $paginas"
    log ""

    # Adjuntar snapshot de memoria y procesos del espia
    log "  SNAPSHOT DE MEMORIA (espia):"
    awk '/ESTADO DE MEMORIA/,/ESTADO DE PROCESOS/' "$ESPIA_LOG" \
        | grep -v "ESTADO DE PROCESOS" | sed 's/^/    /' | tee -a "$REPORTE"
    log ""
    log "  ESTADO DE PROCESOS (espia):"
    awk '/ESTADO DE PROCESOS/,/Tabla completa/' "$ESPIA_LOG" \
        | grep -v "Tabla completa" | sed 's/^/    /' | tee -a "$REPORTE"
    log ""

    # Verificar resultado esperado
    local resultado="PASO"
    if [ "$espera_muertos" = "si" ] && [ "$muertos" -eq 0 ]; then
        resultado="FALLO (se esperaban procesos muertos pero ninguno murio)"
    fi
    if [ "$espera_muertos" = "no" ] && [ "$muertos" -gt 0 ]; then
        resultado="FALLO (no se esperaban muertos pero murieron $muertos)"
    fi
    if [ "$creados" -eq 0 ]; then
        resultado="FALLO (no se creo ningun proceso)"
    fi

    log "  RESULTADO: $resultado"
    if [ "$resultado" = "PASO" ]; then
        PASS=$((PASS+1))
    else
        FAIL=$((FAIL+1))
    fi

    # Guardar log de bitacora del caso
    if [ -f bitacora.log ]; then
        cp bitacora.log "/tmp/bitacora_tc$((idx+1)).log"
    fi
}

# =============================================================================
# EJECUCION DE PRUEBAS
# =============================================================================

# Asegurarse de estar en el directorio correcto
cd "$(dirname "$0")"

# Compilar si hace falta
if [ ! -f ./inicializador ] || [ ! -f ./productor ] || \
   [ ! -f ./espia ] || [ ! -f ./finalizador ]; then
    echo "Compilando..."
    make > /dev/null 2>&1
fi

# Limpiar reporte anterior
> "$REPORTE"

log "======================================================================"
log "  REPORTE DE PRUEBAS - Proyecto 2 Sistemas Operativos"
log "  Paginacion y Segmentacion con Semaforos"
log "======================================================================"
log "  Fecha   : $(date '+%Y-%m-%d %H:%M:%S')"
log "  Sistema : $(uname -sr)"
log ""
log "  PARAMETROS GLOBALES DE PRUEBA:"
log "    PROD_INTERVAL = ${INTERVALO_PRUEBA}s  (spec: 30-60s aleatorio)"
log "    PROD_DURATION = ${DURACION_PRUEBA}s   (spec: 20-60s aleatorio)"
log "    Tiempo por caso: ${TIEMPO_EJECUCION}s"
log "======================================================================"

# Asegurar limpieza inicial
limpiar_ambiente

TOTAL=${#TC_NOMBRE[@]}
for i in $(seq 0 $((TOTAL-1))); do
    ejecutar_caso "$i"
    sleep 1
done

# =============================================================================
# RESUMEN FINAL
# =============================================================================
separador
log ""
log "RESUMEN FINAL"
log ""
log "  Total de casos : $TOTAL"
log "  Pasaron        : $PASS"
log "  Fallaron       : $FAIL"
log ""
for i in $(seq 0 $((TOTAL-1))); do
    log "  Caso $((i+1)): ${TC_NOMBRE[$i]}"
    log "    Paginas=${TC_PAGINAS[$i]} | Algo=${TC_ALGO[$i]} | Muertos esperados=${TC_ESPERA_MUERTOS[$i]}"
    [ -f "/tmp/bitacora_tc$((i+1)).log" ] && \
        log "    Bitacora guardada en: /tmp/bitacora_tc$((i+1)).log"
done
log ""
separador
log ""
log "Reporte completo en: $REPORTE"

echo ""
echo "Pruebas finalizadas. Ver $REPORTE para el detalle completo."
