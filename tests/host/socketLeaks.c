#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <malloc.h>
#include <netinet/in.h>
#include <stdio.h>
#include <sys/socket.h>
#include <unistd.h>

void ejcInit(int argc, char **argv);

int ejcTestConnect(int port);
int ejcTestListen(int port);
int ejcTestReadClosed(int port, int count);

static int openDescriptors(void) {
    int open = 0;
    for (int fd = 0; fd < 1024; fd++) {
        if (fcntl(fd, F_GETFD) != -1) {
            open++;
        }
    }
    return open;
}

/* Binds a loopback socket to an ephemeral port. It listens only if listening is nonzero. */
static int reservePort(int listening, int *port) {
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    struct sockaddr_in address = {0};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    socklen_t size = sizeof(address);
    if (bind(fd, (struct sockaddr *)&address, sizeof(address)) == -1 ||
        getsockname(fd, (struct sockaddr *)&address, &size) == -1 || (listening && listen(fd, 1) == -1)) {
        return -1;
    }
    *port = ntohs(address.sin_port);
    return fd;
}

static long heapBytes(void) {
    struct mallinfo2 info = mallinfo2();
    return (long)(info.uordblks + info.hblkhd);
}

int main(int argc, char **argv) {
    ejcInit(argc, argv);
    int failed = 0;

    /* A bound socket that does not listen refuses connections; once it listens, its port is taken. */
    int port;
    int reserved = reservePort(0, &port);
    if (reserved == -1) {
        printf("cannot reserve a port\n");
        return 1;
    }
    int before = openDescriptors();
    int connected = 0;
    for (int i = 0; i < 20; i++) {
        connected += ejcTestConnect(port);
    }
    printf("refused connects succeeded: %d, descriptors leaked: %d\n", connected, openDescriptors() - before);
    failed |= connected != 0 || openDescriptors() != before;

    listen(reserved, 64);
    before = openDescriptors();
    int listened = 0;
    for (int i = 0; i < 20; i++) {
        listened += ejcTestListen(port);
    }
    printf("listens on a used port succeeded: %d, descriptors leaked: %d\n", listened, openDescriptors() - before);
    failed |= listened != 0 || openDescriptors() != before;

    /* Successful connects and listeners are closed again. */
    before = openDescriptors();
    int successes = 0;
    for (int i = 0; i < 20; i++) {
        successes += ejcTestConnect(port);
    }
    printf("connects succeeded: %d, descriptors leaked: %d\n", successes, openDescriptors() - before);
    failed |= successes != 20 || openDescriptors() != before;

    /* Reading from a closed socket fails and must release its buffer. */
    long heap = heapBytes();
    int readFailed = 0;
    for (int i = 0; i < 20; i++) {
        readFailed += ejcTestReadClosed(port, 1 << 20);
    }
    long growth = heapBytes() - heap;
    printf("failed reads: %d, buffers leaked: %s\n", readFailed, growth > (4 << 20) ? "yes" : "no");
    failed |= readFailed != 20 || growth > (4 << 20);

    return failed;
}
