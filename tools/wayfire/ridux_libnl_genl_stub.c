#include <stdint.h>

struct nl_sock;
struct nl_msg;
struct genlmsghdr;
struct nlattr;

int genl_connect(struct nl_sock *sk) {
    (void)sk;
    return -1;
}

int genl_ctrl_resolve(struct nl_sock *sk, const char *name) {
    (void)sk;
    (void)name;
    return -1;
}

void *genlmsg_put(struct nl_msg *msg, uint32_t port, uint32_t seq, int family,
                  int hdrlen, int flags, uint8_t cmd, uint8_t version) {
    (void)msg;
    (void)port;
    (void)seq;
    (void)family;
    (void)hdrlen;
    (void)flags;
    (void)cmd;
    (void)version;
    return 0;
}

struct nlattr *genlmsg_attrdata(const struct genlmsghdr *gnlh, int hdrlen) {
    (void)gnlh;
    (void)hdrlen;
    return 0;
}

int genlmsg_attrlen(const struct genlmsghdr *gnlh, int hdrlen) {
    (void)gnlh;
    (void)hdrlen;
    return 0;
}
