/*
 * API de red, X11 y Wayland para clientes Linux.
 */
#ifndef RIDUX_COMPAT7_H
#define RIDUX_COMPAT7_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "base.h"
#include "memory_tasks.h"
#include "linux_syscalls.h"
#include "user_libc.h"
#include "bsd_libc.h"
#include "linux_abi.h"

/* Real TCP state machine + congestion control */
/* TCP states (already in compat.h but re-declared for clarity) */
#define TCP7_CLOSED       0
#define TCP7_LISTEN       1
#define TCP7_SYN_SENT     2
#define TCP7_SYN_RCVD     3
#define TCP7_ESTABLISHED  4
#define TCP7_FIN_WAIT_1   5
#define TCP7_FIN_WAIT_2   6
#define TCP7_CLOSE_WAIT   7
#define TCP7_CLOSING      8
#define TCP7_LAST_ACK     9
#define TCP7_TIME_WAIT   10

/* Congestion control states */
#define CC_SLOW_START    0
#define CC_AVOIDANCE     1
#define CC_FAST_RECOVER  2

/* TCP control block - extends socket_t with real TCP state */
typedef struct {
    int        sock_fd;        /* back-reference to g_sockets[] slot */
    int        state;          /* TCP7_* */

    /* Send side */
    uint32_t   snd_una;        /* oldest unacked seq */
    uint32_t   snd_nxt;        /* next seq to send */
    uint32_t   snd_wnd;        /* receiver window */
    uint32_t   snd_up;         /* urgent pointer */
    uint32_t   snd_wl1;        /* window update seq */
    uint32_t   snd_wl2;        /* window update ack */

    /* Receive side */
    uint32_t   rcv_nxt;        /* next expected seq */
    uint32_t   rcv_wnd;        /* receive window */
    uint32_t   rcv_up;         /* urgent pointer */

    /* Congestion control */
    int        cc_state;       /* CC_* */
    uint32_t   cwnd;           /* congestion window (bytes) */
    uint32_t   ssthresh;       /* slow-start threshold */
    uint32_t   dup_acks;       /* duplicate ACK counter */
    uint32_t   recover;        /* recovery point */

    /* Retransmission */
    uint32_t   rto;            /* retransmit timeout (ms) */
    uint32_t   srtt;           /* smoothed RTT (ms) */
    uint32_t   rttvar;         /* RTT variance (ms) */
    uint32_t   rto_count;      /* retransmit count */
    uint32_t   rto_max;        /* max retransmits before give up */
    uint64_t   rto_expire;     /* absolute tick when RTO fires */

    /* Timers */
    uint64_t   time_wait_expire; /* TIME_WAIT expiry tick */
    uint64_t   delack_expire;    /* delayed ACK expiry tick */
    bool       delack_pending;   /* ACK needs to be sent */

    /* MSS / options */
    uint32_t   mss;            /* maximum segment size */
    uint32_t   iss;            /* initial send seq */
    uint32_t   irs;            /* initial recv seq */

    /* Segment reassembly (out-of-order) */
    uint8_t   *ooo_buf;        /* out-of-order segment buffer */
    uint32_t   ooo_seq;        /* start seq of OOO data */
    uint32_t   ooo_len;        /* length of OOO data */
} tcp7_tcb_t;

#define TCP7_MAX_CONNECTIONS 128
#define TCP7_MSS_DEFAULT     1460
#define TCP7_INITIAL_CWND    10    /* segments (RFC 6928) */
#define TCP7_INITIAL_SSTHRESH 65535
#define TCP7_RTO_MIN         200   /* ms */
#define TCP7_RTO_MAX         60000 /* ms */
#define TCP7_RTO_INITIAL     1000  /* ms */
#define TCP7_MAX_RETRANSMITS 15

extern tcp7_tcb_t g_tcp7_tcbs[TCP7_MAX_CONNECTIONS];

/* API */
int  tcp7_connect(int sock_fd, uint32_t dst_ip, uint16_t dst_port);
int  tcp7_accept(int listen_fd, uint32_t *src_ip, uint16_t *src_port);
int  tcp7_send(int sock_fd, const void *buf, size_t len);
int  tcp7_recv(int sock_fd, void *buf, size_t len);
int  tcp7_close(int sock_fd);
void tcp7_tick(void);  /* called periodically to handle timers */
void tcp7_incoming_segment(int sock_fd, const uint8_t *ip_pkt, size_t len);
int  tcp7_get_state(int sock_fd);
uint32_t tcp7_get_rtt(int sock_fd);

/* TLS 1.2 implementation */
#define TLS7_MAX_SESSIONS  32
#define TLS7_MAX_FRAG      16384
#define TLS7_MAX_CERTS     16
#define TLS7_MAX_CA        8
#define TLS7_RSA_MAX_BYTES 512

/* TLS record types */
#define TLS7_CHANGE_CIPHER_SPEC 20
#define TLS7_ALERT              21
#define TLS7_HANDSHAKE          22
#define TLS7_APPLICATION_DATA   23

/* TLS handshake types */
#define TLS7_HS_CLIENT_HELLO       1
#define TLS7_HS_SERVER_HELLO       2
#define TLS7_HS_CERTIFICATE        11
#define TLS7_HS_SERVER_KEY_EXCH    12
#define TLS7_HS_SERVER_HELLO_DONE  14
#define TLS7_HS_CLIENT_KEY_EXCH    16
#define TLS7_HS_FINISHED           20

/* TLS version */
#define TLS7_VERSION_12  0x0303

/* Cipher suites we support */
#define TLS7_RSA_WITH_AES_128_CBC_SHA256  0x003C
#define TLS7_RSA_WITH_AES_256_CBC_SHA256  0x003D
#define TLS7_ECDHE_RSA_WITH_AES_128_GCM_SHA256 0xC02F

/* Verification policy */
#define TLS7_VERIFY_NONE      0
#define TLS7_VERIFY_CHAIN     1
#define TLS7_VERIFY_HOSTNAME  2
#define TLS7_VERIFY_STRICT    (TLS7_VERIFY_CHAIN|TLS7_VERIFY_HOSTNAME)

/* TLS session state */
typedef enum {
    TLS7_NONE = 0,
    TLS7_CLIENT_HELLO_SENT,
    TLS7_SERVER_HELLO_RECV,
    TLS7_CERTIFICATE_RECV,
    TLS7_KEY_EXCH_RECV,
    TLS7_HELLO_DONE_RECV,
    TLS7_CLIENT_KEY_EXCH_SENT,
    TLS7_CHANGE_CIPHER_SENT,
    TLS7_FINISHED_SENT,
    TLS7_CHANGE_CIPHER_RECV,
    TLS7_FINISHED_RECV,
    TLS7_ESTABLISHED,
    TLS7_CLOSED
} tls7_state_t;

/* AES context (software, no SSE) */
typedef struct {
    uint32_t rk[60];  /* round keys */
    int      nr;      /* number of rounds */
} aes7_ctx_t;

/* SHA-256 context */
typedef struct {
    uint32_t state[8];
    uint64_t count;
    uint8_t  buf[64];
} sha256_ctx_t;

/* HMAC-SHA256 context */
typedef struct {
    sha256_ctx_t inner, outer;
} hmac_sha256_ctx_t;

/* X.509 certificate (parsed from DER) */
typedef struct {
    uint8_t  issuer[128];
    uint8_t  subject[128];
    uint8_t  serial[20];
    uint64_t not_before;
    uint64_t not_after;
    uint8_t  pubkey_type; /* 0=RSA, 1=EC */
    /* Packed RSA key blob: be16(mod_len), be16(exp_len), mod||exp */
    uint8_t  pubkey_data[512];
    uint32_t pubkey_len;
    uint8_t  sig_type;    /* 0=unknown, 1=sha256WithRSA */
    uint8_t  sig_data[512];
    uint32_t sig_len;
    uint8_t  tbs_hash[32]; /* SHA-256(TBSCertificate DER) */
    bool     valid;
} x509_cert_t;

/* TLS session */
typedef struct {
    bool          used;
    int           sock_fd;
    tls7_state_t  state;

    /* Protocol version */
    uint16_t      version;
    uint16_t      cipher_suite;

    /* Client random / server random */
    uint8_t       client_random[32];
    uint8_t       server_random[32];

    /* Pre-master secret and master secret */
    uint8_t       pre_master_secret[48];
    uint8_t       master_secret[48];

    /* Key material */
    uint8_t       client_write_key[32];
    uint8_t       server_write_key[32];
    uint8_t       client_write_iv[16];
    uint8_t       server_write_iv[16];
    uint8_t       client_write_mac_key[32];
    uint8_t       server_write_mac_key[32];

    /* AES contexts */
    aes7_ctx_t    client_enc, client_dec;
    aes7_ctx_t    server_enc, server_dec;

    /* Sequence numbers */
    uint64_t      client_seq, server_seq;

    /* SNI */
    char          sni_hostname[128];

    /* ALPN */
    char          alpn_protocol[32];

    /* Handshake hash (for Finished) */
    sha256_ctx_t  handshake_hash;

    /* Server certificate chain */
    x509_cert_t   certs[TLS7_MAX_CERTS];
    int           cert_count;
    int           verify_mode;
    bool          cert_chain_ok;
    bool          hostname_ok;

    /* Receive buffer for reassembly */
    uint8_t       recv_buf[TLS7_MAX_FRAG + 5];
    uint32_t      recv_len;
    uint8_t       app_buf[TLS7_MAX_FRAG];
    uint32_t      app_len;
} tls7_session_t;

extern tls7_session_t g_tls7_sessions[TLS7_MAX_SESSIONS];

/* CA store */
typedef struct {
    uint8_t  subject[128];
    uint8_t  pubkey_data[512];
    uint32_t pubkey_len;
    uint8_t  pubkey_type;
    bool     trusted;
} ca7_entry_t;

extern ca7_entry_t g_ca7_store[TLS7_MAX_CA];
extern int         g_ca7_count;

/* API */
int  tls7_connect(int sock_fd, const char *hostname, const char *alpn);
int  tls7_send(int sock_fd, const void *buf, size_t len);
int  tls7_recv(int sock_fd, void *buf, size_t len);
void tls7_close(int sock_fd);
int  tls7_get_state(int sock_fd);

/* Crypto primitives */
void aes7_encrypt(aes7_ctx_t *ctx, const uint8_t in[16], uint8_t out[16]);
void aes7_decrypt(aes7_ctx_t *ctx, const uint8_t in[16], uint8_t out[16]);
void aes7_cbc_encrypt(aes7_ctx_t *ctx, const uint8_t *iv, const uint8_t *in, uint8_t *out, size_t len);
void aes7_cbc_decrypt(aes7_ctx_t *ctx, const uint8_t *iv, const uint8_t *in, uint8_t *out, size_t len);
void aes7_key_setup(aes7_ctx_t *ctx, const uint8_t *key, int bits);

void sha256_init(sha256_ctx_t *ctx);
void sha256_update(sha256_ctx_t *ctx, const void *data, size_t len);
void sha256_final(sha256_ctx_t *ctx, uint8_t out[32]);
void sha256(const void *data, size_t len, uint8_t out[32]);

void hmac_sha256_init(hmac_sha256_ctx_t *ctx, const uint8_t *key, size_t keylen);
void hmac_sha256_update(hmac_sha256_ctx_t *ctx, const void *data, size_t len);
void hmac_sha256_final(hmac_sha256_ctx_t *ctx, uint8_t out[32]);

void tls7_prf(const uint8_t *secret, size_t slen, const char *label,
              const uint8_t *seed, size_t seedlen, uint8_t *out, size_t outlen);

/* X.509 / CA */
int  x509_parse_der(const uint8_t *der, size_t len, x509_cert_t *out);
int  ca7_load(const uint8_t *der, size_t len);
bool ca7_verify_cert_chain(const x509_cert_t *chain, int count);

/* X11 protocol implementation */
#define X11_MAX_CONN     8
#define X11_MAX_WINDOWS  64
#define X11_MAX_GC       16
#define X11_MAX_PROPS    64
#define X11_MAX_EVENTS   256
#define X11_MAX_ATOM     256
#define X11_MAX_SHMSEG   32
#define X11_MAX_PICTURE  64
#define X11_MAX_PIXMAP   128

/* X11 request opcodes */
#ifdef X11_CreateGC
#undef X11_CreateGC
#endif
#define X11_CreateWindow            1
#define X11_ChangeWindowAttributes  2
#define X11_GetWindowAttributes     3
#define X11_DestroyWindow           4
#define X11_GetGeometry             14
#define X11_QueryTree               15
#define X11_CreateGC                55
#define X11_ChangeGC                56
#define X11_SetClipRectangles       59
#define X11_FreeGC                  60
#define X11_MapWindow               8
#define X11_UnmapWindow             10
#define X11_PutImage                72
#define X11_GetImage                73
#define X11_PolyFillRectangle       0x3F
#define X11_CopyArea                62
#define X11_PolyText8               74
#define X11_ImageText8              76
#define X11_CreatePixmap            53
#define X11_FreePixmap              54
#define X11_MapSubwindows           9
#define X11_ReparentWindow          7
#define X11_ConfigureWindow         12
#define X11_GrabKeyboard            33
#define X11_GrabPointer             26
#define X11_QueryPointer            38
#define X11_TranslateCoordinates    40
#define X11_InternAtom              16
#define X11_GetAtomName             17
#define X11_ChangeProperty          18
#define X11_DeleteProperty          19
#define X11_GetProperty             20
#define X11_SetSelectionOwner       22
#define X11_GetSelectionOwner       23
#define X11_ConvertSelection        24
#define X11_SendEvent               25
#define X11_SetInputFocus           42
#define X11_GetInputFocus           43
#define X11_OpenFont                45
#define X11_WarpPointer             41
#define X11_GrabServer              36
#define X11_UngrabServer            37
#define X11_QueryExtension          98
#define X11_ListExtensions          99
#define X11_GetKeyboardMapping      101
#define X11_SetPointerMapping       116
#define X11_GetPointerMapping       117
#define X11_SetModifierMapping      118
#define X11_GetModifierMapping      119
#define X11_Flush                   0x5E
#define X11_Sync                    0x47

/* X11 event types */
#define X11_KeyPress         2
#define X11_KeyRelease       3
#define X11_ButtonPress      4
#define X11_ButtonRelease    5
#define X11_MotionNotify     6
#define X11_EnterNotify      7
#define X11_LeaveNotify      8
#define X11_FocusIn          9
#define X11_FocusOut         10
#define X11_Expose           12
#define X11_VisibilityNotify 15
#define X11_CreateNotify     16
#define X11_DestroyNotify    17
#define X11_MapNotify        19
#define X11_UnmapNotify      18
#define X11_ReparentNotify   21
#define X11_ConfigureNotify  22
#define X11_ClientMessage    33
#define X11_SelectionClear   29
#define X11_SelectionRequest 30
#define X11_SelectionNotify  31
#define X11_PropertyNotify   28

/* X11 window */
typedef struct {
    bool     used;
    uint32_t id;
    uint32_t parent;
    int      x, y;
    int      width, height;
    int      border_width;
    uint8_t  depth;
    uint16_t klass;
    uint32_t visual;
    uint32_t background_pixel;
    uint32_t border_pixel;
    bool     mapped;
    bool     visible;
    uint32_t event_mask;
    uint32_t gc;       /* current GC */
    uint8_t *backing;  /* backing store (framebuffer) */
    uint32_t backing_size;
} x11_window_t;

/* X11 pixmap */
typedef struct {
    bool     used;
    uint32_t id;
    uint32_t drawable; /* parent/root drawable */
    uint16_t depth;
    int      width, height;
    uint8_t *backing;  /* ARGB32 backing store */
    uint32_t backing_size;
} x11_pixmap_t;

/* X11 graphic context */
typedef struct {
    bool     used;
    uint32_t id;
    uint32_t drawable;
    uint32_t fg_pixel;
    uint32_t bg_pixel;
    int      function;  /* raster op */
    int      line_width;
    int      line_style;
    int      fill_style;
    int      fill_rule;
    uint32_t font;
} x11_gc_t;

/* X11 atom */
typedef struct {
    bool     used;
    uint32_t id;
    char     name[64];
} x11_atom_t;

typedef struct {
    bool     used;
    uint32_t window;
    uint32_t atom;
    uint32_t type;
    uint8_t  format;      /* 8,16,32 */
    uint8_t *data;
    uint32_t data_bytes;
} x11_property_t;

typedef struct {
    bool     used;
    uint32_t id;       /* XID of shmseg */
    int      shmid;    /* SysV shm id */
    bool     read_only;
} x11_shmseg_t;

typedef struct {
    bool     used;
    uint32_t id;       /* X Render Picture XID */
    uint32_t drawable; /* Window/Pixmap XID */
    uint32_t format;
} x11_picture_t;

/* X11 event (32 bytes, X11 protocol) */
typedef struct {
    uint8_t  type;
    uint8_t  detail;
    uint16_t sequence;
    uint32_t pad[7]; /* 28 bytes of event-specific data */
} x11_event_t;

/* X11 connection */
typedef struct {
    bool     used;
    int      sock_fd;
    uint16_t endian;     /* 0x0100=MSB, 0x0001=LSB */
    uint16_t proto_major;
    uint16_t proto_minor;
    uint32_t next_id;    /* XID allocator */
    uint32_t resource_base;
    uint32_t root_window;
    uint32_t visual;
    uint8_t  depth;
    uint32_t white_pixel;
    uint32_t black_pixel;
    uint16_t screen_width;
    uint16_t screen_height;
    uint16_t sequence;
    uint8_t  ext_opcode_shm;
    uint8_t  ext_opcode_render;
    uint8_t  ext_opcode_xfixes;
    uint8_t  ext_opcode_randr;
    uint8_t  ext_opcode_xinput;
    uint8_t  ext_opcode_glx;
    uint8_t  ext_opcode_bigreq;
    uint8_t  ext_opcode_xge;
    uint8_t  ext_opcode_composite;
    uint8_t  ext_opcode_damage;
    uint8_t  ext_opcode_shape;
    uint8_t  ext_opcode_xkeyboard;
    uint8_t  ext_opcode_sync;

    /* Auth */
    char     auth_name[16];
    char     auth_data[64];
    int      auth_name_len;
    int      auth_data_len;

    /* Windows/GCs/Atoms/Props */
    x11_window_t windows[X11_MAX_WINDOWS];
    x11_pixmap_t pixmaps[X11_MAX_PIXMAP];
    x11_gc_t     gcs[X11_MAX_GC];
    x11_atom_t   atoms[X11_MAX_ATOM];
    x11_property_t props[X11_MAX_PROPS];
    x11_shmseg_t shmsegs[X11_MAX_SHMSEG];
    x11_picture_t pictures[X11_MAX_PICTURE];
    int          window_count;
    int          pixmap_count;
    int          gc_count;
    int          atom_count;
    int          prop_count;
    int          shmseg_count;
    int          picture_count;

    int          pointer_x;
    int          pointer_y;

    /* Minimal XFixes selection state used by GTK/Firefox compositor probing. */
    uint32_t     xfixes_cm_window;
    uint32_t     xfixes_cm_selection;
    bool         xfixes_cm_notified;

    /* Stream request assembly. X11 over SOCK_STREAM can split one
     * protocol request across several writes, especially PutImage. */
    uint8_t     *req_buf;
    uint32_t     req_len;
    uint32_t     req_cap;

    /* Event queue */
    x11_event_t  events[X11_MAX_EVENTS];
    int          event_head, event_tail;
    int          event_count;
} x11_connection_t;

extern x11_connection_t g_x11_conns[X11_MAX_CONN];

/* API */
int  x11_accept_connection(int listen_sock_fd);
int  x11_attach_socket(int sock_fd);      /* attach protocol server to an internal socket */
int  x11_detach_socket(int sock_fd);
int  x11_process_request(int conn_idx);
int  x11_process_socket(int sock_fd);     /* process one request from socket-bound connection */
void x11_push_key_event(int conn_idx, uint8_t keycode, bool press);
void x11_push_mouse_event(int conn_idx, int x, int y, int button, bool press);
int  x11_dispatch_pointer_event(int screen_x, int screen_y, int button, bool press);
int  x11_dispatch_key_event(uint8_t keycode, bool press);
void x11_push_expose(int conn_idx, uint32_t wid, int x, int y, int w, int h);
void x11_push_client_message(int conn_idx, uint32_t wid, uint32_t atom, uint32_t data);
void x11_render_now(void);
void x11_tick(void);
int  x11_get_conn_for_window(uint32_t wid);

/* Wayland compositor bridge */
#define WL7_MAX_CLIENTS     32
#define WL7_MAX_SURFACES    128
#define WL7_MAX_BUFFERS     64
#define WL7_MAX_GLOBALS     16

/* Wayland object IDs */
#define WL7_DISPLAY_ID      1
#define WL7_REGISTRY_ID     2
#define WL7_COMPOSITOR_ID   3
#define WL7_SHM_ID          4
#define WL7_SHELL_ID        5
#define WL7_SEAT_ID         6
#define WL7_OUTPUT_ID       7
#define WL7_XDG_WM_BASE_ID  8
#define WL7_VIEWPORTER_ID   9
#define WL7_DMABUF_ID      10
#define WL7_PRESENTATION_ID 11

/* Wayland interface names */
#define WL7_IFACE_DISPLAY    "wl_display"
#define WL7_IFACE_REGISTRY   "wl_registry"
#define WL7_IFACE_COMPOSITOR "wl_compositor"
#define WL7_IFACE_SHM        "wl_shm"
#define WL7_IFACE_SHELL      "wl_shell"
#define WL7_IFACE_SEAT       "wl_seat"
#define WL7_IFACE_OUTPUT     "wl_output"
#define WL7_IFACE_XDG_WM_BASE "xdg_wm_base"
#define WL7_IFACE_VIEWPORTER "wp_viewporter"
#define WL7_IFACE_DMABUF "zwp_linux_dmabuf_v1"
#define WL7_IFACE_PRESENTATION "wp_presentation"

/* Wayland buffer (shared memory) */
typedef struct {
    bool     used;
    uint32_t id;
    int      fd;       /* memfd */
    uint32_t width, height;
    uint32_t stride;
    uint32_t format;   /* ARGB8888=0 */
    uint8_t *data;     /* mapped pointer */
    uint32_t data_size;
} wl7_buffer_t;

/* Wayland surface */
typedef struct {
    bool     used;
    uint32_t id;
    int      client_idx;
    uint32_t buffer_id;
    int      x, y;
    uint32_t attached_buffer;
    bool     mapped;
    uint32_t frame_callback_id;
} wl7_surface_t;

/* Wayland global */
typedef struct {
    uint32_t id;
    const char *interface;
    uint32_t version;
} wl7_global_t;

/* Wayland client connection */
typedef struct {
    bool     used;
    int      sock_fd;
    uint32_t next_id;  /* client-side ID allocator */
    int      surface_count;
    uint32_t surfaces[WL7_MAX_SURFACES];
    uint32_t keyboard_focus;
    uint32_t pointer_focus;
} wl7_client_t;

extern wl7_client_t  g_wl7_clients[WL7_MAX_CLIENTS];
extern wl7_surface_t g_wl7_surfaces[WL7_MAX_SURFACES];
extern wl7_buffer_t  g_wl7_buffers[WL7_MAX_BUFFERS];
extern wl7_global_t  g_wl7_globals[WL7_MAX_GLOBALS];
extern int           g_wl7_global_count;

/* API */
int  wl7_accept_client(int listen_sock_fd);
int  wl7_attach_socket(int sock_fd);      /* attach compositor protocol to an internal socket */
int  wl7_detach_socket(int sock_fd);      /* detach and cleanup protocol state for a socket */
int  wl7_process_message(int client_idx);
int  wl7_process_socket(int sock_fd);     /* process one message from socket-bound client */
void wl7_render_surfaces(void);  /* composite all mapped surfaces to fb */
void wl7_push_keyboard_event(int client_idx, uint32_t key, uint32_t state);
void wl7_push_pointer_event(int client_idx, int x, int y, uint32_t button, uint32_t state);
int  wl7_dispatch_pointer_event(int screen_x, int screen_y, uint32_t button, uint32_t state);
int  wl7_dispatch_keyboard_event(uint32_t key, uint32_t state);
void wl7_tick(void);

/* Shell commands + init */
void compat7_init_all(void);
void compat7_register_shell_cmds(void);
void compat7_tick_all(void);

#endif /* RIDUX_COMPAT7_H */
