#ifndef F710_H
#define F710_H

typedef enum {
    F710Button_A     = (1 << 0),
    F710Button_B     = (1 << 1),
    F710Button_X     = (1 << 2),
    F710Button_Y     = (1 << 3),
    F710Button_LB    = (1 << 4),
    F710Button_RB    = (1 << 5),
    F710Button_back  = (1 << 6),
    F710Button_start = (1 << 7),
    F710Button_logo  = (1 << 8)
} F710Button;

typedef enum {
    F710Axis_left_x  = 0,
    F710Axis_left_y  = 1,
    F710Axis_LT      = 2,
    F710Axis_right_x = 3,
    F710Axis_right_y = 4,
    F710Axis_RT      = 5,
    F710Axis_arrow_x = 6,
    F710Axis_arrow_y = 7
} F710Axis;

#endif
