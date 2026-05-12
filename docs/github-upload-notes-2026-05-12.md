# GitHub Upload Notes - 2026-05-12

## Summary

- Fixed Release x64 build by retargeting the project to the installed Windows SDK.
- Added cross-process batch coordination for Explorer multi-select actions.
- Changed batch operation notifications to avoid success-only popups; failures still surface.
- Suppressed internal batch-mode warning/error popups so failures are reported once through the final summary.
- Reworked Super Delete so destructive deletes are confirmed before any target is deleted.
- Improved ModernMsgBox dynamic width calculation for multi-line content.
- Established the popup policy for upload review: first decide whether an operation supports Explorer multi-select; single-item operations should use the same failure-summary style as a one-item batch, while success is usually silent.

## File Notes

### VitraMenu.vcxproj

- Changed `WindowsTargetPlatformVersion` from missing SDK `10.0.22621.0` to installed SDK `10.0.26100.0`.
- Removed hardcoded Windows Kit path assumptions by relying on inherited MSBuild include/library paths.
- Added `BatchCoordinator` source/header entries so batch coordination is compiled.
- Upload note: this change assumes GitHub/CI builders have Windows SDK `10.0.26100.0` or can retarget. If CI uses a different SDK, consider changing this to a repo-supported SDK version.

### include/BatchCoordinator.h

- New header for cross-process batch operation tracking.
- Exposes result recording, consolidated notification, and destructive-operation confirmation APIs.
- Used by multi-select context menu operations where Explorer launches one process per selected item.

### src/BatchCoordinator.cpp

- New implementation for batch coordination using temp files plus named mutexes.
- Tracks active operation PIDs so only one process displays the final notification.
- Records per-target results and reads them back for a consolidated summary.
- Added Super Delete confirmation coordination:
  - all selected targets are registered before delete starts;
  - only one confirmation dialog is shown;
  - all processes wait for the shared yes/no decision;
  - no target is deleted if the user cancels.
- Added Unlock-specific summary behavior:
  - no-lock results are filtered from occupied-file counts;
  - fully successful unlock operations do not show a second popup;
  - only cancellation/failure/partial failure shows a follow-up warning.
- Changed generic batch summary policy to success-silent/failure-visible.
- Removed the single-result special case from batch summaries; a single item now uses the same batch summary path as multiple items.
- Super Delete failure summaries distinguish full failure from partial failure and list failed items.
- Clean Empty Folders now records and displays folders that failed to scan or failed to delete.
- Clean Empty Folders is now routed through batch coordination and shared confirmation.
- Upload note: keep failure summaries as the only final popup. Do not add per-item warning/error popups inside batch-suppressed operations.
- Upload note: batch coordination state is stored in `%TEMP%` files named `VitraMenu_<operation>_*`; stale files are cleared when a new batch begins with no active PIDs.

### include/Localization.h

- New shared localization helper.
- Centralizes Chinese/English selection for new batch and message-box text.
- Upload note: keep new UI strings routed through this helper or existing `LText` wrappers.

### src/main.cpp

- Routes multi-select-capable commands through `BatchCoordinator`.
- Suppresses per-item success popups during batch actions.
- Adds consolidated handling for unlock, encoding, firewall rules, take ownership, clear read-only, and super delete.
- Adds consolidated handling for clean empty folders.
- Super Delete now calls the shared destructive confirmation before executing delete logic.
- Upload note: do not move destructive execution before `ConfirmDestructiveOperation`; it is what prevents mixed file/folder selections from deleting files before confirmation.
- Upload note: clean empty folders now uses `ConfirmDestructiveOperation` before deleting empty folders under the selected root folders.

### src/FeatureManager.cpp

- Unlock:
  - returns structured batch messages for no-lock, cancellation, failure, and process details;
  - treats partial process termination as failure for summary purposes;
  - no longer causes a second success popup after all locking processes are terminated.
- Super Delete:
  - per-item success notifications are suppressed during batch mode;
  - delete confirmation is now handled before `SuperDelete` is called from `main.cpp`.
- Clean Empty Folders:
  - collects folders that cannot be scanned;
  - collects empty folders that fail deletion;
  - displays failed folder paths in the partial-failure dialog with localized text.
- Several operations now return boolean success values used by batch summaries.
- Upload note: direct single-operation flows may still show their own immediate dialogs; batch flows rely on `ModernMsgBox::SetSuppressed(true)`.

### include/FeatureManager.h

- Updated function declarations to support boolean results and optional batch messages.
- Upload note: keep declarations in sync with `FeatureManager.cpp` before staging.

### src/ModernMsgBox.cpp

- Fixed dynamic width calculation for all modern message boxes.
- Width is now based on the longest visible line, not the full text length.
- Multi-line summaries no longer become overly wide when they contain many short file names.
- Long single-line text still wraps within a max width.
- Batch suppression now hides all non-confirmation message boxes, including warnings and errors, so per-item failures do not duplicate the consolidated failure popup.

### include/ModernMsgBox.h

- Exposes suppression state access used by feature logic to distinguish batch mode from direct UI mode.

### src/MenuInstaller.cpp

- Context menu registration participates in the updated command routing.
- Upload note: reinstall context menu entries after release if command strings or icons changed.

### src/RegistryManager.cpp / include/RegistryManager.h

- Registry/context-menu support changes are present in the working diff.
- Upload note: review these changes before committing to ensure they are intentional and aligned with menu installation behavior.

### src/UIManager.cpp / include/UIManager.h

- UI/menu text and command surface changes are present in the working diff.
- Upload note: verify Chinese/English labels and installed-menu state after building.

### .gitignore

- Working diff includes one ignore-rule change.
- Upload note: review before staging so generated build artifacts are not accidentally committed.

## Files With Git Status But No Textual Diff

The following files appear modified in `git status`, but did not appear in `git diff --name-only` at the time of this note. They may be line-ending-only changes or pre-existing local metadata changes:

- `README.md`
- `VitraMenu.sln`
- `include/PyWinTypes.h`
- `include/PythonCOM.h`
- `include/PythonCOMRegister.h`
- `include/PythonCOMServer.h`
- `include/hooks.h`
- `include/test.h`
- `src/hooks.cpp`

Upload note: inspect these with whitespace-aware tools before staging. Avoid committing unrelated line-ending churn unless intended.

## Behavior Checklist

- Popup policy:
  - classify each command as multi-select-capable or single-only before reviewing UI behavior;
  - single-item failure popups should use the same failure-summary style as a one-item batch;
  - success usually does not show a final popup;
  - partial failure must show which items failed;
  - full failure must list all failed items;
  - all user-visible text must have Chinese and English variants.
- Unlock:
  - one initial confirmation per locked item may still appear because terminating processes is interactive;
  - no second popup when unlock succeeds;
  - one failure popup only when cancellation/failure/partial failure occurs;
  - if no selected item is locked, one no-lock popup appears.
- Super Delete:
  - one confirmation appears before any deletion;
  - cancel means no selected target is deleted;
  - success is silent after deletion;
  - failure or partial failure shows one summary popup with failed items listed.
- Clean Empty Folders:
  - one confirmation appears before deletion starts;
  - success is silent in batch mode;
  - scan failures list the folders that could not be scanned;
  - delete failures list the empty folders that could not be deleted;
  - multi-select directory runs are consolidated through `BatchCoordinator`.
- Generic batch operations:
  - success-only result summaries are suppressed;
  - failures remain visible.
  - internal per-item warning/error dialogs are suppressed while the batch is running, leaving only one final failure summary.
  - single-item operations are treated as a one-item batch.
- ModernMsgBox:
  - multi-line popups size to actual content width instead of total character count.

## Verification

- Built with:

```powershell
& 'E:\Visual Studio 2026\MSBuild\Current\Bin\MSBuild.exe' .\VitraMenu.sln /t:Build /p:Configuration=Release /p:Platform=x64 /m
```

- Latest result: Release x64 build succeeded with `0 Warning(s), 0 Error(s)`.

## Suggested PR Summary

- Fix multi-select context menu popup spam by consolidating batch results.
- Require one shared confirmation before irreversible Super Delete operations.
- Make successful batch operations silent while preserving failure reporting.
- Improve custom message-box sizing for real multi-line content.
- Retarget project to installed Windows SDK.
