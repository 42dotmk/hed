#include "lsp_servers.h"
#include <string.h>

/* C99 compound literals at file scope have static storage duration, so
 * inlining them here is just as safe as a named static array — but the
 * registry stays one row per server, easy to read and edit. */
#define ARGV(...)  (const char *const[]){ __VA_ARGS__, NULL }
#define ROOTS(...) (const char *const[]){ __VA_ARGS__, NULL }

static const LspServerDef SERVERS[] = {
    { "c",          ARGV("clangd"), ROOTS("compile_commands.json", ".clangd", "Makefile", ".git") },
    { "cpp",        ARGV("clangd"), ROOTS("compile_commands.json", ".clangd", "Makefile", ".git") },
    { "rust",       ARGV("rust-analyzer"), ROOTS("Cargo.toml", ".git") },
    { "python",     ARGV("pyright-langserver", "--stdio"), ROOTS("pyproject.toml", "setup.py", "requirements.txt", ".git") },
    { "go",         ARGV("gopls"), ROOTS("go.mod", ".git") },
    { "typescript", ARGV("typescript-language-server", "--stdio"), ROOTS("tsconfig.json", "package.json", ".git") },
    { "javascript", ARGV("typescript-language-server", "--stdio"), ROOTS("tsconfig.json", "package.json", ".git") },
    { "lua",        ARGV("lua-language-server"), ROOTS(".luarc.json", ".git") },
    { "zig",        ARGV("zls"), ROOTS("build.zig", ".git") },
};

const LspServerDef *lsp_servers_lookup(const char *lang) {
    if (!lang) return NULL;
    int n = (int)(sizeof(SERVERS) / sizeof(SERVERS[0]));
    for (int i = 0; i < n; i++) {
        if (strcmp(SERVERS[i].lang, lang) == 0) return &SERVERS[i];
    }
    return NULL;
}
