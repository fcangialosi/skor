#ifndef EVENTQUEUE_H
#define EVENTQUEUE_H

#include <sys/queue.h>
#include <sys/time.h>

struct event {
    struct timeval *remaining_time;
    void *data;

    TAILQ_ENTRY(event) links;
};

TAILQ_HEAD(eventqueue, event);

struct eventqueue *initialize_eventqueue();

void destroy_eventqueue(struct eventqueue *queue);

void add_event(struct eventqueue *queue, struct event *new_event);

void remove_event(struct eventqueue *queue, struct event *old_event);

struct event *find_event(struct eventqueue *queue, int (*comparator)(void *a, void *b), void *comparedto);

/* Does a select() on the provided fd_set, with a timeout set to the remaining_time on the event at the 
 * head of the eventqueue. When the select returns, the remaining_time of all events in the queue are 
 * decremented by the elapsed time. 
 *
 * If an error occurs while select()-ing, the errno is returned; the value of timedout_event is undefined. 
 *
 * Otherwise, if the remaining_time of the event at the head of the queue is <= 0 then 1) that event is 
 * removed from the queue, 2) timedout_event is set to point to the event, and 3) 0 is returned.
 *
 * If the remaining_time at the head of the queue is > 0, timedout_event is set to NULL and 0 is returned.
 */
int select_until_event(int max_fd, fd_set *read_fds, fd_set *write_fds, struct eventqueue *queue, struct event **timedout_event);

#endif
