# libremarkablefile

English | [简体中文](README.zh-CN.md)

A C ABI static library for reading and writing the reMarkable document library directly, using Qt 6 Core and libarchive.

```sh
make
make check
# After sourcing an ARMv7 SDK environment:
make BUILD=build-armv7
```

The output is `build/libremarkablefile.a`. Include `remarkablefile.h`. Initialize the physical library directory with `rml_init()`. Paths are relative display paths; `rml_list()` enumerates entries and `rml_open()` returns a POSIX file descriptor. Upload with `O_CREAT | O_WRONLY`, then fsync/close and call `rml_finish(path, 1)`. Cancel with `rml_finish(path, 0)`; call `rml_abort_pending()` at session end. Failures return -1 and expose errno plus `rml_error()`. One process manages one library; the `rml_*` C ABI names are unchanged.

Uploads are staged in `.umtp-incoming`. PDF validation checks header/end markers, and EPUB validation checks its ZIP mimetype; neither is a complete document syntax check. Publication writes the original file, `.content`, then `.metadata`, using atomic metadata replacement. Existing documents cannot be overwritten, protecting annotations and page mappings. SIGKILL or power loss can leave unlisted staging files.

New documents start with V1 content and page count 0; xochitl handles import/opening. Rename and native move retain UUIDs and update versions/timestamps. Deletion changes metadata `parent` to `trash`; deleting a folder preserves its children's data.

Duplicate titles get a full UUID suffix. Existing titles with unsuitable filename characters are escaped; long titles are truncated and suffixed with their UUID. New/renamed titles must use ordinary filename characters and fit 170 UTF-8 bytes. PDF/EPUB suffix matching is case-insensitive. Directory operations rescan metadata, so large libraries can be slower.

Concurrent edits are checked against the metadata read before writing, but there is no atomic transaction with xochitl/cloud. Return to the tablet's home screen and avoid editing the same entry simultaneously. Without live refresh, xochitl may need reloading/restarting to display external changes.

`test.cpp` runs the same import/read/rename/move/trash and validation checks with English, Chinese and accented Unicode filenames. Test diagnostics and comments use English; Unicode literals are filesystem fixtures, not untranslated user interface text.

## TODO

- Support exporting handwritten notes.
