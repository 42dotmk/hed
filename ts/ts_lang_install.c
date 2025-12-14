#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>

/*
 * Tree-sitter language installer for hed.
 *
 * Usage:
 *   ./ts_lang_install <lang>
 *
 * Example:
 *   ./ts_lang_install c
 *
 * This will:
 *   - Clone https://github.com/tree-sitter/tree-sitter-<lang>.git
 *     into ./ts/build/<lang>
 *   - Build <lang>.so from parser.c (+ scanner.c if present)
 *   - Copy the .so to ./ts/<lang>.so
 *   - Copy queries/highlights.scm (if present) to
 *       ./ts-langs/queries/<lang>/highlights.scm
 *
 * Run this from the hed repo root so that ts-langs/ and queries/
 * are created in the place hed expects.
 */

static int file_exists(const char *path) {
    struct stat st;
    return stat(path, &st) == 0;
}

static int mkdir_if_needed(const char *path) {
    struct stat st;
    if (stat(path, &st) == 0) {
        if (S_ISDIR(st.st_mode)) return 0;
        fprintf(stderr, "Path exists but is not a directory: %s\n", path);
        return -1;
    }
    if (mkdir(path, 0777) != 0) {
        perror("mkdir");
        fprintf(stderr, "Failed to create directory: %s\n", path);
        return -1;
    }
    return 0;
}

static int run_cmd(const char *cmd) {
    int rc = system(cmd);
    if (rc != 0) {
        fprintf(stderr, "Command failed (%d): %s\n", rc, cmd);
        return -1;
    }
    return 0;
}

static int copy_file(const char *src, const char *dst) {
    FILE *in = fopen(src, "rb");
    if (!in) {
        perror("fopen src");
        fprintf(stderr, "Failed to open %s\n", src);
        return -1;
    }
    FILE *out = fopen(dst, "wb");
    if (!out) {
        perror("fopen dst");
        fprintf(stderr, "Failed to open %s\n", dst);
        fclose(in);
        return -1;
    }

    char buf[4096];
    size_t n;
    while ((n = fread(buf, 1, sizeof(buf), in)) > 0) {
        if (fwrite(buf, 1, n, out) != n) {
            perror("fwrite");
            fclose(in);
            fclose(out);
            return -1;
        }
    }

    fclose(in);
    fclose(out);
    return 0;
}

void print_usage(const char *prog_name) {
    fprintf(stderr, "Usage: %s <lang>\n", prog_name);
    fprintf(stderr, "Example: %s c\n", prog_name);
}
void print_help(const char *prog_name) {
    print_usage(prog_name);
    fprintf(stderr, "\n");
    fprintf(stderr, "Installs Tree-sitter language parser for <lang>.\n");
    fprintf(stderr, "Clones the grammar from GitHub, builds the parser,\n");
    fprintf(stderr, "and installs it into ts-langs/ directory.\n");
    fprintf(stderr, "\n");
    fprintf(stderr, "Options:\n");
    fprintf(stderr, "  --help       Print this help message\n");
    fprintf(stderr, "  --url <url>  Specify custom git repository URL\n");
}
void parse_args(int argc, char **argv, const char **lang, const char **custom_url) {
    for (int i = 1; i < argc; i++) {
        if (i == 1 && argv[i][0] != '-') {
            *lang = argv[i];
            continue;
        }
        if (strcmp(argv[i], "--help") == 0) {
            print_help(argv[0]);
            exit(0);
        } else if (strcmp(argv[i], "--url") == 0) {
            if (i + 1 < argc) {
                *custom_url = argv[i + 1];
                i++;
            } else {
                fprintf(stderr, "--url requires an argument\n");
                exit(1);
            }
        } else {
            fprintf(stderr, "Unknown argument: %s\n", argv[i]);
            exit(1);
        }
    }
}
/* Main function 
 * argv[1]: language name
 * Optional arguments:
 * --help : print usage
 * --url <url> : specify custom git repository URL
 */
int main(int argc, char **argv) {
    if (argc < 2) {
        print_usage(argv[0]);
    }
    const char *lang=NULL;  // Default to C language
    const char *custom_url = NULL;
    parse_args(argc, argv, &lang, &custom_url);

    if (!lang[0]) {
        fprintf(stderr, "Invalid language name.\n");
        return 1;
    }

    char cwd[1024];
    if (!getcwd(cwd, sizeof(cwd))) {
        perror("getcwd");
        return 1;
    }

    /* Prepare build directory: ./ts/build/<lang> */
    if (mkdir_if_needed("ts") != 0) return 1;
    if (mkdir_if_needed("ts/build") != 0) return 1;

    char build_dir[1024];
    snprintf(build_dir, sizeof(build_dir), "ts/build/%s", lang);

    if (!file_exists(build_dir)) {
        char repo_url[512];
        char cmd[1024];
        int cloned = 0;

        if (custom_url) {
            snprintf(repo_url, sizeof(repo_url), "%s", custom_url);
        } else {
            snprintf(repo_url, sizeof(repo_url),
                     "https://github.com/tree-sitter-grammars/tree-sitter-%s.git",
                     lang);
        }
        snprintf(cmd, sizeof(cmd), "git clone --depth 1 %s %s",
                 repo_url, build_dir);
        fprintf(stderr, "Cloning %s into %s\n", repo_url, build_dir);
        if (run_cmd(cmd) == 0) {
            cloned = 1;
        } else if (!custom_url) {
            fprintf(stderr,
                    "First clone attempt failed, trying upstream tree-sitter org...\n");
            /* Fallback: original tree-sitter org */
            snprintf(repo_url, sizeof(repo_url),
                     "https://github.com/tree-sitter/tree-sitter-%s.git",
                     lang);
            snprintf(cmd, sizeof(cmd), "git clone --depth 1 %s %s",
                     repo_url, build_dir);
            fprintf(stderr, "Cloning %s into %s\n", repo_url, build_dir);
            if (run_cmd(cmd) == 0) {
                cloned = 1;
            }
        }

        if (!cloned) {
            fprintf(stderr,
                    "Failed to clone Tree-sitter grammar for '%s' from both sources.\n",
                    lang);
            return 1;
        }
    } else {
        fprintf(stderr, "Using existing build directory: %s\n", build_dir);
    }

    /* Change into build_dir to compile the parser. */
    if (chdir(build_dir) != 0) {
        perror("chdir build_dir");
        return 1;
    }

    /* Build parser.o */
    if (!file_exists("src/parser.c")) {
        fprintf(stderr, "Expected src/parser.c in %s\n", build_dir);
        return 1;
    }
    fprintf(stderr, "Compiling parser.c\n");
    if (run_cmd("cc -fPIC -I./src -c src/parser.c -o parser.o") != 0) {
        fprintf(stderr, "Failed to compile parser.c\n");
        return 1;
    }

    /* Optionally build scanner.o if src/scanner.c exists */
    int have_scanner = file_exists("src/scanner.c");
    if (have_scanner) {
        fprintf(stderr, "Compiling scanner.c\n");
        if (run_cmd("cc -fPIC -I./src -c src/scanner.c -o scanner.o") != 0) {
            fprintf(stderr, "Failed to compile scanner.c\n");
            return 1;
        }
    }

    /* Link shared object: <lang>.so */
    char so_name[64];
    snprintf(so_name, sizeof(so_name), "%s.so", lang);
    char link_cmd[256];
    if (have_scanner) {
        snprintf(link_cmd, sizeof(link_cmd),
                 "cc -shared -o %s parser.o scanner.o", so_name);
    } else {
        snprintf(link_cmd, sizeof(link_cmd),
                 "cc -shared -o %s parser.o", so_name);
    }
    fprintf(stderr, "Linking %s\n", so_name);
    if (run_cmd(link_cmd) != 0) {
        fprintf(stderr, "Failed to link %s\n", so_name);
        return 1;
    }

    /* Go back to repo root. */
    if (chdir(cwd) != 0) {
        perror("chdir back to cwd");
        return 1;
    }

    /* Ensure ts-langs/ exists and install the .so there. */
    if (mkdir_if_needed("ts-langs") != 0) return 1;

    char src_so[1024];
    snprintf(src_so, sizeof(src_so), "%s/%s", build_dir, so_name);
    char dst_so[1024];
    snprintf(dst_so, sizeof(dst_so), "ts-langs/%s.so", lang);

    fprintf(stderr, "Installing %s -> %s\n", src_so, dst_so);
    if (copy_file(src_so, dst_so) != 0) {
        fprintf(stderr, "Failed to copy %s to %s\n", src_so, dst_so);
        return 1;
    }

    /* Install queries/highlights.scm if present. */
    char src_q[1024];
    snprintf(src_q, sizeof(src_q), "%s/queries/highlights.scm", build_dir);
    if (file_exists(src_q)) {
        if (mkdir_if_needed("ts-langs/queries") != 0) return 1;
        char dst_q_dir[1024];
        snprintf(dst_q_dir, sizeof(dst_q_dir), "ts-langs/queries/%s", lang);
        if (mkdir_if_needed(dst_q_dir) != 0) return 1;

        char dst_q[1024];
        snprintf(dst_q, sizeof(dst_q), "%s/highlights.scm", dst_q_dir);
        fprintf(stderr, "Installing %s -> %s\n", src_q, dst_q);
        if (copy_file(src_q, dst_q) != 0) {
            fprintf(stderr, "Failed to copy %s to %s\n", src_q, dst_q);
            return 1;
        }
    } else {
        fprintf(stderr, "No queries/highlights.scm found for '%s' (syntax colors may be limited)\n", lang);
    }

    fprintf(stderr, "Done. Language '%s' installed.\n", lang);
    fprintf(stderr, "  Shared library: ts-langs/%s.so\n", lang);
    fprintf(stderr, "  Queries:        ts-langs/queries/%s/highlights.scm (if present)\n", lang);
    fprintf(stderr, "Remember to run hed from this directory and use :ts on / :tslang %s\n", lang);

    return 0;
}
