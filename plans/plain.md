# Plain: Omarchy's text editor for config files

## Why

Omarchy opens config files in `$EDITOR`, which is Neovim unless the user picks something else. That's right for people who live in Vim and a wall for everyone else: the first time someone picks "Edit Hyprland config" they land in modal editing and can't save or quit. The alternatives Omarchy offers (VSCode, Zed, Sublime) are whole IDEs to change one number.

Plain is the editor you don't have to learn: Ctrl+S saves, Ctrl+Z undoes, Ctrl+F finds, and the window closes like any other. It's built for small text files, config above all, and does nothing else.

It's built the way Hype, Monologue, Omacut, Signed and Folio are: C++ and Qt Quick, one window, following the Omarchy theme and language live, installed as a package.

## Principles

- **One window, one file.** No tabs, file tree, project, LSP, plugins, minimap, split views or multiple cursors. If it ever needs a sidebar, it has stopped being Plain.
- **Conventional keys and mouse.** Everything works the way it does in any GUI editor: shortcuts, scrollbars, a right-click menu. Nothing modal.
- **Never damage a file.** Save atomically or ask first, follow symlinks instead of replacing them, keep line endings and BOM, and never save over a version of the file the user hasn't seen. Saving a config file must never surprise the program that reads it.
- **Looks like the desktop.** The Omarchy palette in the Omarchy monospace font, with syntax colors taken from the theme, so it looks like the terminal next to it.
- **Say what's wrong.** Unreadable, not text, changed on disk, needs root: said in the window, with the way forward.

## The window

A full-bleed editor on the theme background: a quiet line-number gutter, the current line faintly highlighted, the text in the theme foreground, and scrollbars. A thin status line at the bottom shows the path (home shortened to `~`), a dot when there are unsaved changes, the language, `Ln 12, Col 4`, and the indentation (`Spaces: 4` / `Tabs`). The window title is `name — Plain`, with `●` in front when modified.

The find bar slides in above the status line (Ctrl+F), with a replace row (Ctrl+H). A banner above the editor carries the few messages that need an answer: changed or deleted on disk, can't save safely, needs root, not a text file.

Right-click gives Undo, Redo, Cut, Copy, Paste, Select All. Light themes (`mode = "light"`) flip the Material style, as Folio does.

## Keyboard

The Omarchy app conventions of bare `Q`, `?` and Space can't apply in a text editor, where those keys type. Plain uses conventional editor keys instead, and `?`'s job moves to F1.

| Key | Action |
| --- | --- |
| Ctrl+S | Save |
| Ctrl+Shift+S | Save as (portal) |
| Ctrl+O | Open (portal), asking first about unsaved changes |
| Ctrl+Z / Ctrl+Shift+Z, Ctrl+Y | Undo / redo |
| Ctrl+X / C / V / A | Cut, copy, paste, select all |
| Ctrl+F | Find (the selection prefills it) |
| Ctrl+H | Find and replace |
| Enter / Shift+Enter, F3 / Shift+F3 | Next / previous match |
| Escape | Close the find bar or banner |
| Ctrl+G | Go to line |
| Ctrl+/ | Comment or uncomment the line or selection |
| Tab / Shift+Tab | Indent / outdent the selection, or indent at the cursor |
| Alt+Z | Toggle word wrap |
| Ctrl+Plus / Ctrl+Minus / Ctrl+0 | Text size |
| Ctrl+W, Ctrl+Q | Close, asking first about unsaved changes |
| F1 | Every shortcut in an overlay |

Enter keeps the previous line's indentation, nothing cleverer. Find is literal, with a match-case toggle; no regular expressions.

Editing keys (Enter, Tab, Escape, Ctrl+/) are handled on the editor itself, not as window-wide shortcuts, and leave the key alone while an input method is composing, so CJK input and dead keys work. Saving commits any composing text first.

## Files

### Opening

- `plain [+LINE] [--] [FILE]`, and `file://` URIs from xdg-open. Everything after `--` is a literal filename (`omarchy-launch-editor` passes `-- path`). Other URI schemes are refused. No `FILE:LINE`: colons are legal in filenames.
- One file per process. More than one file is an error on stderr with exit 1, not a silent drop (`sudoedit a b` must not lose `b`).
- A path that doesn't exist opens empty and is created on first save, like nano. A missing parent folder is reported, not created.
- Only regular files open. FIFOs, devices and directories are refused before reading, since reading them can block.
- The whole file must be valid UTF-8 with no NUL bytes. Anything else is reported as not a text file and doesn't open: there is no lossy read-only mode that could later be saved.
- Plain runs in the foreground and exits when its window closes, so it works as `$EDITOR`, `$VISUAL` and `$SUDO_EDITOR` (git commit, crontab -e, sudoedit). There's no single-instance daemon. Closing without saving leaves the file as it was, which is how git and crontab already detect an abort.
- Indentation is detected from the file (tabs or spaces, and the width from the most common leading indent). New files use 4 spaces, or 2 for YAML and JSON.
- Large files: phase 1 measures `TextEdit` with the shapes that hurt (one multi-megabyte line, 200,000 short lines, wrapping on, highlighting on) and sets the size Plain asks before opening from what it finds.

### Text in, text out

The editor is `TextEdit` with `textFormat: TextEdit.PlainText`, so pasted rich text arrives as text and nothing is ever interpreted as markup. Its `QTextDocument` is the one source of truth: QML never rebinds `text` after loading, and a reload replaces the content through a `QTextCursor` so even a reload is one undoable step.

Saving serializes block by block rather than with `toPlainText()`, which turns non-breaking spaces into spaces. The round trip is tested: open and save without edits must give back identical bytes, for files with CRLF, a BOM, no final newline, blank final lines, non-breaking spaces and tabs.

- **Line endings**: LF or CRLF, whichever the file uses, written back the same way. A file with mixed endings opens with a banner saying it will be saved with the majority ending; lone CRs count as not a text file.
- **Final newline**: not a flag. The text in the editor is exactly what's saved, so a final newline is simply the empty last line you can see and delete. New files start empty.
- **BOM**: kept if the file had one.

### Saving

All in C++ (`Document`), tested against real files in temp directories.

- **The visible path and the save target are separate.** At open, Plain resolves symlinks to the target and remembers both. Before every save it resolves again; if the link now points somewhere else, that's a conflict to ask about, never a quiet save into a different file.
- **Atomic by default**: `QSaveFile` beside the target, then rename. That keeps the ordinary permission bits and nothing else, because the rename makes a new inode.
- **When a rename would lose something, ask.** Plain checks for these before saving: the folder isn't writable, the file has more than one hard link, it's owned by someone else (a group-writable file), or it carries extended attributes or ACLs. Then the banner says why and offers **Overwrite in place**, which truncates and writes the existing file. It is never a silent fallback.
- **Never over an unseen version.** Plain remembers a hash of the bytes it last read or wrote. Right before saving, it re-reads the target; if the hash differs, or the file is gone, the save stops and the banner offers **Overwrite** or **Reload**. Overwrite acknowledges that one disk version only.
- **No backup, swap or recovery files.** Config files carry secrets, and a crash losing unsaved edits is the same deal nano and gedit offer. Undo is per session.

### Files Plain can't write

When the target isn't writable by the user, the banner says so: the file belongs to root (or another user), and `sudoedit` is the way to edit it, with a button that copies `sudoedit <path>`. Plain itself never escalates in 0.1.

Saving as root from inside the app means a privileged helper, and a shell script run through `pkexec` can't do that safely: there's a race between resolving and checking a path the user controls and replacing it. If it comes back, it's a separate design (a small compiled helper working on file descriptors, with a narrowly bound polkit action and admin authentication on every save), reviewed on its own.

### Changed on disk

A `QFileSystemWatcher` on the visible path, the resolved target and both parent folders, re-armed after every change, since editors and `omarchy-*` scripts replace files rather than writing into them. On a change Plain compares the content hash, not mtime and size:

- No unsaved changes: reload quietly, keeping the cursor line and scroll.
- Unsaved changes: the banner offers **Reload** or **Keep mine**.
- Deleted: the banner says so; saving asks before recreating it, since a deletion may have been deliberate.

Watching is only for news. Safety comes from the check right before saving, which catches what a watcher misses.

### Closing

Unsaved changes ask Save, Don't save or Cancel, from Ctrl+W, Ctrl+Q and the window's close button.

## Syntax highlighting

Plain has its own `QSyntaxHighlighter` with a rule set per language, attached to the `TextEdit`'s document through `QQuickTextDocument`. Recoloring never touches the text or the modified state. Colors come from the theme: comments `secondaryForeground`, strings `green`, numbers `orange`, keys `blue`, sections and keywords `magenta`, booleans and constants `yellow`. No linting, no error colors.

Languages, picked by file name, then extension, then a shebang:

- **Lua**: Hyprland's config is Lua now (`hyprland.lua`, `bindings.lua`, `input.lua`). Long strings and comments with `=` levels (`[==[ ]==]`).
- **conf**: hyprlang (`hypridle.conf`, `hyprlock.conf`), INI, `.desktop`, systemd units and other `key = value` files, as one rule set.
- **TOML** (with `"""` and `'''` strings), **JSON / JSONC**, **YAML**.
- **Shell**: `.sh`, `.bash`, `.zsh`, `bashrc`, `envs`, `PKGBUILD`. Heredoc bodies are left uncolored rather than guessed at.
- **CSS**: Waybar, Walker, GTK.
- Everything else: plain text, no colors.

Strings are matched with escapes, comments don't start inside strings, and multi-line constructs carry the highlighter's block state. Each language has tests for those cases.

## Theme, font and language

- `Theme` from Folio (whole palette, light mode, Hyprland rounding), followed live.
- The font is fontconfig's `monospace`, which Omarchy points at the font picked in its menu, resolved at startup at 14px. A font change in Omarchy applies the next time Plain opens; following it live isn't worth a fontconfig watcher for an editor that's open for a minute. Ctrl+Plus and Ctrl+Minus change the size, and Plain remembers the size and word wrap in `~/.config/plain/settings.conf`. Nothing else is a setting.
- `Translator` from the template, every string through `qsTr()`/`tr()`, and the `.desktop` `Name` and `Comment` translated. Right-to-left languages mirror the chrome (status line, find bar, banners), never the text, which keeps its own direction per line.

## How the code splits

```text
plain.pro
bin/build  bin/test  bin/install
src/main.cpp             arguments, engine setup
src/document.{h,cpp}     load (checks, decoding, detection), serialize, save, conflict checks, watching
src/highlighter.{h,cpp}  QSyntaxHighlighter and the per-language rules, language detection
src/editing.{h,cpp}      edits on the QTextDocument: comment, indent, outdent, auto-indent, find, replace
src/settings.{h,cpp}     text size and wrap, persisted
src/theme.{h,cpp}  src/translator.{h,cpp}   from Folio and the template
src/Main.qml             window, shortcuts, close handling
src/EditorView.qml       Flickable, TextEdit, gutter, scrollbars, context menu
src/FindBar.qml  src/Banner.qml  src/StatusLine.qml  src/HelpOverlay.qml  src/GoToLine.qml
src/portalfilepicker.{h,cpp}  src/filepicker.h   from Omacut
tests/plain_tests.{pro,cpp}
pkgbuild/PKGBUILD  plain.desktop  plain.svg
```

Edits go through `QTextCursor` edit blocks on the shared `QTextDocument`. A spike against a real QML `TextEdit` on Qt 6.12 confirmed that each block is a single step in `TextEdit`'s own undo stack, that `canUndo`/`canRedo` follow, and that `isModified()` returns to false when undo reaches the saved state. The tests drive a real `TextEdit` the same way: type, run a line operation, undo, redo, keep typing.

The gutter finds the first and last visible blocks with `TextEdit.positionAt` at the viewport's top and bottom, and draws numbers only for that range, positioned with `positionToRectangle`. It updates on scroll, resize, font size and wrap changes, so its cost doesn't grow with the file. The view keeps the cursor visible as it moves, and dragging a selection past the edge scrolls.

No agent CLI (`plain check`, `plain render`): agents already edit text files directly, and Plain adds nothing to that.

## Phases

### 1. A safe editor (0.1.0)

Open, edit and save with every guarantee above, conflict checks, changed on disk, close prompts, the gutter, scrollbars and context menu, the status line, theme and language following, literal find and replace, go to line, comment toggle, indent and outdent, copy-indent on Enter, highlighting for the languages above, the keyboard table and the F1 overlay. The large-file measurements and the size limit that comes from them.

Tests: `Document` (byte-identical round trips, symlinks and retargeted symlinks, permissions, the four ask-first cases, conflict and deletion detection, non-UTF-8, NUL, lone CR, mixed endings, FIFO refusal, missing parent), argument parsing, language detection and each highlighter's tricky cases, and each edit through a real `TextEdit` with undo and redo. CJK input checked by hand with fcitx5.

### 2. Omarchy

- A separate config-editor choice: `omarchy-launch-config-editor` opens Plain (when installed) instead of `$EDITOR`, while `$EDITOR` for terminal flows stays whatever the user picked. Plain is also added to `omarchy-default-editor`, and `omarchy-launch-editor` `exec`s it directly in `--inline` mode, so it can be `$EDITOR` for those who want it.
- `text/plain` and the config MIME types open Plain, replacing `nvim.desktop` in `default/applications/mimeapps.list`.
- Package in `omacom/omarchy-pkgs`, add to `install/omarchy-base.packages`, and a migration that installs it and switches the config editor only for people still on the stock choice. An explicit editor choice is left alone.

### 3. Polish that's part of done

Translations for all 50 languages, a README screenshot from the offscreen harness with made-up config, and screen-reader names on the chrome.

## Open questions

- **Wrap by default?** Off (like VSCode) keeps config lines one per row; on avoids sideways scrolling in long comment lines.
- **Validation on save.** Hyprland reports Lua errors itself after a save. Should Plain ever check JSON or TOML syntax and warn before saving a file that won't parse? Tempting, but it's the first step toward an IDE.
- **Saving as root from the app.** Is `sudoedit` enough, or is a compiled, descriptor-based helper worth its own design and review?

## Review

Reviewed by Codex (codex-cli 0.167.0) before building. Taken from it: no silent in-place writes (ask instead) and a precise statement of what atomic saving keeps; root saving out of 0.1, since a shell helper through `pkexec` races; the visible path kept apart from the save target, and retargeted links treated as conflicts; a content-hash check right before every save instead of trusting mtime, size and the watcher; deletion as a conflict; the final newline as visible text rather than a flag; byte-identical round-trip tests (`toPlainText()` rewrites non-breaking spaces); whole-file validation and refusing special files; `TextEdit.PlainText` and one authoritative document; editor-scoped keys that respect input method composition; the visible-range gutter; large-file measurements in phase 1; a separate config-editor preference in Omarchy; one file per process with an error for more; simpler argument parsing; scrollbars, cursor-following and a context menu; no recovery files, stated; and the theme key fix. Cut on its advice: regex replace, language-aware indentation, and move/duplicate/delete line. Kept against it: syntax highlighting in 0.1, since colored keys and comments are much of why a GUI editor beats nano for config.
