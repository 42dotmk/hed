#ifndef CMD_LSP_H
#define CMD_LSP_H

/* LSP command callbacks */
void cmd_lsp_start(const char *args);
void cmd_lsp_stop(const char *args);
void cmd_lsp_hover(const char *args);
void cmd_lsp_definition(const char *args);
void cmd_lsp_completion(const char *args);

#endif /* CMD_LSP_H */
