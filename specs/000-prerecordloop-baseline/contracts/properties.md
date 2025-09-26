# Contracts: Properties

| Property | Type | Range | Default | Behavior | Notes |
|----------|------|-------|---------|----------|-------|
| max-time | guint (seconds) | 1..3600 | 10 | Time capacity soft ceiling; adaptive 2-GOP floor | Setting 0 invalid (reject) |
| flush-on-eos | enum { auto, always, never } | fixed | auto | EOS flushing policy | Auto flush only in PASS_THROUGH |
| silent | gboolean | {TRUE,FALSE} | FALSE | Suppress info/info-level logs | Does not suppress warnings/errors |

## Validation Rules
- `max-time == 0` → return FALSE in set_property; emit warning.
- Changing `max-time` while BUFFERING: new limit affects subsequent pruning decisions immediately.
- Changing `flush-on-eos` while draining flush: apply after drain completes.

## Introspection
GTK-Doc MUST document each property with: Nick, Blurb, Detailed description (include adaptive floor rule for `max-time`).
