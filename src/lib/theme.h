#ifndef THEME_H
#define THEME_H

/*
 * Simple theme configuration for ANSI colors.
 *
 * Values are full SGR escape sequences so you can use
 *  - 8/16-color codes      (e.g. "\x1b[31m")
 *  - 256-color codes       (e.g. "\x1b[38;5;81m")
 *  - truecolor (24-bit)    (e.g. "\x1b[38;2;135;206;250m")
 *
 * Adjust these to match your terminal background/theme.
 */
#define RGBC(r, g, b) "\x1b[38;2;" #r ";" #g ";" #b "m"

#define COLOR_RESET "\x1b[0m"

/* Tree-sitter highlight colors (examples tuned for dark backgrounds)
 * Adjust to taste. You can freely change these to any SGR you like.
 */

/* Palette loosely based on Tokyo Night */

/* Strings and comments */
#define COLOR_STRING RGBC(158, 206, 106) /* #9ece6a */
#define COLOR_COMMENT RGBC(86, 95, 137)  /* #565f89 */

/* Identifiers and symbols */
#define COLOR_VARIABLE RGBC(192, 202, 245) /* #c0caf5 */
#define COLOR_CONSTANT RGBC(224, 175, 104) /* #e0af68 */
#define COLOR_NUMBER RGBC(255, 158, 100)   /* #ff9e64 */
#define COLOR_KEYWORD RGBC(187, 154, 247)  /* #bb9af7 */
#define COLOR_TYPE RGBC(42, 195, 222)      /* #2ac3de */
#define COLOR_FUNCTION RGBC(122, 162, 247) /* #7aa2f7 */
#define COLOR_PROPERTY RGBC(192, 202, 245) /* reuse fg for properties */
#define COLOR_LABEL RGBC(224, 108, 117)    /* ~#e06c75 */
#define COLOR_OPERATOR RGBC(144, 153, 174) /* #9099ae-ish */
#define COLOR_DELIMITER RGBC(65, 72, 104)  /* #414868 */

/* Special categories */
#define COLOR_BUILTIN RGBC(255, 158, 100)   /* builtins, macros */
#define COLOR_ATTRIBUTE RGBC(187, 154, 247) /* attributes/annotations */
#define COLOR_PUNCT COLOR_DELIMITER         /* alias for punctuation.* */

/* Diagnostics / messages */
#define COLOR_DIAG_ERROR RGBC(247, 118, 142) /* #f7768e */
#define COLOR_DIAG_WARN RGBC(224, 175, 104)  /* #e0af68 */
#define COLOR_DIAG_NOTE RGBC(122, 162, 247)  /* #7aa2f7 */

#endif /* THEME_H */
