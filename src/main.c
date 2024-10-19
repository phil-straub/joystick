#include <stdlib.h>
#include <stdio.h>
#include <inttypes.h>
#include <stdatomic.h>

#include <posigs.h>

#include "js.h"

static const char js_std_pathname[] = "/dev/input/js0";

static atomic_bool is_running = true;

static void sigint_action(int signum, void * arg) {if (signum == SIGINT) is_running = false;}

int main(int argc, char * argv[])
{
    /*
     * Read Command Line Argument (if there is one)
     */

    if (argc > 2) {
        fprintf(stderr, "Usage: joystick {pathname}\n");
        return EXIT_FAILURE;
    }

    const char * const pathname = (argc == 1 ? js_std_pathname : argv[1]);

    /*
     * Install Signal Handler
     */

    sigset_t sig_set;
    sigemptyset(&sig_set);
    PosigsHandler signal_handler = {
        .sig_set = &sig_set,
        .sig_action = sigint_action,
        .timeout = (struct timespec){.tv_nsec = 10'000'000}
    };
    if (!((sigaddset(&sig_set, SIGINT) == 0) && posigs_create_handler(&signal_handler))) {
        fprintf(stderr, "Error: Unable to install signal handler!\n");
        goto create_signal_handler_error;
    }

    /*
     * Connect to Joystick
     */

    const int js = js_connect(pathname);
    if (js < 0) {
        fprintf(stderr, "Error: Unable to connect to joystick at '%s'\n", pathname);
        goto connect_error;
    }

    /*
     * Print Joystick Properties
     */

    JsProperties properties;
    if (js_get_properties(js, &properties) != JsResult_success) {
        fprintf(stderr, "Error: Unable to obtain joystick properties!\n");
        goto get_properties_error;
    }
    js_display_properties(&properties);

    /*
     * Create Asynchronously Updated State
     */

    JsAsyncState async_state;
    if (js_create_async_state(js, &async_state) != JsResult_success) {
        fprintf(stderr, "Error: Unable to create asynchronous state!\n");
        goto create_async_state_error;
    }

    /*
     * Main Loop
     */

    /* save current cursor state */
    printf("\n\033[s");

    while (is_running) {
        /* return cursor to initial position and clear screen */ 
        printf("\033[u\033[0J");

        /* obtain current joystick state */
        JsState state;
        if (js_query_async_state(&async_state, &state) != JsResult_success) {
            fprintf(stderr, "Error: Unable to obtain joystick state!\n");
            goto query_async_state_error;
        }

        /* print state */
        printf("Buttons: ");
        for (unsigned int i=0; i<js_max_number_of_buttons; ++i) {
            printf("%u ", (state.buttons & (1 << i)) ? 1 : 0);
            if (i % 4 == 3) {
                printf(" ");
            }
        }
        printf("\nAxes   : ");
        for (unsigned int i=0; i<js_max_number_of_axes; ++i) {
            printf("%-7"PRIi16"  ", state.axes[i]);
        }
        printf("\n");

        fflush(stdout);

        /* sleep */
        thrd_sleep(&(struct timespec){.tv_nsec = 10'000'000}, nullptr);
    }

    /*
     * Cleanup
     */

    js_destroy_async_state(&async_state);
    js_disconnect(js);
    posigs_destroy_handler(&signal_handler);

    return EXIT_SUCCESS;

    /*
     * Error Handling
     *
     * (This is not really necessary, since the program terminates anyway.)
     */

    query_async_state_error:
    {
        js_destroy_async_state(&async_state);
    }
    create_async_state_error:
    get_properties_error:
    {
        js_disconnect(js);
    }
    connect_error:
    {
        posigs_destroy_handler(&signal_handler);
    }
    create_signal_handler_error:
    return EXIT_FAILURE;
}
