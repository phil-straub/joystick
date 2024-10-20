# js - A Lightweight Joystick Library for Linux 

## Introduction

`js` is a simple library for interfacing with joysticks, gamepads and similar devices.
It is built on top of the [Linux kernel API](https://docs.kernel.org/input/joydev/joystick-api.html)
and written in C23. It provides three distinct interfaces to joystick-like devices, each built on top
of the previous one:

1. The lowest-level API is simply a wrapper for the kernel functionalities and allows to read out
events produced by the device as well as basic information about the device (name, number of buttons
and axes, etc.). It requires some kind of explicit event handling loop.
2. In order to avoid explicit event handling in the main thread, there is also an event-based API
using a secondary thread to handle events produced by the device by means of a callback function.
3. Finally, there is a state-based API which utilizes a secondary thread to automatically
synchronize with the physical device state. It is probably the prefered interface for most applications.

## Building the Library

Building the library requires only `make` and either `gcc` (at least version 14.2.1) or `clang`
(at least version 18.1.8). First, open the Makefile and set the compiler to either `gcc` or `clang`.
Then, set the variable `CC_FLAGS` to `CC_FLAGS_RELEASE` in order to compile the library with all
available optimizations and without debugging symbols or to `CC_FLAGS_DEBUG` to enable compiler
warnings and to include debugging information. Finally, run `make lib` to compile the library.
This will produce a single object file in `build` containing all library code.
In order to use the library as part of a C or C++ project, include the header file `src/js.h` and
link with the object file `build/js.o` or simply include all source files directly in the project.
For C++ projects, the header should be included as `extern "C" {#include "js.h"}`.

Building the demo program requires downloading and building the signal handling library
[posigs](https://github.com/phil-straub/posigs). Adjust the Makefile variable `POSIGS_PATH` to point
to the installation directory (the default value is `../posigs`) and run `make` to compile both
the library and the executable.

See the Makefile for more information.

## Usage

### Accessing a Joystick

The Linux kernel maps joystick-like devices to virtual files in `/dev/input`, usually `/dev/input/js0`
by default, though the name may change depending on the type of device and on how many devices are
connected. Accessing a device is therefore as simply as calling `open(device_path, flags)` to
obtain a file descriptor and the connection can be closed by passing the file descriptor to `close`.
For convenience, the library wraps these functions as

~~~C
    int js_connect(const char * path);
    void js_disconnect(int js);
~~~

which also automatically provides the appropriate flags `O_RDONLY | O_NONBLOCK` (read only + non-blocking).

The file descriptor returned by `js_connect` should be checked for errors, i.e. it must be `>= 0`
(in fact, `> 2`, since 0, 1, 2 are usually reserved for `stdin`, `stdout`, `stderr`).

### Querying Joystick Properties

Basic joystick properties can be obtained from a valid file descriptor by calling

~~~C
    JsResult js_get_properties(int js, JsProperties * properties);
~~~

with a pointer to a valid `JsProperties` struct. The return value is `JsResult_success`/`JsResult_failure`
in case of a successful/failed call. `JsProperties` is defined as

~~~C
    typedef JsProperties {                                                                         
        int driver_version;
        char name[128];
        char number_of_buttons;
        char number_of_axes;
    } JsProperties;
~~~

and can be printed to `stdout` using

~~~C
    void js_display_properties(const JsProperties * properties);
~~~

### Event Handling in the Main Thread

The Linux kernel provides a queue for storing a finite number of device events (see references)
which can overflow when not emptied quickly enough. In this case, the driver may issue synthetic
events to allow the application to obtain a valid device state. Both physical and synthetic events
can be read from the top of the queue using

~~~C
    JsResult js_get_event(int js, JsEvent * event);
~~~

For debugging puporses, events can be printed to `stdout` with

~~~C
    void js_display_event(const JsEvent * event);
~~~

### Asynchronous Event Handling

Handling events 'by hand' in the main thread may be inconvenient or too slow for real time event handling.
js therefore provides a mechanism for handling events efficiently using a secondary thread
via a user-defined callback function.

To enable asynchronous event handling for a specific device, create an instance of `JsEventHandler`
(either dynamically allocated or as a global/local variable) and fill in the following fields:

+ `js`: file descriptor for the device,
+ `event_action`: the callback function to be executed on each event,
+ `event_action_arg`: a parameter of type `void*` to be passed to `event_action`

Other fields should never be explicitly modified. The signature of the callback function is

~~~C
    JsResult (const JsEvent * event, void * arg);
~~~

and it should return one of the following values:

+ `JsResult_success` to indicate successful execution,
+ `JsResult_failure` to indicate that an error has occured (causing the event handler to terminate)
+ `JsResult_stop` to indicate that the event handler should terminate.

It is important to note that each file descriptor should only be used once and must not be passed to
`js_get_event` or any other library function (other than `js_get_properties`) before being used by
an event handler.

To complete the setup process, call

~~~C
    JsResult js_create_event_handler(JsEventHandler * event_handler);
~~~

on the event handler and check that the result is `JsResult_success`. The event handler will then
execute the callback function on each event and pass `event_action_arg` as a second argument.

To terminate an event handler, call

~~~C
    JsResult js_destroy_event_handler(JsEventHandler * event_handler);
~~~

The return value should be `JsResult_success`, but may be `JsResult_failure` if the event handler
encountered an error at some point (causing it to terminate prematurely).

Finally, the state (running/not running) of an event handler can be checked with

~~~C
    bool js_event_handler_is_running(const JsEventHandler * event_handler);
~~~

__Notes__: The `JsEventHandler` pointer passed to `js_create_event_handler` must stay valid until
`js_destroy_event_handler` is called on it. Furthermore, it must not be modified by any non-library
function. It is also important to keep in mind that the callback function should execute as quickly
as possible to avoid overflowing the event queue provided by the kernel. If this turns out to be an
issue, the cleanest solution is to implement a secondard, dynamically growing queue and have the
callback function transfer each event to the secondary queue for asynchronous handling by another
thread. For most purposes (games, remotely controlled devices) events should be handled in real time
anyway, however. One possibility is to ignore all incoming events (but retrieve them from the queue)
while an event is being handled by another thread.

### State-Based Interface

The most straightforward way to interface with a device is to simply read out its state whenever
required or convenient. This can be done by passing a valid file descriptor (that has not already
been passed to any other library function other than `js_get_properties`) to

~~~C
    JsResult js_create_async_state(int js, JsAsyncState * async_state);
~~~

together with a pointer to a dynamically or statically allocated `JsAsyncState`. The return value
is either `JsResult_success` if the `JsAsyncState` was created successfully or `JsResult_failure`
otherwise. The current device state can then be retrieved at any time by calling

~~~C
    JsResult js_query_async_state(JsAsyncState * async_state, JsState * state);
~~~

to store it in a 'static' (not automatically updated) `JsState` struct. Finally, the `JsAsyncState`
should be destroyed by calling

~~~C
    JsResult js_destroy_async_state(JsAsyncState * async_state);
~~~

on it. Note that the pointer must stay valid between the calls to `js_create_async_state` and
`js_destroy_async state` and should not accessed in any way other than through `js_query_async_state`.

The state `JsState` of a joystick provides three fields:

+ `time` (`uint32_t`), the time of the last modification of the state by an event
(measured in ms relative to some arbitrary initial time),
+ `buttons` (`uint32_t`), the values (0/1) represented as a sequence of 32 bits cast to an unsigned integer,
i.e. the value of the n-th button is `(buttons & (1 << n))` cast to `bool`,
+ axes (`int16_t[]`), the values of all axes represented as an array of `int16_t` (of some maximum length),
i.e. the value of the n-th axis is (`axes[n]`). The array length is determined by the compile-time
constant `js_max_number_of_axes` (= 8 by default).

The number of buttons and axes for a specific device can be queried as described above.

## Running the Demo

If the demo was build alongside the library, it can be run by either executing `make run` or `build/js`.
Press `ctrl + c` to quit the demo.

## Notes

+ Each file descriptor should only ever be passed to a single type of API in order to avoid
undefined behavior, but opening multiple file descriptor for the same device does not seem to cause
any issues.
+ The library sometimes exhibits erratic behavior when used together with ROS1. A solution seems to be
to only initialize ROS _after_ js is fully initialized. Why this is the case is unclear.
+ So far, the library has only been tested with the Logitech Gamepad F710. There is a separate
header file `src/f710.h` providing mappings between button/axis names to indices for this particular device.
+ Comparable functionality to js is provided by [GLFW](https://glfw.org).
The latter is really meant to be used with graphics libraries like OpenGL and Vulkan and
may only work reliably when a monitor is connected.

## References

+ [Linux Kernel Docs Joystick](https://docs.kernel.org/input/joydev/joystick-api.html)
+ [posigs signal handling library](https://github.com/phil-straub/posigs)
