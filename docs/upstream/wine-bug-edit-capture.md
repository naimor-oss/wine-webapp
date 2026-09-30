# Draft: WineHQ bug report, edit control keeps the mouse capture

Status: **draft, not filed.** To be reviewed and filed by a person on
bugs.winehq.org under their own account (Product: Wine, Component: comctl32;
the same issue exists in user32). The text below was drafted with an AI
assistant and contains no code; per WineHQ policy, any fix submitted upstream
must be written by a person without AI tools. Check that no bug for it has been
filed since this draft (search "WM_CAPTURECHANGED edit").

---

**Summary:** Edit control never releases the mouse capture if it already had
the capture when the click arrived

**Version:** 11.0 (also present in 11.18 source)

**Description:**

If an edit control already has the mouse capture when it receives
WM_LBUTTONDOWN, it keeps the capture after WM_LBUTTONUP. From then on every
mouse click in the application goes to that edit control, so buttons, tabs
and menus stop responding to the mouse.

Real-world case: applications built with the Clarion runtime set the capture
on an entry field before forwarding the click to the control. After the user
clicks into any field, the mouse no longer works anywhere in the application.

What happens: the edit control's WM_LBUTTONDOWN handler calls SetCapture() on
itself. Since the control already has the capture, SetCapture() sends it
WM_CAPTURECHANGED with lParam naming the control itself (Windows does the same;
see test_DoubleSetCapture in dlls/user32/tests/msg.c). The edit control's
WM_CAPTURECHANGED handler unconditionally clears its internal "capture active"
flag, so the WM_LBUTTONUP handler then skips ReleaseCapture().

Same logic in dlls/comctl32_v6/edit.c and dlls/user32/edit.c.

**Steps to reproduce** (black-box, any edit control):

1. Create a single-line edit control.
2. Call SetCapture() on it.
3. Send it WM_LBUTTONDOWN (MK_LBUTTON) and then WM_LBUTTONUP.
4. Check GetCapture().

**Expected** (Windows 11, both the Common Controls 6 edit control and the
user32 one, checked 2026-09-30): GetCapture() returns NULL.

**Actual** (Wine 11.0): GetCapture() still returns the edit control.

Without step 2 both Windows and Wine release the capture correctly.
