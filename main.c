#include <netinet/in.h>
#include <sys/socket.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include "liburing.h"

#define MAX_CLIENTS	2
#define MAX_SIZE	128

unsigned char msg[] = "ACCEPTED\n";
unsigned char stop = 0;

void stop_handler(int s)
{
	printf("Forced stop\n");
	stop = 1;
}

int main(int argc, char *argv[])
{
	int retval = 0;

	if (argc != 2) {
		printf("Usage: %s port\n", argv[0]);
		retval = -1;
		goto cleanup;
	}

	unsigned short port = atoi(argv[1]);
	if (port <= 0) {
		printf("Error: bad port number\n");
		retval = -2;
		goto cleanup;
	}

	unsigned char fname[16];
	sprintf(fname, "%d.txt", port);
	int outfile_fd = open(fname, O_WRONLY | O_CREAT | O_TRUNC, 0644);
	if (outfile_fd < 0) {
		printf("Error: unable create file %s\n", fname);
		retval = -3;
		goto cleanup;
	}

	int lsock = socket(AF_INET, SOCK_STREAM, 0);
	if (lsock == -1) {
		printf("Error: unable create socket (%d)\n", errno);
		retval = -4;
		goto cleanup1;
	}
	fcntl(lsock, F_SETFL, fcntl(lsock, F_GETFL) | O_NONBLOCK);

	struct sockaddr_in saddr;
	saddr.sin_family = AF_INET;
	saddr.sin_addr.s_addr = INADDR_ANY;
	saddr.sin_port = htons(port);
	if (bind(lsock, (struct sockaddr *)&saddr, sizeof(saddr)) == -1) {
		printf("Error: unable bind socket (%d)\n", errno);
		retval = -5;
		goto cleanup2;
	}

	if (listen(lsock, MAX_CLIENTS) == -1) {
		printf("Error: unable listen socket (%d)\n", errno);
		retval = -6;
		goto cleanup2;
	}

	struct io_uring ring;
	if (io_uring_queue_init(MAX_CLIENTS, &ring, 0) != 0) {
		printf("Error: uring init\n");
		retval = -7;
		goto cleanup2;
	}

	signal(SIGINT, stop_handler);

	struct io_uring_sqe *sqe, *sqe2;
	struct io_uring_cqe *cqe;

	struct sockaddr paddr;
	int paddr_size = 0;
	int psock[MAX_CLIENTS];
	unsigned char buffer[MAX_SIZE];

	int i;
	for (i=0;i<MAX_CLIENTS;i++) psock[i] = -1;

	while(!stop) {
		printf(".\n");

		/* accept new client when free slot(s) in pool */
		for (i=0;i<MAX_CLIENTS;i++) {
			if (psock[i] == -1) {
				psock[i] = accept(lsock, &paddr, &paddr_size);
				if (psock[i] != -1) {
					printf("Client #%x connected!\n", psock[i]);
					fcntl(psock[i], F_SETFL, fcntl(psock[i], F_GETFL) | O_NONBLOCK);
				}
				break;
			}
		}

		/* read data from clients and put them to file */
		int br;
		for (i=0;i<MAX_CLIENTS;i++) {
			if (psock[i] != -1) {	/* alive client in pool */
				br = read(psock[i], buffer, sizeof(buffer));
				if (br == 0) {
					printf("Client #%x disconnected\n", psock[i]);
					psock[i] = -1;
				} else if (br != -1) {
					printf("Client #%x sent data! (%d bytes)\n", psock[i], br);

					sqe = io_uring_get_sqe(&ring);
					if (sqe == NULL) {
						printf("Error: sqe for write\n");
						break;
					}
					io_uring_prep_write(sqe, outfile_fd, buffer, br, -1);
					sqe->user_data = -1;
					sqe->flags |= IOSQE_IO_LINK;

					sqe2 = io_uring_get_sqe(&ring);
					if (sqe2 == NULL) {
						printf("Error: sqe for timeout\n");
						break;
					}
					struct __kernel_timespec ts = { .tv_sec = 3, .tv_nsec = 0, };
					io_uring_prep_timeout(sqe2, &ts, 0, 0);
					sqe2->user_data = psock[i];

					if (io_uring_submit(&ring) < 0) {
						printf("Error: submit\n");
					}
				}
			}
		}

		/* read receipts from file records */
		while (io_uring_peek_cqe(&ring, &cqe) == 0) {
			io_uring_cqe_seen(&ring, cqe);
			if (cqe->user_data != -1) {
				printf("Record for client #%llx with timeout completed\n", cqe->user_data);
				int bw = write(cqe->user_data, msg, sizeof(msg));
			}
		}

		sleep(1);
	}

	for (i=0;i<MAX_CLIENTS;i++) {
		if (psock[i] != -1) close(psock[i]);
	}
	io_uring_queue_exit(&ring);

cleanup2:
	close(lsock);
cleanup1:
	close(outfile_fd);
cleanup:
	return retval;
}
