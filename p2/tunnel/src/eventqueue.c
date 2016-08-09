#define _BSD_SOURCE 1

#include <errno.h>
#include <stdlib.h>
#include <sys/queue.h>
#include <sys/time.h>

#include "eventqueue.h"

struct eventqueue *initialize_eventqueue() {
	struct eventqueue *queue = calloc(1, sizeof(struct eventqueue));
	TAILQ_INIT(queue);
	return queue;
}

void destroy_eventqueue(struct eventqueue *queue) {
	free(queue);
}

void add_event(struct eventqueue *queue, struct event *new_event) {
	if (!new_event->remaining_time) {
		TAILQ_INSERT_TAIL(queue, new_event, links);
		return;
	}

	struct event *current;
	for (current = TAILQ_LAST(queue, eventqueue); current; current = TAILQ_PREV(current, eventqueue, links))
		if (current->remaining_time && timercmp(new_event->remaining_time, current->remaining_time, >=)) {
			TAILQ_INSERT_AFTER(queue, current, new_event, links);
			return;
		}

	TAILQ_INSERT_HEAD(queue, new_event, links);
}

void remove_event(struct eventqueue *queue, struct event *old_event) {
	TAILQ_REMOVE(queue, old_event, links);
}

struct event *find_event(struct eventqueue *queue, int (*comparator)(void *a, void *b), void *comparedto){
	struct event *current;
	for (current = TAILQ_FIRST(queue); current; current = TAILQ_NEXT(current, links))
		if (comparator(current->data, comparedto))
			return current;
	return NULL;
}

int select_until_event(int max_fd, fd_set *read_fds, fd_set *write_fds, struct eventqueue *queue, struct event **timedout_event) {
	struct timeval start_time;
	gettimeofday(&start_time, NULL);

	// Select until the next event times out
	struct event *head = TAILQ_FIRST(queue);
	struct timeval *remaining_time = head ? head->remaining_time : NULL;
	int select_result = select(max_fd + 1, read_fds, write_fds, NULL, remaining_time);
	int return_code = select_result < 0 ? errno : 0;

	// Subtract elapsed time from all events
	struct timeval elapsed_time;
	gettimeofday(&elapsed_time, NULL);
	timersub(&elapsed_time, &start_time, &elapsed_time);

	// debug("%d.%06d seconds elapsed.", (int)elapsed_time.tv_sec, (int)elapsed_time.tv_usec);

	struct event *current;
	for (current = head; current; current = TAILQ_NEXT(current, links)) {
		if (!current->remaining_time)
			break;

		if (timercmp(current->remaining_time, &elapsed_time, <=))
			*current->remaining_time = (const struct timeval){ 0 };
		else
			timersub(current->remaining_time, &elapsed_time, current->remaining_time);
	}

	// If error occurred, don't handle timeouts
	if (return_code)
		return return_code;

	// Otherwise, check for a timeout and remove it from the queue
	if (head && head->remaining_time && !timerisset(head->remaining_time)) {
		// debug("Timeout expired.");
		TAILQ_REMOVE(queue, head, links);
		*timedout_event = head;
	} else {
		// debug("Timeout hasn't expired: %d seconds %u useconds left.", (int)head->remaining_time->tv_sec, (int)head->remaining_time->tv_usec);
		*timedout_event = NULL;
	}

	return return_code;
}
