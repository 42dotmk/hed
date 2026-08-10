#ifndef HED_PLUGIN_TASKS_H
#define HED_PLUGIN_TASKS_H
#include "plugin.h"
extern const Plugin plugin_tasks;

/* Exclude files/directories from :task_agenda and :org-files. `glob`
 * uses gitignore semantics (it becomes `rg -g '!<glob>'`): a bare name
 * like "node_modules" skips that directory anywhere in the tree,
 * "notes/old" skips that subtree, "*.draft.md" skips matching files.
 * Call from config_user_init() for a persistent blacklist; the
 * :task_agenda_ignore command adds session-only entries. */
void task_agenda_ignore(const char *glob);

/* Root directory for the org tree. When set, :task_agenda scans it
 * (instead of the cwd) and :org-files picks files under it. NULL or ""
 * clears it back to cwd. Session-only variant: :task_org_root <path>. */
void task_org_root(const char *path);
#endif
