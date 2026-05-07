/* Internal header shared between copilot.c and copilot_proto.c.
 * Not part of the plugin's public API. */

#ifndef HED_PLUGIN_COPILOT_INTERNAL_H
#define HED_PLUGIN_COPILOT_INTERNAL_H

#include "lsp/cjson/cJSON.h"

#define CP_READ_BUF_SIZE  65536
#define CP_PENDING_MAX    32
#define CP_DEBOUNCE_MS    250

typedef enum {
    CP_REQ_NONE = 0,
    CP_REQ_INITIALIZE,
    CP_REQ_SIGN_IN_INITIATE,
    CP_REQ_SIGN_IN_CONFIRM,
    CP_REQ_CHECK_STATUS,
    CP_REQ_SIGN_OUT,
    CP_REQ_GET_COMPLETIONS,
} CpReqKind;

typedef struct {
    int       id;
    CpReqKind kind;
} CpPending;

typedef struct {
    /* Child process */
    int   pid;
    int   to_fd;     /* editor writes here -> child stdin */
    int   from_fd;   /* editor reads here  <- child stdout */

    /* Handshake state */
    int   spawned;
    int   initialized;
    int   next_id;

    /* Receive framing */
    char  read_buf[CP_READ_BUF_SIZE];
    int   read_buf_len;
    int   content_length;     /* -1 while waiting for header */
    char *msg_body;
    int   msg_body_len;

    /* Pending request -> kind table */
    CpPending pending[CP_PENDING_MAX];

    /* Document sync */
    int   doc_version;        /* monotonically increasing across all docs */

    /* Auth */
    char  user_code[64];      /* set by signInInitiate response */
    char  user_login[128];    /* set by checkStatus / signInConfirm */
    int   signed_in;

    /* Suggestion state */
    int    vt_ns;
    char  *suggestion_text;   /* full inserted text for the active alt */
    char  *suggestion_display;/* displayText for the active alt */
    char  *suggestion_uuid;   /* uuid for telemetry on the active alt */
    int    suggestion_line;
    int    suggestion_col;
    int    has_suggestion;

    /* Alternatives. alts[] is owned (each entry's strings are heap).
     * alts_active is the index currently rendered as ghost text. */
    struct CpAlt {
        char *text;
        char *display;
        char *uuid;
    } *alts;
    int    alts_count;
    int    alts_active;

    /* Last sent did-change version per buffer (kept simple: just the
     * buffer pointer + version we last sent for it). v1 only tracks
     * the most recently changed buffer. */
    void  *last_buf;
    int    last_buf_opened;   /* did we send didOpen for last_buf yet? */
} Copilot;

extern Copilot CP;

/* --- proto layer (copilot_proto.c) ---------------------------------- */

/* Spawn the language server child. Returns 0 on success, -1 on failure
 * (with status message set). Caller must not have one already running. */
int  cp_proto_spawn(void);

/* Send a JSON-RPC request. params is consumed (added to the message
 * and freed). Records the kind in the pending table. Returns the id. */
int  cp_proto_request(const char *method, cJSON *params, CpReqKind kind);

/* Send a JSON-RPC notification. params is consumed. */
void cp_proto_notify(const char *method, cJSON *params);

/* Pop the kind for a given id. Returns CP_REQ_NONE if unknown. */
CpReqKind cp_proto_pending_pop(int id);

/* Stop the child and tear down the read fd. Idempotent. */
void cp_proto_shutdown(void);

/* Dispatch entrypoint — called by the proto layer on each fully-framed
 * incoming message. Implemented in copilot.c. */
void cp_handle_message(const char *json, int len);

#endif
