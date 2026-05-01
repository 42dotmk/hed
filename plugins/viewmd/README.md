# viewmd

`:viewmd` opens a live HTML preview of the current Markdown buffer in
your default browser. The preview reloads automatically as you save.

## Usage

```
:viewmd          # open the current buffer's preview
```

Default leader binding: `<space>rp`.

## How it works

The plugin starts a small local HTTP server bound to a random
loopback port, generates an HTML page from the buffer's contents on
each request, and points your browser at the URL. A
`HOOK_BUFFER_SAVE` handler triggers a soft reload via a long-polling
endpoint.

The server is per-buffer — switching to another buffer and running
`:viewmd` starts a second server. When the buffer is closed, its
server is torn down.

## Requirements

- A working browser-launch path (`xdg-open`, `open`, or `start`
  depending on platform).
- An available loopback port. The plugin asks the kernel for one;
  no configuration needed.

## Notes

- The preview is read-only — there's no edit-in-browser. Edit in
  hed; saved changes flow to the preview.
- If your buffer's filetype isn't `markdown`, the preview still
  works but the conversion is a best-effort plain-text wrap.
- Stop a preview by closing its browser tab — the server stays
  running, but its only purpose is to serve that tab. (Closing the
  buffer kills the server cleanly.)
