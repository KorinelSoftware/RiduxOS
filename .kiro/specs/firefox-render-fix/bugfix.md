# Bugfix Requirements Document

## Introduction

Firefox loads successfully in RiduxOS (custom kernel) — the process starts, `ld-linux` maps all `.so` files, and both X11 and Wayland sockets connect — but Firefox never renders anything to the framebuffer and exits silently. Serial log analysis (`build/vbox-serial-20260425-015515.log`) identifies five root causes: a missing `clone3` syscall (nr=435), a missing `sendfile` syscall (nr=187), an unhandled `clone(CLONE_NEWUSER)` permission error, the X11/Wayland compositor not blitting Firefox window content to the physical framebuffer, and the VFS `read()` returning 1 byte at a time instead of the full requested count. All five must be resolved for Firefox to render its UI at 1920×1080×32bpp.

---

## Bug Analysis

### Current Behavior (Defect)

1.1 WHEN Firefox calls `clone3` (syscall nr=435) to create content processes THEN the system returns `ENOSYS` (-38), causing Firefox to fail spawning its content process

1.2 WHEN Firefox calls `sendfile` (syscall nr=187) to transfer each `.so` file during library loading THEN the system returns `ENOSYS`, causing repeated failures for every shared library loaded

1.3 WHEN Firefox calls `clone(CLONE_NEWUSER)` with flags `0x10000011` to create a user namespace for its sandbox THEN the system returns `-1` (unspecified error) instead of a proper `-EPERM`, causing Firefox to abort sandbox setup rather than gracefully falling back to no-sandbox mode

1.4 WHEN Firefox successfully connects to the X11 socket (`/tmp/.X11-unix/X0`) and the Wayland socket (`/run/user/0/wayland-0`) and submits window content THEN the compositor in `compat7.c` never calls `wl7_render_surfaces()` or the equivalent X11 blit path to composite Firefox's window content onto `g_backbuffer`, so nothing appears on the physical framebuffer

1.5 WHEN Firefox reads `/opt/firefox/dependentlibs.list` to discover its dependent libraries THEN the VFS `real_sys_read` implementation returns 1 byte per call regardless of the requested count, causing Firefox to loop thousands of times to read the file

### Expected Behavior (Correct)

2.1 WHEN Firefox calls `clone3` (syscall nr=435) THEN the system SHALL handle the call by either implementing `clone3` natively or translating it to the existing `clone` (nr=56) implementation, returning a valid child PID so Firefox can spawn its content process

2.2 WHEN Firefox calls `sendfile` (syscall nr=187) with a source file descriptor and a destination file descriptor THEN the system SHALL implement `sendfile` as a fallback `read`+`write` loop, returning the number of bytes transferred so library loading proceeds without error

2.3 WHEN Firefox calls `clone(CLONE_NEWUSER)` with flags `0x10000011` THEN the system SHALL return `-EPERM` (errno 1) so Firefox receives a well-formed permission error, prints its "no-sandbox mode" message, and continues execution

2.4 WHEN Firefox submits window content via the X11 or Wayland protocol to the compositor in `compat7.c` THEN the system SHALL call `wl7_render_surfaces()` (and the X11 equivalent blit path) to composite Firefox's window pixels onto `g_backbuffer` and flush them to the physical 1920×1080×32bpp framebuffer, making Firefox's UI visible

2.5 WHEN Firefox calls `read()` on `/opt/firefox/dependentlibs.list` requesting N bytes THEN the system SHALL return up to N bytes per call (the full requested count when available), so Firefox reads the file in a small number of calls rather than one byte at a time

### Unchanged Behavior (Regression Prevention)

3.1 WHEN any existing process calls `clone` (nr=56) with flags that do not include `CLONE_NEWUSER` THEN the system SHALL CONTINUE TO create the child task and return a valid PID as before

3.2 WHEN any process calls `read()` on files other than `dependentlibs.list` (e.g., `/proc/self/stat`, device nodes, sockets) THEN the system SHALL CONTINUE TO return data using the existing read logic without regression

3.3 WHEN the X11/Wayland compositor renders windows for processes other than Firefox THEN the system SHALL CONTINUE TO composite and display those windows correctly on the framebuffer

3.4 WHEN Firefox or any process calls `sendfile` with a zero-length transfer or invalid file descriptors THEN the system SHALL CONTINUE TO return appropriate error codes (`-EBADF`, `0` for zero length) consistent with Linux semantics

3.5 WHEN the kernel boots and initializes the framebuffer at 1920×1080×32bpp THEN the system SHALL CONTINUE TO display the RiduxOS desktop UI (taskbar, wallpaper, icons) without corruption before and after Firefox renders

---

## Bug Condition Pseudocode

### Bug Condition Functions

```pascal
FUNCTION isBugCondition_clone3(X)
  INPUT: X of type SyscallInvocation
  OUTPUT: boolean
  RETURN X.nr = 435  // clone3 syscall number
END FUNCTION

FUNCTION isBugCondition_sendfile(X)
  INPUT: X of type SyscallInvocation
  OUTPUT: boolean
  RETURN X.nr = 187  // sendfile syscall number
END FUNCTION

FUNCTION isBugCondition_cloneNewUser(X)
  INPUT: X of type SyscallInvocation
  OUTPUT: boolean
  RETURN X.nr = 56 AND (X.flags AND 0x10000000) != 0  // clone with CLONE_NEWUSER
END FUNCTION

FUNCTION isBugCondition_framebufferBlit(X)
  INPUT: X of type CompositorEvent
  OUTPUT: boolean
  RETURN X.client = "firefox" AND X.surface_committed = true AND g_backbuffer_updated = false
END FUNCTION

FUNCTION isBugCondition_readOneByte(X)
  INPUT: X of type ReadSyscall
  OUTPUT: boolean
  RETURN X.fd_path = "/opt/firefox/dependentlibs.list" AND X.requested_count > 1 AND X.returned_count = 1
END FUNCTION
```

### Fix Checking Properties

```pascal
// Property: Fix Checking - clone3 handled
FOR ALL X WHERE isBugCondition_clone3(X) DO
  result ← syscall_dispatch'(X)
  ASSERT result > 0  // valid child PID, not ENOSYS
END FOR

// Property: Fix Checking - sendfile implemented
FOR ALL X WHERE isBugCondition_sendfile(X) DO
  result ← syscall_dispatch'(X)
  ASSERT result >= 0  // bytes transferred, not ENOSYS
END FOR

// Property: Fix Checking - clone CLONE_NEWUSER returns -EPERM
FOR ALL X WHERE isBugCondition_cloneNewUser(X) DO
  result ← real_sys_clone'(X)
  ASSERT result = -1 AND errno = EPERM
END FOR

// Property: Fix Checking - framebuffer receives Firefox pixels
FOR ALL X WHERE isBugCondition_framebufferBlit(X) DO
  wl7_render_surfaces'(X)
  ASSERT g_backbuffer_updated = true AND framebuffer_flushed = true
END FOR

// Property: Fix Checking - read returns full count
FOR ALL X WHERE isBugCondition_readOneByte(X) DO
  result ← real_sys_read'(X)
  ASSERT result = MIN(X.requested_count, file_remaining_bytes)
END FOR
```

### Preservation Checking

```pascal
// Property: Preservation Checking
FOR ALL X WHERE NOT isBugCondition_clone3(X)
             AND NOT isBugCondition_sendfile(X)
             AND NOT isBugCondition_cloneNewUser(X)
             AND NOT isBugCondition_framebufferBlit(X)
             AND NOT isBugCondition_readOneByte(X) DO
  ASSERT F(X) = F'(X)  // all other syscall/compositor behavior unchanged
END FOR
```
