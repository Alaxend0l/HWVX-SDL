# Save store review fixes (fix-saves)

For docs/xdl-format/README.md §11.3 (the store: Files / Write) and the import section (Preview and import).

**§11.3 Files**, add after `hwvx.damaged.json ...`:
`hwvx.before-import.json`, the save as it was before the last import (replaced only by the next import; not one of the rotated backups).

**§11.3 Write**, replace the sync and rotation sentences:
* the `.tmp` is written and synced (`fsync`; on macOS `fcntl(F_FULLFSYNC)`, falling back to `fsync`; `FlushFileBuffers`), and so is the folder after the rename.
* A write whose text is the same as the save's changes nothing: no rotation, so repeated autosaves don't push the backups out.
* Only a save the reader calls damaged (bad hash, won't parse, not a save, unreadable) is moved to `.damaged.json`. If it can't be checked (out of memory) it goes into the backups as usual; if its text can't be loaded just then, it is renamed to `.1` rather than copied.
* The read cache (keyed on the files' sizes and times) is dropped after every write and delete.

**Preview and import**, replace "(atomic, the save before kept as `hwvx.1.json`; ...)" with:
(atomic; the save before it kept as `hwvx.before-import.json` and in the backups; `imported_from` set from the file actually imported if the save had none). With one file picked (`onlySource`), `imported_from`, the log and a blank save's settings come from that file. A new save from a PC/PS2 file whose chosen car is a default car left out (cars 0-2) gets its first unlocked car instead (-1 if none).

**Loading a slot** (§11.3 Ids, after "a chosen car the data doesn't have reads as none"): a car or item outside [-1, count) reads as none (-1); the JSON reader itself stays lossless.

**JSON**: `JSON_MAX_DEPTH` (64) is the reader's nesting limit; the writer goes 16 deeper, so anything read can be written back.
