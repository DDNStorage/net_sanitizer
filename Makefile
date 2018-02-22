MPICC=mpicc
PROG=net_sanitizer

all: ${PROG}

${PROG}: ${PROG}.o
	${MPICC} ${PROG}.o -o ${PROG}

${PROG}.o: ${PROG}.c
	${MPICC} -Wall -Werror -std=c11 -g -c ${PROG}.c

.PHONY: clean
clean:
	rm *.o ${PROG}
