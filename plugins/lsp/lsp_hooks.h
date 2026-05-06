#ifndef LSP_HOOKS_H
#define LSP_HOOKS_H

#include "ui/window.h"

/* Initialize LSP buffer lifecycle hooks */
void lsp_hooks_init(void);

/* Register an LSP-owned popup modal. The keypress hook only handles
 * (q/Esc/j/k) the modal it was told about, so it can't accidentally
 * tear down popups owned by other plugins (e.g. selectlist). */
void lsp_popup_track(Window *modal);

#endif /* LSP_HOOKS_H */
