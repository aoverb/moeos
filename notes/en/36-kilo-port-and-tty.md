## Homemade OS (36): Kilo Port and TTY Upgrades

```
This article is incomplete... work in progress.
```

Today we're porting a text editor to our operating system, and using this opportunity to enhance our TTY capabilities.

#### Kilo Port

Kilo project address: https://github.com/antirez/kilo

#### Filling in Missing Header Files

Kilo requires a header file to toggle our console's echo and character-by-character response functionality. We can have AI help us write a header file, while we implement it ourselves.

#### ioctl Standardization

Our previous `ioctl` took `fd`, `cmd`, and `args`. The standard `ioctl` approach replaces string-form commands with numeric `request` codes.

#### truncate

Kilo requires us to implement truncation (which we should have implemented long ago...)

#### ioctl Implementation and VT100 State Machine

#### termios

`termios` = **term**inal **i**nput/**o**utput **s**ettings. It's the POSIX standard for terminal input/output configuration — a set of configuration specifications.

`termios` consists of four flags:

```cpp
struct termios {
    tcflag_t c_iflag;     /* Input mode flags */
    tcflag_t c_oflag;     /* Output mode flags */
    tcflag_t c_cflag;     /* Control mode flags */
    tcflag_t c_lflag;     /* Local mode flags */
    cc_t     c_cc[NCCS];  /* Control characters */
};
```

The four flag fields in `struct termios` correspond to four stages of terminal data flow:

- `c_iflag` — **i**nput: how to preprocess data when it comes in from the keyboard (e.g., whether to convert carriage returns to newlines)
- `c_oflag` — **o**utput: how to post-process data when it goes to the screen (e.g., whether to automatically add carriage returns before newlines)
- `c_cflag` — **c**ontrol: hardware control parameters (baud rate, data bits — legacy from the serial port era)
- `c_lflag` — **l**ocal: local terminal behavior (whether to echo, whether to line buffer, whether to respond to Ctrl+C)

Users modify or query our console configuration through the user-space interfaces `tcsetattr` and `tcgetattr`. The configuration comes into play when we do `console_read` or `console_write`. Let's first look at how to respond to the user-space interfaces `tcsetattr` and `tcgetattr`.

#### tcsetattr, tcgetattr

```cpp
int tcgetattr(int fd, struct termios *t) {
    return ioctl(fd, TCGETS, t);
}

int tcsetattr(int fd, int action, const struct termios *t) {
    unsigned long req;
    switch (action) {
    case TCSANOW:   req = TCSETS;  break;
    case TCSADRAIN: req = TCSETSW; break;
    case TCSAFLUSH: req = TCSETSF; break;
    default:        return -1;
    }
    return ioctl(fd, req, (void*)t);
}
```

The user-space call is straightforward — it essentially calls the `ioctl` interface of our console (the console device file). The uppercase strings above are the request codes. We go to `console_ioctl` to respond to these requests:

#### Input Configuration

![image-20260318181202986](../assets/自制操作系统（36）：kilo移植，tty能力加强/image-20260318181202986.png)
