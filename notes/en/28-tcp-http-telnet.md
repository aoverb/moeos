## Homemade OS (28): TCP (Part 5) — HTTP, TELNET

Our TCP stack is so useful — it would be a waste not to put it to good use!

### HTTP Source Retrieval

We can fetch HTTP source code!

```
This article is incomplete... under construction.
```

HTTP connections are novel at first, but they get boring after a while. Let's do something more interesting — launch a **Telnet server**.

### Telnet Service

Telnet is a simple remote network login protocol.

It transmits only two types of data: **negotiation** and **regular data**.

We can reject all negotiations and just handle echo directly.

```cpp
int main(int argc, char** argv) {
    int conn = open("/sock/tcp", O_CREATE);
    if (conn == -1) {
        printf("telnetd: tcp unsupported!\n");
        return 0;
    }

    sockaddr bindaddr;
    bindaddr.addr = SOCKADDR_BROADCAST_ADDR;
    bindaddr.port = 8080;
    if (ioctl(conn, "SOCK_IOC_BIND", &bindaddr) < 0) {
        printf("telnetd: failed to bind %s:%d\n", bindaddr.addr, bindaddr.port);
        return 0;
    }

    if (listen(conn, 5)) {
        printf("failed to listen!\n");
        return 0;
    }
    printf("Telnet Server listening...\n");
    int client_fd;
    while (client_fd = accept(conn, nullptr, nullptr)) {
        if (client_fd == -1) break;
        printf("New session: %d\n", client_fd);
        handle_session(client_fd);
    }

    close(conn);
    return 0;
}
```

First the main flow.

After accepting a new session, the overall approach is: create pipes, launch a shell, grab the TCP connection, and act as an intermediary. We respond to telnet input, reject all negotiations, strip `\r` before passing data to the shell, and relay the shell's output back to telnet:

```cpp
void handle_session(int conn) {
    int shell_in[2];  // read end: shell's stdin; write end: we control
    int shell_out[2]; // read end: we hold; write end: shell's stdout

    int ret = pipe(shell_in);
    int ret2 = pipe(shell_out);
    // We get data from the TCP connection, process it, and pass it to the shell
    int shell_pid = execute_shell(shell_in[0], shell_out[1]);
    if (shell_pid == -1 || ret == -1 || ret2 == -1) {
        close(shell_in[0]);
        close(shell_in[1]);
        close(shell_out[0]);
        close(shell_out[1]);
        close(conn);
        return;
    }
    pollfd fds[2] = {
        { .fd = shell_out[0], .events = POLLIN, .revents = 0}, // shell stdout
        { .fd = conn, .events = POLLIN, .revents = 0 }
    };

    char buff[256];
    while(1) {
        int ret = poll(fds, 2, -1);  // -1 = infinite wait
        if (ret < 0) { break; }

        // Shell has output
        if (fds[0].revents & POLLIN) {
            handle_console(shell_out[0], conn);
        }

        // Remote data arrived
        if (fds[1].revents & POLLIN) {
            handle_conn(conn, shell_in[1]);
        }
    }
    close(conn);
}
```

We control the shell's I/O based on the connection status.

```cpp
constexpr char IAC  = (char)255;
constexpr char DONT = (char)254;
constexpr char DO   = (char)253;
constexpr char WONT = (char)252;
constexpr char WILL = (char)251;
constexpr char SB   = (char)250;
constexpr char SE   = (char)240;
```

These are the Telnet negotiation characters. We reject all negotiations. When we encounter `IAC`, we check if it's a 3-byte sequence:
- For `DO` series, reply with `WONT`
- For `WILL` series, reply with `DONT`
- For `SB` (subnegotiation), skip everything between `SB` and `SE`
- For 2-byte `IAC` sequences, just skip them
- Everything else is regular data: replace `\r\n` with `\n`

```cpp
void handle_conn_out(char* in, int in_len, char* iac, int& iac_len, char* con, int& con_len) {
    iac_len = 0;
    con_len = 0;
    for (int i = 0; i < in_len; ) {
        if (in[i] == IAC) {
            if (in_len - i < 3) break;
            if (in[i + 1] == DO || in[i + 1] == DONT) { // Reject with WONT
                iac[iac_len++] = IAC;
                iac[iac_len++] = WONT;
                iac[iac_len++] = in[i + 2];
                i += 3;
            } else if (in[i + 1] == WILL || in[i + 1] == WONT) { // Reject with DONT
                iac[iac_len++] = IAC;
                iac[iac_len++] = DONT;
                iac[iac_len++] = in[i + 2];
                i += 3;
            } else if (in[i + 1] == SB) { // Subnegotiation, just drop it
                while (i + 1 < in_len && !(in[i] == IAC && in[i + 1] == SE)) {
                    ++i;
                }
                i += 2;
            } else {
                i += 2; // 2-byte IAC
            }
        } else {
            if (in[i] == '\r') {
                con[con_len++] = '\n';
                ++i;
            } else {
                con[con_len++] = in[i];
            }
            ++i;
        }
    }
}
```

IAC negotiation responses go back to the telnet client; everything else goes to the shell's pipe write end.

```cpp
static char conn_buff[1500];

static char iac_buff[1500];
static char output_buff[1500];
void handle_conn(int in, int out) {
    // Mainly handles negotiation characters and converts \r\n to \n
    int len;
    if ((len = read(in, conn_buff, 256)) > 0) {
         
        int iac_outlen = 0;
        int outlen = 0;
        handle_conn_out(conn_buff, len, iac_buff, iac_outlen, output_buff, outlen);
        if (iac_outlen > 0) {
            write(in, iac_buff, iac_outlen); // Send negotiation rejection to telnet
        }
        if (outlen > 0) {
            write(out, output_buff, outlen);
        }
    }
}
```

And for the shell's output, we convert `\n` to `\r\n`.

#### Result

![image-20260312235644616](../assets/自制操作系统（28）：TCP（五）——HTTP、TELNET/image-20260312235644616.png)

I have to say, the telnet terminal is way more usable than our built-in one...

I feel like, from this moment on, our operating system has undergone a qualitative transformation!

---

In the next chapter, let's implement UDP and DNS protocol support!
