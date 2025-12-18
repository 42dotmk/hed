#include "fold_methods.h"
#include "fold.h"
#include "buf/buffer.h"

void fold_apply_method(Buffer *buf, FoldMethod method) {
    if (!buf)
        return;

    switch (method) {
    case FOLD_METHOD_MANUAL:
        /* Manual mode - do nothing, keep existing folds */
        break;

    case FOLD_METHOD_BRACKET:
        fold_detect_brackets(buf);
        break;

    case FOLD_METHOD_INDENT:
        fold_detect_indent(buf);
        break;

    default:
        /* Unknown method - do nothing */
        break;
    }
}
