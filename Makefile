CC      = gcc
CFLAGS  = -Wall -Wextra -pthread -O2
TARGETS = inicializador productor espia finalizador

all: $(TARGETS)

inicializador: inicializador.c common.h
	$(CC) $(CFLAGS) -o $@ $<

productor: productor.c common.h
	$(CC) $(CFLAGS) -o $@ $<

espia: espia.c common.h
	$(CC) $(CFLAGS) -o $@ $<

finalizador: finalizador.c common.h
	$(CC) $(CFLAGS) -o $@ $<

clean:
	rm -f $(TARGETS) bitacora.log /tmp/proj2_producer.pid

.PHONY: all clean
