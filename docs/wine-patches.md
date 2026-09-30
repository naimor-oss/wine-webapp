# Wine patches and workarounds

Everything this image changes about stock WineHQ, why, and when it can go.
Every entry is a `FIXME(remove-when-fixed)` in the sense of
`dev-commons/STYLE.md` §10.

| # | Problem | Our fix | Where | Upstream |
| --- | --- | --- | --- | --- |
| 1 | Mouse stops working after clicking into a text field | Patched edit controls | fork `naimor/wine-11.0`, `comctl32_v6.dll`, `user32.dll` | not filed; draft in [`upstream/`](upstream/wine-bug-edit-capture.md) |
| 2 | Typed text invisible in text fields | Visual theme off | `rootfs-base/etc/wine-webapp/prefix.d/10-disable-visual-theme.reg` | bug not yet filed |

## How patches are carried

- Our Wine fork is [`Naimor-OSS/wine`](https://github.com/Naimor-OSS/wine),
  forked from the `wine-mirror/wine` GitHub mirror.
- For each Wine release we ship on, branch `naimor/wine-<version>` starts at the
  release tag and carries our commits in Wine's own style (test first, then the
  fix removing `todo_wine`).
- The image installs the WineHQ binary packages of that release and replaces
  only the DLLs we patched (`WINE_PATCHED_DLLS` in the `Dockerfile`), built
  from the fork by `build/build-wine-dlls.sh`. Building only those modules
  takes minutes instead of the hour a full Wine build takes.
- Moving to a new Wine release: create `naimor/wine-<new>` from the new tag,
  cherry-pick the commits still not upstream, update `WINE_PKG`,
  `WINE_FORK_REF` and `WINE_PATCHED_DLLS` together, rebuild, rerun the checks
  below.

### Why these patches are not sent to Wine as merge requests

WineHQ does not accept code written with an LLM tool (Developer FAQ, and the
Clean Room Guidelines). The fork commits were written with Claude and carry a
`Co-Authored-By` trailer; they stay in the fork. Upstream gets **bug reports**
with the analysis, a reproduction and black-box test results from Windows,
so that a Wine developer can write the fix. Once a fix is in a Wine release,
drop our commit when moving to that release.

## 1. Edit control keeps the mouse capture

**Symptom.** After clicking into a text field, every later click anywhere in
the app goes to that field: tabs, buttons and title bars stop responding until
the app restarts. Seen with a Clarion application (its runtime does this for
every entry field); applies to any app that sets the capture on an edit control
before passing a click to it.

**Cause (Wine 11.0, still present in 11.18).** `SetCapture()` on the window that
already has the capture sends it `WM_CAPTURECHANGED` naming itself as the new
capture window. That matches Windows (Wine's own `test_DoubleSetCapture` in
`dlls/user32/tests/msg.c` checks it). Wine's edit controls then clear
`bCaptureState` unconditionally, so on `WM_LBUTTONUP` they skip
`ReleaseCapture()` and keep the capture forever. Both edit implementations
have it: `dlls/comctl32_v6/edit.c` (apps with a Common Controls 6 manifest) and
`dlls/user32/edit.c` (apps without).

**Evidence.** A conformance test (`test_capture` in
`dlls/comctl32/tests/edit.c` and `dlls/user32/tests/edit.c` on the fork
branch) sets the capture on an edit control, then sends button down and up:

| Run | comctl32 v6 | user32 |
| --- | --- | --- |
| Windows 11 (native, 2026-09-30) | passes | passes |
| Wine 11.0 unpatched | fails | fails |
| Wine 11.0 + fork DLLs (image build of 2026-09-30) | passes | passes |

The Wine rows were measured with the test built from the test-first commits
(still marked `todo_wine`): unpatched Wine reports "Test marked todo", the
fork reports "Test succeeded inside todo block", which is why the fix commits
remove the marker.

**Fix in the fork.** Ignore `WM_CAPTURECHANGED` when `lParam` is the control
itself.

**Remove when** a Wine release we ship contains an upstream fix: drop the two
`*/edit:` commits when rebasing onto that release, keep checking with the test.

## 2. Wine visual theme paints over self-drawn edit text

**Symptom.** Characters typed into a field move the caret but don't show,
although they are stored. Seen with a Clarion application on Wine 10.0 and
11.0 with Wine's default "Light" theme.

**Cause (analysis so far).** The Clarion runtime draws an entry field's text
itself (into a memory bitmap, blitted to the control) and then sends the
control `WM_NCPAINT`. With a theme active, the themed edit control's
non-client painting fills the whole control, client area included, covering
the text. With the theme off, only the border is drawn. Not yet reduced to a
conformance test; on Windows with themes on, the same application draws
correctly.

**Workaround.** `ThemeActive=0` in the prefix (Wine's default look before 9.0).

**Remove when** an upstream fix ships; to check, delete the `.reg` file,
rebuild, and type into a field of an affected application.

## Checks after changing Wine or the patches

1. Build the conformance tests from the fork and run the edit tests on a
   Windows machine and under the new image (see the fork branch's commits for
   the test).
2. Smoke test: `examples/notepad`, type text, click elsewhere, drag the window.
3. Run `tools/locktest` if the storage layout changed.
