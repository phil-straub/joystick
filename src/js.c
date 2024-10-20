#include <stdlib.h>
#include <stdio.h>
#include <threads.h>
#include <inttypes.h>
#include <errno.h>

#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <linux/joystick.h>

#include "js.h"

/****************************************************************************************************
 *
 * Serial Interface for Reading Individual Events
 *
 ***************************************************************************************************/

int js_connect(const char * path)
{
    return open(path, O_RDONLY | O_NONBLOCK);
}

void js_disconnect(int js)
{
    close(js);
}

JsResult js_get_event(int js, JsEvent * event)
{
    const ssize_t s = read(js, event, sizeof(*event));

    /* valid event */
    if (s == sizeof(*event)) {
        return JsResult_success;
    }

    /* not enough bytes (this should never happen) */
    if (s > 0) {
        return JsResult_failure;
    }

    /* no event */
    if (s == 0 || (s == -1 && (errno == EAGAIN || errno == EWOULDBLOCK))) {
        return JsResult_nothing;
    }

    /* unknow error */
    return JsResult_failure;
}

void js_display_event(const JsEvent * event)
{
    printf(event->type & JS_EVENT_BUTTON ? "button" : "axis  ");
    printf(" [%.2"PRIu8"] -> %6"PRIi16, event->number, event->value);
    printf(" at time %fs", ((float) event->time) / 1000.0f); /* convert time from ms to s */

    printf("\n");
}

/****************************************************************************************************
 *
 * Query Joystick Properties
 *
 ***************************************************************************************************/

JsResult js_get_properties(int js, JsProperties * properties)
{
    return (
        ioctl(js, JSIOCGNAME(sizeof(properties->name)), properties->name) >= 0
        && ioctl(js, JSIOCGVERSION, &properties->driver_version) >= 0
        && ioctl(js, JSIOCGBUTTONS, &properties->number_of_buttons) >= 0
        && ioctl(js, JSIOCGAXES, &properties->number_of_axes) >= 0
        ? JsResult_success
        : JsResult_failure
    );
}

void js_display_properties(const JsProperties * properties)
{
    printf("%s {driver version: %d, number of axes: %d, number of buttons: %d}\n",
        properties->name, properties->driver_version,
        (int) properties->number_of_axes, (int) properties->number_of_buttons
    );
}

/****************************************************************************************************
 *
 * Asynchronous Event Handler
 *
 ***************************************************************************************************/

static const struct timespec js_timeout = {.tv_nsec = 100000};

static int js_event_handler_main(void * arg)
{
    JsEventHandler * const event_handler = (JsEventHandler*) arg;

    while (event_handler->is_running) {
        /* obtain next event */
        JsEvent event;
        switch (js_get_event(event_handler->js, &event)) {
            /* event successfully read */
            case JsResult_success:
                break;
            /* no event -> continue with loop */
            case JsResult_nothing:
                thrd_sleep(&js_timeout, nullptr);
                continue;
            /* error */
            default:
                goto exit_failure;
        }

        /* handle event */
        switch (event_handler->event_action(&event, event_handler->event_action_arg)) {
            /* stop */
            case JsResult_stop:
                goto exit_success;
            /* error */
            case JsResult_failure:
                goto exit_failure;
            /* continue */
            default:
                break;
        }
    }

    exit_success:
    return EXIT_SUCCESS;

    exit_failure:
    event_handler->is_running = false;
    return EXIT_FAILURE;
}

JsResult js_create_event_handler(JsEventHandler * event_handler)
{
    atomic_init(&event_handler->is_running, true);

    /* create event handling thread */
    return (
        thrd_create(
            &event_handler->thread_id,
            js_event_handler_main,
            (void*) event_handler
        ) == thrd_success ? JsResult_success : JsResult_failure
    );
}

JsResult js_destroy_event_handler(JsEventHandler * event_handler)
{
    event_handler->is_running = false;

    int return_val;
    const int join_result = thrd_join(event_handler->thread_id, &return_val);
    return (
        join_result == thrd_success && return_val == EXIT_SUCCESS
        ? JsResult_success
        : JsResult_failure
    );
}

bool js_event_handler_is_running(const JsEventHandler * event_handler)
{
    return event_handler->is_running;
}

/****************************************************************************************************
 *
 * Joystick State
 *
 ***************************************************************************************************/

JsResult js_update_state(JsState * state, const JsEvent * event)
{
    state->time = event->time;

    if (event->type & JS_EVENT_BUTTON) {
        if (event->number >= js_max_number_of_buttons) {
            return JsResult_failure;
        }

        if (event->value) {
            state->buttons |= (1 << event->number);
        }
        else {
            state->buttons &= ~(1 << event->number);
        }
    }
    else if (event->type & JS_EVENT_AXIS) {
        if (event->number >= js_max_number_of_axes) {
            return JsResult_failure;
        }

        state->axes[event->number] = event->value;
    }
    else {
        return JsResult_failure;
    }
    return JsResult_success;
}

/****************************************************************************************************
 *
 * Asynchronously Updated State
 *
 ***************************************************************************************************/

static JsResult js_async_state_event_action(const JsEvent * event, void * arg)
{
    JsAsyncState * const async_state = (JsAsyncState*) arg;
    if (mtx_lock(&async_state->lock) != thrd_success) {
        return JsResult_failure;
    }
    const JsResult r = js_update_state(&async_state->state, event);
    if (mtx_unlock(&async_state->lock) != thrd_success) {
        return JsResult_failure;
    }
    return r;
}

JsResult js_create_async_state(int js, JsAsyncState * async_state)
{
    async_state->state = (JsState){};

    /* handle initial synthetic events */
    JsEvent event;
    do {
        const JsResult r = js_get_event(js, &event);
        if (r == JsResult_success) {
             if (js_update_state(&async_state->state, &event) != JsResult_success) {
                 return JsResult_failure;
            }
        }
        else if (r == JsResult_nothing) {
            break;
        }
        else {
            return JsResult_failure;
        }
    } while (event.type & JS_EVENT_INIT);

    /* initialize lock */
    if (mtx_init(&async_state->lock, mtx_plain) != thrd_success) {
        return JsResult_failure;
    }

    /* create event handler */
    async_state->event_handler = (JsEventHandler){
        .js = js,
        .event_action_arg = (void*) async_state,
        .event_action = js_async_state_event_action
    };
    if (js_create_event_handler(&async_state->event_handler) != JsResult_success) {
        /* at this point the lock has already been initialized and needs to be destroyed if
         * creating the event handler fails */
        mtx_destroy(&async_state->lock);

        return JsResult_failure;
    }

    return JsResult_success;
}

JsResult js_destroy_async_state(JsAsyncState * async_state)
{
    const JsResult r = js_destroy_event_handler(&async_state->event_handler);
    mtx_destroy(&async_state->lock);
    return r;
}

JsResult js_query_async_state(JsAsyncState * async_state, JsState * state)
{
    if (mtx_lock(&async_state->lock) != thrd_success) {
        return JsResult_failure;
    }
    *state = async_state->state;
    if (mtx_unlock(&async_state->lock) != thrd_success) {
        return JsResult_failure;
    }
    return JsResult_success;
}
