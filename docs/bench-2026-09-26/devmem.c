#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/mman.h>
#include <stdint.h>
int main(int argc, char **argv) {
    if (argc < 2) { fprintf(stderr, "usage: devmem ADDR [VALUE]\n"); return 2; }
    uintptr_t addr = strtoul(argv[1], NULL, 0); long pg = sysconf(_SC_PAGESIZE);
    int fd = open("/dev/mem", (argc > 2 ? O_RDWR : O_RDONLY) | O_SYNC); if (fd < 0) { perror("open /dev/mem"); return 1; }
    void *m = mmap(NULL, pg, argc > 2 ? PROT_READ | PROT_WRITE : PROT_READ, MAP_SHARED, fd, addr & ~(pg - 1));
    if (m == MAP_FAILED) { perror("mmap"); return 1; }
    volatile uint32_t *r = (volatile uint32_t *)((char *)m + (addr & (pg - 1)));
    if (argc > 2) *r = (uint32_t)strtoul(argv[2], NULL, 0);
    printf("0x%08x\n", *r); return 0;
}
