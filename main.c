/**
 * Copyright © 2017 IBM Corporation
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <getopt.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <sys/param.h>
#include <pthread.h>
#include <poll.h>
#include <unistd.h>

static const size_t max_words = 128;

struct server_ctx
{
	int fd;
	size_t nr_words;
	volatile bool shutdown;
};

static void usage(FILE *stream, char *pname)
{
	fprintf(stream, "Usage: %s [options] command\n", pname);
	fprintf(stream, " Commands:\n");
	fprintf(stream, "\tsend DEVICE FILE\tSend data and display response.\n");
	fprintf(stream, "\tserver DEVICE\tRead data from stdin, send to fifo and display response.\n");
	fprintf(stream, " Options:\n");
	fprintf(stream, "\t-c COUNT\tNumber of words for read.\n");
}

void *server_thread(void *data)
{
	struct server_ctx *ctx = data;
	size_t nr_words = ctx->nr_words;
	uint32_t buf[max_words];
	ssize_t ret = 0;
	int i;

	if (nr_words > max_words)
		nr_words = max_words;

	while (!ctx->shutdown) {
		if((ret = read(ctx->fd, buf, nr_words << 2)) < 0) {
			perror("Reading fifo device failed");
			ctx->shutdown = true;
			ret = -1;
			goto shutdown;
		}

		printf("  ");
		for (i = 0; i < ret >> 2; i++)
			printf("%08x ", buf[i]);
		printf("\n");
	}

shutdown:
	if(ctx->shutdown)
		printf("aborted\n");

	pthread_exit(&ret);
}

int do_server(const char *fifo, size_t nr_words)
{
	size_t wordcount, linesz;
	uint32_t buf[max_words];
	struct server_ctx ctx;
	char *p, *line = NULL;
	int i, ret, pos;
	pthread_t tid;

	ctx.nr_words = nr_words;
	ctx.shutdown = false;

	if ((ctx.fd = open(fifo, O_RDWR)) < 0) {
		perror("Opening fifo failed");
		return -1;
	}

	if (pthread_create(&tid, NULL, server_thread, &ctx)) {
		perror("Creating server thread failed");
		return -1;
	}

	while (!ctx.shutdown && (ret = getline(&line, &linesz, stdin)) > 0) {
		wordcount = 0;
		p = line;
		while (sscanf(p, "%x %n", buf + wordcount, &pos) == 1) {
			p += pos;
			if (wordcount > max_words) {
				ctx.shutdown = true;
				fprintf(stderr, "user input too large (%d words max)\n",
						max_words);
				goto join;
			}
			wordcount++;
		}
		free(line);
		line = NULL;

		if (!wordcount)
			continue;

		if((ret = write(ctx.fd, buf, wordcount << 2) < 0)) {
			ctx.shutdown = true;
			perror("Writing user input failed");
			ctx.shutdown = true;
			goto join;
		}
	}

	if (ret < 0) {
		perror("Reading user input failed");
		ctx.shutdown = true;
		goto join;
	}

join:
	pthread_join(tid, NULL);
	if (close(ctx.fd))
		perror("Error on fifo close");
	return ret;
}

int do_send(const char *fifo, const char *file, size_t nr_words)
{
	FILE *stream = stdin;
	struct pollfd pollfd;
	size_t wordcount = 0;
	uint32_t buf[max_words];
	int i, fd;
	int ret;

	if ((fd = open(fifo, O_RDWR|O_NONBLOCK)) < 0) {
		perror("Opening fifo failed");
		return -1;
	}

	if (strcmp(file, "-"))
		stream = fopen(file, "r");

	if (!stream) {
		perror("Opening user input file failed");
		return -1;
	}

	while (fscanf(stream, "%x ", buf + wordcount) == 1) {
		wordcount++;
		if (wordcount > max_words) {
			fprintf(stderr, "user input too large (%d words max)\n",
					max_words);
			return -1;
		}
	}

	pollfd.fd = fd;
	pollfd.events = POLLOUT | POLLERR;

	if((ret = poll(&pollfd, 1, -1)) < 0) {
		perror("Waiting for fifo device failed");
		return -1;
	}

	if (pollfd.revents & POLLERR) {
		fprintf(stderr, "POLLERR while waiting for writeable fifo\n");
		return -1;
	}

	if((ret = write(fd, buf, wordcount << 2) < 0)) {
		perror("Writing user input failed");
		return -1;
	}

	pollfd.fd = fd;
	pollfd.events = POLLIN | POLLERR;

	if((ret = poll(&pollfd, 1, -1) < 0)) {
		perror("Waiting for fifo device failed");
		return -1;
	}

	if (pollfd.revents & POLLERR) {
		fprintf(stderr, "POLLERR while waiting for readable fifo\n");
		return -1;
	}

	while (1) {
		if((ret = read(fd, buf, nr_words << 2)) < 0) {
			if (errno == EAGAIN)
				break;
			perror("Reading fifo device failed");
			return -1;
		}

		for (i = 0; i < ret >> 2; i++)
			printf("%08x ", buf[i]);
		printf("\n");
	}

	if (close(fd))
		perror("Error on fifo close");

	if (stream != stdin)
		if (fclose(stream))
			perror("Error on user input file close");

	return 0;
}

int main(int argc, char** argv)
{
	int opt;
	const char* fifo = NULL;
	const char* file = NULL;
	size_t nr_words = 32;
	int ret = 0;

	while ((opt = getopt(argc, argv, "c:")) != -1) {
		switch (opt) {
			case 'c':
				nr_words = strtoul(optarg, NULL, 0);
				break;

			default:
				ret = 1;
				goto exit;
		}
	}

	if (optind == argc) {
		fprintf(stderr, "%s; No command specified.\n", argv[0]);
		usage(stderr, argv[0]);
		ret = 1;
		goto exit;
	}

	if (!strcmp(argv[optind], "send")) {
		if (optind + 1 == argc) {
			fprintf(stderr, "%s; No fifo target device specified.\n",
					argv[0]);
			ret = 1;
			goto exit;
		}
		if (optind + 2 == argc) {
			fprintf(stderr, "%s; No data file specified.\n",
					argv[0]);
			ret = 1;
			goto exit;
		}

		fifo = argv[optind + 1];
		file = argv[optind + 2];
		ret = do_send(fifo, file, nr_words);
		if (ret)
			goto exit;

	} else if (!strcmp(argv[optind], "server")) {
		if (optind + 1 == argc) {
			fprintf(stderr, "%s; No fifo target device specified.\n",
					argv[0]);
			ret = 1;
			goto exit;
		}

		fifo = argv[optind + 1];
		ret = do_server(fifo, nr_words);
		if (ret)
			goto exit;

	} else {
		fprintf(stderr, "%s; Unrecognized command \"%s\"\n",
				argv[optind]);
		ret = 1;
		goto exit;
	}

exit:
	if (ret == 1)
		usage(stderr, argv[0]);

	exit(ret);
}
