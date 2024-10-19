#ifndef JS_H
#define JS_H

#include <stdint.h>
#include <stdbool.h>
#include <threads.h>
#include <stdatomic.h>

#include <linux/joystick.h>

typedef enum {
    JsResult_success,
    JsResult_nothing,
    JsResult_stop,
    JsResult_failure
} JsResult;

/****************************************************************************************************
 *
 * Wrapper for Linux Kernel Interface
 *
 ***************************************************************************************************/

typedef struct js_event JsEvent;

int js_connect(const char * pathname);
void js_disconnect(int js);

JsResult js_get_event(int js, JsEvent * event);
void js_display_event(const JsEvent * event);

/****************************************************************************************************
 * 
 * Query Joystick Properties
 *
 ***************************************************************************************************/

typedef struct JsProperties {
    int driver_version;
    char name[128];
    char number_of_buttons;
    char number_of_axes;
} JsProperties;

JsResult js_get_properties(int js, JsProperties * properties);
void js_display_properties(const JsProperties * properties);

/****************************************************************************************************
 * 
 * Asynchronous Event Handler
 *
 ***************************************************************************************************/

typedef struct JsEventHandler {
    int js;
    void * event_action_arg;
    JsResult (*event_action)(const JsEvent * event, void * arg);

    thrd_t thread_id;
    atomic_bool is_running;
} JsEventHandler;

JsResult js_create_event_handler(JsEventHandler * event_handler);
JsResult js_destroy_event_handler(JsEventHandler * event_handler);
bool js_event_handler_is_running(const JsEventHandler * event_handler);

/****************************************************************************************************
 *
 * Joystick State
 *
 ***************************************************************************************************/

/* maximum number of buttons must be 32, sice we are using a 32-bit uint to store all values */
#define js_max_number_of_buttons 32

/* maximum number of axes may be adjusted freely to fit the hardware */
#define js_max_number_of_axes 8

typedef struct JsState {
    /* time of most recent update */
    uint32_t time;
    /* button values (0/1) */
    uint32_t buttons;
    /* axes values */
    int16_t axes[js_max_number_of_axes];
} JsState;

JsResult js_update_state(JsState * state, const JsEvent * event);

/****************************************************************************************************
 *
 * Asynchronously Updated State
 *
 ***************************************************************************************************/

typedef struct JsAsyncState {
    /* mutex for synchronization */
    mtx_t lock;
    /* event handler */
    JsEventHandler event_handler;
    /* actual state */
    JsState state;
} JsAsyncState;

JsResult js_create_async_state(int js, JsAsyncState * async_state);
JsResult js_destroy_async_state(JsAsyncState * async_state);
JsResult js_query_async_state(JsAsyncState * async_state, JsState * state);

#endif
