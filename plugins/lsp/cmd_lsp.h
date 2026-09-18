#ifndef CMD_LSP_H
#define CMD_LSP_H

void cmd_lsp_connect(const char *args);
void cmd_lsp_start(const char *args);
void cmd_lsp_disconnect(const char *args);
void cmd_lsp_status(const char *args);
void cmd_lsp_autostart(const char *args);
void cmd_lsp_hover(const char *args);
void cmd_lsp_definition(const char *args);
void cmd_lsp_diagnostics(const char *args);
void cmd_lsp_code_action(const char *args);
void cmd_lsp_rename(const char *args);
void cmd_lsp_format(const char *args);
void cmd_lsp_references(const char *args);
void cmd_lsp_toggle(const char *args);

#endif /* CMD_LSP_H */
