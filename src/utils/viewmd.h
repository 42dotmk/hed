#ifndef VIEWMD_H
#define VIEWMD_H

/*
 * VIEWMD INTEGRATION
 * ==================
 *
 * Live markdown preview using the external `viewmd` tool.
 *
 * viewmd prints "VIEWMD_SOCKET=/tmp/viewmd-<pid>.sock" to stdout on startup.
 * We spawn it, capture the socket path, then push buffer content to that
 * socket whenever the buffer changes. viewmd re-renders on each connection
 * close.
 *
 * Usage:
 *   :viewmd   — launch preview for the current buffer (toggle off if active)
 *   <space>rp — same, bound in normal mode
 *
 * The preview tracks the buffer it was opened for. Switching buffers stops
 * auto-push; use the command again on the new buffer.
 */

/* Initialize hooks. Call once during editor startup. */
void viewmd_init(void);

/* Command callback: launch/stop viewmd for the current buffer. */
void cmd_viewmd_preview(const char *args);

#endif /* VIEWMD_H */
