# Pixee File-Operations Test Suite

## Context

Pixee currently has zero automated tests. The recently-shipped task subsystem (`CopyFileTask`, `MoveFileTask`, `DeleteFileTask`, `ScaleImageTask`, `ConvertFormatTask`, `FolderCleanupTask`) plus the surrounding plumbing (`Task::checkPauseStop`, `TaskGroup` sticky answers, `TaskManager` worker pool, the conflict handshake, the SMB hard-delete fallback, recursive `expandToFiles`) is exactly the area where bugs hurt most — silent data corruption is unrecoverable.

We want a lean test suite that catches regressions in the **task layer and its helpers**, not the UI. UI bugs surface immediately on the first manual run; task bugs hide in the threading, the cooperative-cancel handshake, and the conflict flow until they bite a user mid-batch.

Scope: file-operation tasks + their pure-logic helpers. Explicitly excluded: thumbnail pipeline, viewer / image loader, model layer (`FileItem` / `FileModel`), all Qt widgets, and the actual OS Recycle Bin — trash tests would pollute the developer's bin on every run, so we stub at the boundary and only cover the hard-delete fallback path.

Framework: **QtTest** (built into Qt; `QT += testlib`). Zero new dependencies, native `Q_OBJECT` / `QSignalSpy` integration, fits the qmake build.

## Architecture (one-screen overview)

```
Pixee.pro                              (unchanged — app build)
tests/tests.pro                        TEMPLATE = subdirs
├── TestHelpers.{h,cpp}                shared fixtures: file/image factories,
│                                      QSignalSpy-based wait helpers, TaskTestFixture
├── FileOpsHelpers/
│       ├── FileOpsHelpers.pro         one binary per test class (crash-isolation)
│       └── tst_FileOpsHelpers.cpp     pure-logic unit tests (no threads)
├── CopyFileTask/
│       ├── CopyFileTask.pro
│       └── tst_CopyFileTask.cpp       happy / cancelled / paused / conflict flows
├── MoveFileTask/                      rename happy path + chunked fallback
├── DeleteFileTask/                    hard-delete only (toTrash=false)
├── TaskGroup/                         sticky answers, sequential per-group dispatch
├── ImageTasks/                        ScaleImageTask + ConvertFormatTask
└── FolderExpand/                      expandToFiles + recursive copy/move integration
```

- The app's `Pixee.pro` stays standalone — no `TEMPLATE = subdirs` restructure. The test tree lives entirely under `tests/` and references app source files via relative paths in each subdir's `.pro` (`SOURCES += $$PWD/../../FileOpsHelpers.cpp`). Less invasive: no `.pro.user` invalidation, no binary path changes.
- Each `tst_*.cpp` is a separate `QTEST_GUILESS_MAIN`. One binary per file means a crash in one suite doesn't take down the others, and CI failure isolation is per-file.
- Tests link the same `.cpp` files the app does — no static lib, no shared object — so a refactor across the app/test boundary is a single edit.
- Async waits use `QSignalSpy::wait(timeoutMs)` (returns false on timeout). No `QTest::qWait` sleeps — they make tests flaky and hide deadlocks.
- File system isolation via `QTemporaryDir` per test method — auto-cleaned in dtor, lives entirely in `%TEMP%` so a crash mid-test leaves nothing behind.

## Local run

```sh
mkdir build-tests && cd build-tests
qmake ../tests/tests.pro -recursive
mingw32-make
# Each suite is its own binary; PATH must include the Qt bin dir for DLLs.
PATH="C:\Qt\6.11.0\mingw_64\bin;$PATH" ./FileOpsHelpers/release/tst_FileOpsHelpers.exe
```

## Phase 1 — Bootstrap + helper extraction ✅ shipped

**Goal**: green smoke test in CI-able form before writing any real assertions.

**What landed**:
- `tests/tests.pro` — `TEMPLATE = subdirs`, lists `FileOpsHelpers/` for now. New entries appended as each phase adds a binary.
- `tests/TestHelpers.{h,cpp}` — Phase 1 only ships the file/image factory helpers (`writeBytes`, `writeImage`). The `TaskTestFixture` (QTemporaryDir + TaskManager + signal-spy waits) lands in Phase 3 where it's first exercised.
- `tests/FileOpsHelpers/FileOpsHelpers.pro` + `tst_FileOpsHelpers.cpp` — smoke test binary.
- `FileOpsHelpers.{h,cpp}` — pure-logic helpers extracted out of anon namespaces so the test layer can reach them across translation units:
  - `isDriveRoot(const QString&)`
  - `destIsSourceOrDescendant(const QString& dest, const QString& src)`
  - `expandToFiles(const QString& src, const QString& destBase) → QList<Pair>` (and its `Pair` struct)
  - `uniqueRenamedPath(const QString&)` — bonus DRY: four duplicate copies in `CopyFileTask.cpp` / `MoveFileTask.cpp` / `ConvertFormatTask.cpp` / `ScaleImageTask.cpp` collapsed into one.
  - `clipboardSaysCut(const QMimeData*)`
  - `dropEffectBytes(quint32) → QByteArray`
  - `kDropEffectMime` constant (Windows MIME format string).
- `pickDropAction` stayed inside `FileListView` — it reads modifiers off a `QDropEvent`, not pure enough to be worth extracting.

**Smoke test** (in `tst_FileOpsHelpers.cpp`):
```cpp
void harnessSmoke() {
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());
    const QString p = tmp.path() + "/probe.bin";
    TestHelpers::writeBytes(p, 1024);
    QVERIFY(QFile::exists(p));
    QCOMPARE(QFileInfo(p).size(), qint64(1024));
}
```

**Verification**: `qmake ../tests/tests.pro -recursive && mingw32-make && ./FileOpsHelpers/release/tst_FileOpsHelpers.exe` prints `3 passed, 0 failed, 0 skipped` and exits 0. App build remains clean (only the pre-existing unused-parameter warning in `FileModel::headerData`).

## Phase 2 — Pure-logic unit tests

**Goal**: cover the helpers extracted in Phase 1. Fast (sub-millisecond per test), no threads, no I/O beyond `QTemporaryDir`.

**`tst_FileOpsHelpers.cpp`** — one method per branch, table-driven where the input space is enumerable:

- `isDriveRoot`:
  - `C:/`, `C:\`, `/`, `D:/`, network root `//server/share/`, ordinary path `C:/foo`, file `C:/foo/bar.txt`.
- `destIsSourceOrDescendant`:
  - identical paths (with trailing slash, without, mixed `/` and `\`).
  - dest is direct child, dest is grandchild.
  - dest is sibling (negative).
  - case-only difference on Windows — document current behaviour (case-sensitive), don't pretend we do anything else.
  - `C:/foo` vs `C:/foobar` (substring trap — must not match).
- `expandToFiles`:
  - single file source → one pair, dest is `destBase/filename`.
  - empty folder source → folder created at dest, no file pairs.
  - 2-level nested folder → file pairs for every leaf, mkpath replicates structure.
  - missing source → empty list, no exception.
  - symlink source → skipped (`QDir::NoSymLinks`).
- `uniqueRenamedPath`:
  - dest doesn't exist → returns input unchanged.
  - dest exists → returns `name (1).ext`.
  - `name (1).ext` also exists → returns `name (2).ext`.
  - no extension → `name (1)` form.
  - 10000-collision give-up → returns input (don't actually create 10k files; mock with a stub that always returns `true` from `QFile::exists` — easiest is to test against a real `tmp.path()` with a synthetic stem and assert behaviour at small N, then unit-test the loop bound separately).
- `clipboardSaysCut` / `dropEffectBytes`:
  - round-trip every well-known DROPEFFECT value (1 = COPY, 2 = MOVE, 4 = LINK, 5 = COPY+SCROLL).
  - `clipboardSaysCut` on a mime with no `Preferred DropEffect` → false (no exception).
  - 3-byte truncated payload → false (no out-of-bounds read).

**Verification**: every test method has at least one `QCOMPARE` or `QVERIFY`; no test relies on filesystem state surviving across methods (each constructs its own `QTemporaryDir` if it needs one).

## Phase 3 — CopyFileTask end-to-end

**Goal**: exercise the conflict handshake and the cooperative-cancel loop with real file I/O on temp paths. This is the densest test file because `CopyFileTask` is the prototype every other task copies from.

**Adds to `tests/TestHelpers.{h,cpp}`**:
- `class TaskTestFixture` (a plain helper struct, not a base class):
  - `QTemporaryDir tmp` member with `tmp.path()` accessor.
  - `TaskManager mgr{2}` member — two workers (matches Config default).
  - QSignalSpy members on `taskFinished` / `taskFailed` / `taskAborted` / `groupRemoved` / `taskQuestionPosed`.
- Wait helpers:
  - `bool waitForGroupRemoved(int timeoutMs = 5000)` — most tests just need "task is done". This is the cleanest end-of-run signal.
  - `bool waitForQuestion(QUuid* outTaskId, QVariantMap* outCtx, int timeoutMs = 2000)` — for conflict tests.

**`tst_CopyFileTask.cpp`**:

- **Happy path, small file**: 1 KB source, copy → assert `groupRemoved` fires within 1 s, dest exists, byte-for-byte equal to source, `taskFinished` emitted exactly once, no `taskFailed` / `taskAborted`.
- **Happy path, multi-MB file**: 4 MB random-byte source — exercises chunked loop (64 KB chunks → ~64 iterations), `taskProgress` emitted multiple times with monotonic non-decreasing values, final value 100.
- **Source missing**: dest path valid, source path doesn't exist → `taskFailed` with non-empty message, no dest file created.
- **Dest parent doesn't exist**: source exists, dest is `tmp/sub/dir/file.bin` with `sub/dir` not pre-created → succeeds (the task `mkpath`s the parent).
- **Cancel mid-copy** (the important one): 50 MB source so the chunked loop runs long enough; spy on `taskProgress`, when first progress signal fires call `mgr.stopTask(id)`, assert `taskAborted` within 2 s, assert dest file does NOT exist (the task removes the partial file on cancel).
- **Pause then resume**: 10 MB source. After first `taskProgress`, `pauseTask`; assert `taskStateChanged → Paused`; spin-wait 200 ms confirming no further `taskProgress` fires; `resumeTask`; assert `taskFinished` arrives, dest is byte-for-byte equal.
- **Conflict — Skip**: pre-create a dest file with known bytes, run copy, when `taskQuestionPosed` fires call `provideAnswer(taskId, DestinationExists, Skip, false)`; assert `taskFinished` (Skip is a successful terminal, not a failure), dest bytes unchanged from pre-test.
- **Conflict — Overwrite**: same setup, answer `Overwrite`; assert dest bytes equal source bytes after.
- **Conflict — Rename**: same setup, answer `Rename`; assert original dest bytes unchanged AND a new file `name (1).ext` exists with source bytes.
- **Conflict ignored on stop**: pre-create dest, enqueue copy, before answering call `stopTask`; assert `taskAborted` arrives without the test ever calling `provideAnswer` (the answer-cv unblocks on stop and `run()` bails on `isStopRequested()`).

**Pattern**: every test spins up a fresh `TaskTestFixture` in the test method body. No `init()` / `cleanup()` — easier to read, slight duplication is fine.

## Phase 4 — Move + Delete + Cleanup

**`tst_MoveFileTask.cpp`**:
- **Same-volume rename**: source and dest on same `QTemporaryDir` → `QFile::rename` succeeds, source gone, dest exists, assert quickly (≤10 ms).
- **Rename failure → fallback** (the bug-prone branch): need to force `QFile::rename` to fail. Options: (a) make dest already exist so rename returns false → exercises the `copyAndDelete` path; (b) use a `QTemporaryDir` mounted under a different volume on systems where one exists. Pick (a) — universal and tests the same code path. Pre-create dest with known bytes, configure conflict answer to Overwrite, run move; assert source gone, dest replaced.
- **Cross-volume conditional**: if `QStorageInfo::mountedVolumes()` reports a second writable volume (often `D:` with a temp dir under it), include a real cross-volume test; otherwise `QSKIP("no second writable volume available")`. Marked optional — not all dev machines have it.
- **Source disappears mid-move**: hard to provoke deterministically; skip in v1, document as manual.
- **Conflict flows**: same Skip/Overwrite/Rename matrix as CopyFileTask but compressed — Move shares the handshake, no need to re-verify the full conflict state machine, just one of each to confirm wiring.

**`tst_DeleteFileTask.cpp`** — `toTrash=false` only (per scope decision):
- Single file → file gone, `taskFinished`, `affectedDirs()` returns parent.
- Folder containing files → folder + contents gone (recursive remove).
- Missing path → `taskFailed` with descriptive message, no crash.
- Read-only file on Windows → `taskFailed` (current behaviour — confirm we don't silently skip the failure).

**`tst_FolderCleanupTask.cpp`** (small):
- Empty folder → removed.
- Non-empty folder → left in place, task completes successfully (cleanup is best-effort).
- Missing folder → succeeds without complaint (idempotent).

**Trash tests are explicitly out of scope.** Document at the top of `tst_DeleteFileTask.cpp` why: "Recycle Bin pollution. Trash path is exercised manually."

## Phase 5 — TaskGroup + sticky answers + manager dispatch

**`tst_TaskGroup.cpp`** — covers the bits that span multiple tasks:

- **Sticky answer applies to subsequent tasks**: build a group of 3 `CopyFileTask`s, all 3 dest paths pre-existing. When the first `taskQuestionPosed` fires, answer `Overwrite` with `applyToGroup=true`. Assert the next two tasks finish without further `taskQuestionPosed` emissions (the group's sticky map short-circuits `resolveOrAsk`).
- **Sticky answer does NOT cross groups**: same as above but enqueue a second group with a conflicting copy after the first completes; assert the second group asks again on its first conflict.
- **Group stop cancels queued tasks**: 3-task group, `stopGroup` after the first task finishes — assert the remaining two emit `taskAborted` (or never start, depending on dispatch timing) and `groupRemoved` arrives.
- **Sequential per-group dispatch**: 2-task group with 2-worker manager. Pause the first task, assert the second never starts (one worker per group). Resume; assert it does.
- **Parallel across groups**: enqueue 2 single-task groups simultaneously, assert both `taskStateChanged → Running` arrive before either finishes (with a slow enough source — use 2 MB files — to make the overlap observable).

**`tst_TaskManager.cpp`** (or merge into `tst_TaskGroup.cpp` — judgment call, probably merge for v1):
- Shutdown is idempotent: call `shutdown()` twice in a row → no crash, no hang.
- Shutdown mid-run: enqueue a long copy, call `shutdown()` while it's running — assert it returns within ~1 s and no thread is left.
- `pathTouched` emits per affected dir on completion.

## Phase 6 — Image format tasks + folder-expand integration

**`tst_ImageTasks.cpp`**:
- **ScaleImageTask happy**: write a 2000×1000 PNG via `writeImage`, scale to longest-edge 1024 → output exists, `QImageReader::size()` reports 1024×512 (aspect preserved).
- **ScaleImageTask EXIF orientation**: write a JPG with orientation=6 (rotate-90 CW) — the input is 1000×2000 stored, decodes to 2000×1000 displayed. Scaling to 1024 should produce 1024 wide, 512 tall (matching the displayed orientation, not the stored orientation). If `setAutoTransform(true)` is wired correctly, this passes; if not, it produces 512×1024 — a real bug worth catching.
- **ScaleImageTask quality round-trip**: scale → re-decode → file size sanity (don't assert exact bytes; assert the file is a valid JPEG via `QImageReader::canRead`).
- **ConvertFormatTask happy**: PNG in → JPG out, output is a valid JPEG, dimensions preserved.
- **Conflict on dest**: same as Copy — pre-existing dest, answer Skip → no overwrite.
- **Cancel mid-decode**: hard to cancel a `QImageReader::read()` (the codec doesn't yield); document as not-tested. We can still test that `stopTask` before `run()` starts results in `taskAborted` cleanly.

**`tst_FolderExpand.cpp`** — the integration glue between `expandToFiles` and the task pipeline (already unit-tested in Phase 2, this verifies it actually drives the manager correctly):

- **Recursive copy**: build `src/{a.bin, sub/{b.bin, c.bin}, empty/}` in a temp dir; expand into a list of `CopyFileTask`s wrapped in one group; enqueue; await `groupRemoved`; assert dest mirrors the structure and every file is byte-equal.
- **Mixed file + folder selection**: source list contains one loose file and one folder → dest gets the file at top level AND the folder mirrored.
- **Self-descendant rejection** (tested in Phase 2 too, but verify it surfaces at the menu-builder layer — call `FileOpsMenuBuilder::handleDropOrPaste` or whichever public path expands and enqueues, and assert nothing gets enqueued + the appropriate Toast / dialog hook fires). May skip if the rejection path is too entangled with `QApplication` for a console test — document and move on.

## Critical files

- `tests/tests.pro` — top-level subdirs project; new SUBDIRS entry per phase.
- `tests/TestHelpers.{h,cpp}` — shared fixtures + factories. Grows in Phase 3 (TaskTestFixture).
- `tests/<Suite>/<Suite>.pro` + `tst_<Suite>.cpp` — one pair per test class.
- `FileOpsHelpers.{h,cpp}` — pure helpers shared with the app. The single source of truth for `uniqueRenamedPath`, `expandToFiles` etc.

## Reference patterns to mirror

- **Async wait without sleeps**: `QSignalSpy::wait(timeoutMs)` returns false on timeout. Pattern is `QVERIFY(spy.wait(2000))` rather than `QTest::qWait(2000)` — fast happy-path, true ceiling on flaky-path.
- **Real image fixtures**: `QImageWriter` writes a valid PNG/JPG to disk in <1 ms for small sizes. Don't ship test images in the repo — generate them on demand. Same approach as `Theme::pixmaps` uses for runtime, just inverted.
- **Per-test isolation**: `QTemporaryDir` in the test method body, not in `init()` — explicit lifetime, no surprising cross-contamination.
- **Cooperative-cancel timing**: the existing tasks check `checkPauseStop()` per chunk. To deterministically catch a task mid-run, ensure the source file is large enough that `kChunkSize=64KB` produces ≥ 10 iterations (so ≥ 640 KB). The Phase 3 plan picks 4 MB / 10 MB / 50 MB to leave headroom.

## Out of scope (explicit non-goals for v1)

- Trash / Recycle Bin tests. (Pollution.)
- UI tests (`QTest::mouseClick`, model-view roundtrips, dialog flows). The menu wiring is shallow; manual exercise catches it.
- Thumbnail pipeline (`ThumbnailCache`, `ThumbnailGenerator`, `ThumbnailWorker`) — separate, equally bug-prone subsystem; plan separately if/when wanted.
- Viewer / `ImageLoader`.
- Model layer (`FileItem`, `FileModel`, `FolderRefresher`, `FolderEnumerator`).
- CI integration (no CI configured in the repo). Once a CI lands, add a `make check` invocation and that's it — the suite is already CI-shaped.
- Code coverage measurement.
- Network / SMB shares — every test is local. SMB-specific paths (the trash-fallback to hard-delete) are exercised by the `toTrash=false` branch on a regular temp dir.
- Memory leak detection. The existing manual `delete _childItems.takeAt(row)` fix in `FileItem` was found by code review; v1 of this suite isn't going to chase leaks, just correctness.
