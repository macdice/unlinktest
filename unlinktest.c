#include <assert.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/errno.h>
#include <unistd.h>

struct state_t
{
	pthread_mutex_t mutex;
	pthread_cond_t cv;
	int unlinker_done;
	int reader_done;
	int shutdown;
};

static
void *unlinker_function(void *data)
{
	struct state_t *state = (struct state_t *) data;
	int rc;

	rc = pthread_mutex_lock(&state->mutex);
	assert(rc == 0);
	while (!state->shutdown) {
		if (!state->unlinker_done) {

			/* Unlock while we make the syscall, to race the reader. */
			rc = pthread_mutex_unlock(&state->mutex);
			assert(rc == 0);
			rc = unlink("test_file");
			assert(rc == 0);
			rc = pthread_mutex_lock(&state->mutex);
			assert(rc == 0);
			state->unlinker_done = 1;
			rc = pthread_cond_broadcast(&state->cv);
			assert(rc == 0);
		}
		rc = pthread_cond_wait(&state->cv, &state->mutex);
		assert(rc == 0);
	}
	rc = pthread_mutex_unlock(&state->mutex);
	assert(rc == 0);

	return NULL;
}

static
void *reader_function(void *data)
{
	struct state_t *state = (struct state_t *) data;
	int rc;

	rc = pthread_mutex_lock(&state->mutex);
	assert(rc == 0);
	while (!state->shutdown) {
		if (!state->reader_done) {
			int fd;

			/* Unlock while we make the syscalls, to race the unlinker. */
			rc = pthread_mutex_unlock(&state->mutex);
			assert(rc == 0);
			fd = open("test_file", O_RDWR);
			if (fd >= 0) {
				char buffer[1024];

				/* File still exists.  How many bytes can we read? */
				rc = read(fd, buffer, sizeof(buffer));
				if (rc != 11)
					printf("read %d bytes, unexpected\n", rc);
				rc = close(fd);
				assert(rc == 0);
			} else {
				assert(errno == ENOENT);
			}
			rc = pthread_mutex_lock(&state->mutex);
			assert(rc == 0);
			state->reader_done = 1;
			rc = pthread_cond_broadcast(&state->cv);
			assert(rc == 0);
		}
		rc = pthread_cond_wait(&state->cv, &state->mutex);
		assert(rc == 0);
	}
	rc = pthread_mutex_unlock(&state->mutex);
	assert(rc == 0);

	return NULL;
}

int
main(int argc, char *argv[])
{
	struct state_t state;
	pthread_t unlinker_thread;
	pthread_t reader_thread;
	int rc;
	int i;
	int loops;

	if (argc >= 2)
		loops = atoi(argv[1]);
	else
		loops = 1000;

	rc = pthread_mutex_init(&state.mutex, NULL);
	assert(rc == 0);
	rc = pthread_cond_init(&state.cv, NULL);
	assert(rc == 0);
	state.shutdown = 0;

	/* We'll hold the mutex, except during cv wait for workers. */
	rc = pthread_mutex_lock(&state.mutex);
	assert(rc == 0);

	/* Start the threads. */
	rc = pthread_create(&unlinker_thread, NULL, unlinker_function, &state);
	assert(rc == 0);
	rc = pthread_create(&reader_thread, NULL, reader_function, &state);
	assert(rc == 0);

	for (i = 0; i < loops; ++i) {
		int fd;

		/* Create and write to the file. */
		fd = open("test_file", O_RDWR | O_CREAT, 0666);
		assert(fd >= 0);
		rc = write(fd, "hello world", 11);
		assert(rc == 11);
		rc = close(fd);
		assert(rc == 0);

		/* Tell the workers to do their thing. */
		state.reader_done = 0;
		state.unlinker_done = 0;
		rc = pthread_cond_broadcast(&state.cv);
		assert(rc == 0);

		/* Wait for them to say they've done their thing. */
		do {
			rc = pthread_cond_wait(&state.cv, &state.mutex);
			assert(rc == 0);
		} while (!state.reader_done || !state.unlinker_done);
	}

	/* Tell the workers to exit. */
	state.shutdown = 1;
	rc = pthread_mutex_unlock(&state.mutex);
	assert(rc == 0);
	rc = pthread_cond_broadcast(&state.cv);
	assert(rc == 0);

	/* Wait for them to exit. */
	rc = pthread_join(unlinker_thread, NULL);
	assert(rc == 0);
	rc = pthread_join(reader_thread, NULL);
	assert(rc == 0);

	return 0;
}
