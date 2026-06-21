#ifndef HED_PLUGIN_MARKDOWN_INTERNAL_H
#define HED_PLUGIN_MARKDOWN_INTERNAL_H

/* Plugin-private wiring shared between markdown.c and the per-feature
 * implementations (highlight, fold). Each feature lives in its own
 * translation unit so the descriptor file stays tiny. */

void md_init_highlights(void);
void md_init_fold(void);

#endif
