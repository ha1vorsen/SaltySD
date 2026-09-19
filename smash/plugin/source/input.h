#ifndef SALTYSD_INPUT_H
#define SALTYSD_INPUT_H

#define KEY_A          (1u << 0)
#define KEY_B          (1u << 1)
#define KEY_START      (1u << 3)
#define KEY_DRIGHT     (1u << 4)
#define KEY_DLEFT      (1u << 5)
#define KEY_DUP        (1u << 6)
#define KEY_DDOWN      (1u << 7)
#define KEY_CPAD_RIGHT (1u << 28)
#define KEY_CPAD_LEFT  (1u << 29)
#define KEY_CPAD_UP    (1u << 30)
#define KEY_CPAD_DOWN  (1u << 31)
#define KEY_UP         (KEY_DUP | KEY_CPAD_UP)
#define KEY_DOWN       (KEY_DDOWN | KEY_CPAD_DOWN)
#define KEY_LEFT       (KEY_DLEFT | KEY_CPAD_LEFT)
#define KEY_RIGHT      (KEY_DRIGHT | KEY_CPAD_RIGHT)

int input_open(void);
unsigned int input_held(void);

#endif
