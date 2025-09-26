# Contracts: Custom Events

## Downstream Flush Event
- **Name**: `prerecord-flush`
- **Direction**: Custom downstream event sent to element sink pad.
- **Structure Fields**: *(none required initially)*
- **Semantics**: Initiates draining sequence. If already draining, event is ignored.
- **State Transition**: BUFFERING -> PASS_THROUGH (after queued GOPs flushed)

## Upstream Re-Arm Event
- **Name**: `prerecord-arm`
- **Direction**: Custom upstream (application inject via element pad/event API) delivered to sink pad.
- **Structure Fields**: *(none required initially)*
- **Preconditions**: Mode == PASS_THROUGH and not draining.
- **Semantics**: Re-enters BUFFERING mode; resets timing baseline; does not retroactively buffer frames already forwarded.

## EOS Handling Policy
- **Property**: `flush-on-eos` (enum: auto | always | never)
- **AUTO**: Flush only if mode == PASS_THROUGH (i.e., a prior flush occurred) then forward EOS.
- **ALWAYS**: Force flush (drain queue) regardless of mode; if in BUFFERING flush then EOS.
- **NEVER**: Forward EOS immediately; discard queued pre-event data.

## Error Conditions
| Condition | Handling |
|-----------|----------|
| Flush during flush | Ignore (debug log) |
| Re-arm during BUFFERING | Ignore (debug log) |
| Arm while draining | Queue ignored; user must retry post-drain |
| Unknown custom event | GST_EVENT_UNKNOWN handling path (log) |

## Versioning
- Initial event contract version: 1.0
- Backward incompatible changes (renaming events, changing semantics) require MAJOR version bump in plugin metadata.
