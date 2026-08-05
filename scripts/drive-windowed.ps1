<#
.SYNOPSIS
    Drives the REAL WINDOWED BUILD with real input, and photographs what it did.

.DESCRIPTION
    WHY THIS EXISTS.

    Every sprint before #77 verified this game with `--smoke=N --screenshot=PATH`:
    the client runs N movement steps with a scripted MoveInput, writes a PNG and
    exits without ever opening a window. That proves the renderer and it proves
    the simulation. It cannot prove ANY of the things Eli complained about --
    input latency, camera smoothness, whether a modifier feels like a modifier,
    whether walking into a wall climbs it -- because none of them exist in a
    still frame. That gap is exactly how the archaic feel shipped, sprint after
    sprint, past a green gate.

    So this opens the game, gives the window the keyboard and the mouse, presses
    real keys through SendInput -- scancodes, which is what SDL reads -- moves
    the mouse with real RELATIVE motion, screenshots the window, and reads the
    body's own final position out of the process's stdout.

    It is not a substitute for playing it. It is the thing that makes "I played
    it and it felt like X" checkable by somebody else afterwards.

.PARAMETER Script
    A comma-separated list of beats. Each beat is one of:

        w:MS         hold forward for MS milliseconds
        s:MS         hold back
        a:MS / d:MS  strafe
        shift:MS     hold sprint while moving (combine: "w+shift:1200")
        ctrl:MS      hold crouch
        alt:MS       hold walk
        tap:KEY      press and release KEY (space, e, q, tab, f1, f2, v, x)
        look:DX,DY   move the mouse DX,DY counts, relative
        turn:DEG     look, in degrees, using the shipped sensitivity
        wait:MS      do nothing
        shot:NAME    capture the window to docs/frames/NAME.png

.EXAMPLE
    powershell -ExecutionPolicy Bypass -File .\scripts\drive-windowed.ps1 `
        -Script "wait:1500,shot:p77-spawn,w+shift:2000,shot:p77-sprinted,turn:180,shot:p77-turned"
#>
[CmdletBinding()]
param(
    [string]$Exe = ".\dist\granadad.exe",
    [string]$ExeArgs = "--width=960 --height=540 --scale=1 --time=10",
    [Parameter(Mandatory = $true)][string]$Script,
    [string]$OutDir = "docs\frames\p77-controls",
    [int]$BootMs = 2500
)

$ErrorActionPreference = "Stop"
Add-Type -AssemblyName System.Drawing
Add-Type -AssemblyName System.Windows.Forms
$repo = Split-Path -Parent $PSScriptRoot
Set-Location $repo

# ---------------------------------------------------------------------------
# Win32. SendInput, not SendKeys.
# ---------------------------------------------------------------------------
#
# SendKeys posts WM_CHAR-level messages and produces no scancode and no relative
# mouse motion at all, so a game that reads SDL scancodes and relative mouse
# deltas -- which is to say, this one -- sees nothing from it. SendInput is the
# same path a real keyboard and a real mouse go down.
$signature = @'
using System;
using System.Runtime.InteropServices;

public static class Drive {
    [StructLayout(LayoutKind.Sequential)]
    public struct MOUSEINPUT { public int dx; public int dy; public uint mouseData;
                               public uint dwFlags; public uint time; public IntPtr dwExtraInfo; }
    [StructLayout(LayoutKind.Sequential)]
    public struct KEYBDINPUT { public ushort wVk; public ushort wScan; public uint dwFlags;
                               public uint time; public IntPtr dwExtraInfo; }
    [StructLayout(LayoutKind.Explicit)]
    public struct INPUTUNION { [FieldOffset(0)] public MOUSEINPUT mi;
                               [FieldOffset(0)] public KEYBDINPUT ki; }
    [StructLayout(LayoutKind.Sequential)]
    public struct INPUT { public uint type; public INPUTUNION u; }

    public const uint INPUT_MOUSE = 0;
    public const uint INPUT_KEYBOARD = 1;
    public const uint KEYEVENTF_SCANCODE = 0x0008;
    public const uint KEYEVENTF_KEYUP = 0x0002;
    public const uint KEYEVENTF_EXTENDEDKEY = 0x0001;
    public const uint MOUSEEVENTF_MOVE = 0x0001;

    public const uint MOUSEEVENTF_ABSOLUTE = 0x8000;
    public const uint MOUSEEVENTF_LEFTDOWN = 0x0002;
    public const uint MOUSEEVENTF_LEFTUP = 0x0004;

    [DllImport("user32.dll", SetLastError = true)]
    public static extern uint SendInput(uint nInputs, INPUT[] pInputs, int cbSize);
    [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr hWnd);
    [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr hWnd, out RECT r);
    [DllImport("user32.dll")] public static extern IntPtr GetForegroundWindow();
    [DllImport("user32.dll")] public static extern bool ShowWindow(IntPtr hWnd, int nCmdShow);
    [DllImport("user32.dll")] public static extern bool BringWindowToTop(IntPtr hWnd);
    [DllImport("user32.dll")] public static extern bool SetCursorPos(int x, int y);
    [DllImport("user32.dll")] public static extern bool AttachThreadInput(uint a, uint b, bool f);
    [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr h, IntPtr p);
    [DllImport("kernel32.dll")] public static extern uint GetCurrentThreadId();
    [DllImport("user32.dll")] public static extern bool SetProcessDPIAware();

    /// WINDOWS WILL NOT SIMPLY GIVE A BACKGROUND PROCESS THE FOREGROUND, and a
    /// harness that types into whatever happens to be in front instead of into
    /// the game is worse than no harness -- the first run of this opened the
    /// Start menu and photographed it. So: tap ALT (which is the documented way
    /// to earn the right to change the foreground), attach to the target's
    /// input queue, and then ask. The caller still verifies afterwards and
    /// refuses to send anything if it did not work.
    public static void ForceForeground(IntPtr hWnd) {
        INPUT[] alt = new INPUT[2];
        alt[0].type = INPUT_KEYBOARD;
        alt[0].u.ki.wScan = 0x38;
        alt[0].u.ki.dwFlags = KEYEVENTF_SCANCODE;
        alt[1] = alt[0];
        alt[1].u.ki.dwFlags = KEYEVENTF_SCANCODE | KEYEVENTF_KEYUP;
        SendInput(2, alt, Marshal.SizeOf(typeof(INPUT)));

        uint us = GetCurrentThreadId();
        uint them = GetWindowThreadProcessId(hWnd, IntPtr.Zero);
        AttachThreadInput(us, them, true);
        ShowWindow(hWnd, 9);       // SW_RESTORE
        BringWindowToTop(hWnd);
        SetForegroundWindow(hWnd);
        AttachThreadInput(us, them, false);
    }

    public static void ClickAbsolute(int x, int y) {
        SetCursorPos(x, y);
        INPUT[] click = new INPUT[2];
        click[0].type = INPUT_MOUSE;
        click[0].u.mi.dwFlags = MOUSEEVENTF_LEFTDOWN;
        click[1] = click[0];
        click[1].u.mi.dwFlags = MOUSEEVENTF_LEFTUP;
        SendInput(2, click, Marshal.SizeOf(typeof(INPUT)));
    }

    [StructLayout(LayoutKind.Sequential)]
    public struct RECT { public int Left; public int Top; public int Right; public int Bottom; }

    public static void Key(ushort scan, bool down, bool extended) {
        INPUT[] input = new INPUT[1];
        input[0].type = INPUT_KEYBOARD;
        input[0].u.ki.wScan = scan;
        input[0].u.ki.dwFlags = KEYEVENTF_SCANCODE
                              | (down ? 0u : KEYEVENTF_KEYUP)
                              | (extended ? KEYEVENTF_EXTENDEDKEY : 0u);
        SendInput(1, input, Marshal.SizeOf(typeof(INPUT)));
    }

    public static void MoveRelative(int dx, int dy) {
        INPUT[] input = new INPUT[1];
        input[0].type = INPUT_MOUSE;
        input[0].u.mi.dx = dx;
        input[0].u.mi.dy = dy;
        input[0].u.mi.dwFlags = MOUSEEVENTF_MOVE;
        SendInput(1, input, Marshal.SizeOf(typeof(INPUT)));
    }
}
'@
Add-Type -TypeDefinition $signature -Language CSharp

# PS/2 set-1 scancodes. What the hardware sends and what SDL turns back into
# SDL_SCANCODE_*.
$scan = @{
    'w' = 0x11; 'a' = 0x1E; 's' = 0x1F; 'd' = 0x20; 'e' = 0x12; 'q' = 0x10
    'r' = 0x13; 'f' = 0x21; 'g' = 0x22; 't' = 0x14; 'v' = 0x2F; 'x' = 0x2D
    'c' = 0x2E; 'j' = 0x24; 'z' = 0x2C
    'space' = 0x39; 'tab' = 0x0F; 'esc' = 0x01; 'enter' = 0x1C
    'shift' = 0x2A; 'ctrl' = 0x1D; 'alt' = 0x38
    'f1' = 0x3B; 'f2' = 0x3C; 'f3' = 0x3D; 'f12' = 0x58
    '1' = 0x02; '2' = 0x03; '3' = 0x04; '0' = 0x0B
    'left' = 0x4B; 'right' = 0x4D; 'up' = 0x48; 'down' = 0x50
}
$extended = @('left', 'right', 'up', 'down')

function Send-Key([string]$name, [bool]$down) {
    if (-not $scan.ContainsKey($name)) { throw "no scancode for '$name'" }
    [Drive]::Key([uint16]$scan[$name], $down, ($extended -contains $name))
}

# ---------------------------------------------------------------------------
# start it
# ---------------------------------------------------------------------------
$null = New-Item -ItemType Directory -Force -Path $OutDir
$stdout = Join-Path $env:TEMP "granadad-drive-stdout.txt"
$proc = Start-Process -FilePath $Exe -ArgumentList $ExeArgs -PassThru `
                      -RedirectStandardOutput $stdout
Write-Host "started pid $($proc.Id), waiting $BootMs ms for the world to load"
Start-Sleep -Milliseconds $BootMs

$proc.Refresh()
if ($proc.HasExited) {
    Write-Host "FAILED: the game exited during boot. stdout:"
    Get-Content $stdout
    exit 1
}
$hwnd = $proc.MainWindowHandle
if ($hwnd -eq [IntPtr]::Zero) { throw "the game has no window" }

$rect = New-Object Drive+RECT
[void][Drive]::GetWindowRect($hwnd, [ref]$rect)
$w = $rect.Right - $rect.Left
$h = $rect.Bottom - $rect.Top
Write-Host "window $w x $h at $($rect.Left),$($rect.Top)"

# FOCUS, OR NOTHING. A harness that keeps typing after the window has lost the
# foreground is a harness that types into somebody else's application; the first
# version of this opened the Start menu and photographed it, which is funny once.
[Drive]::ForceForeground($hwnd)
Start-Sleep -Milliseconds 500
if ([Drive]::GetForegroundWindow() -ne $hwnd) {
    Write-Host "  not foreground yet -- clicking the window"
    [Drive]::ClickAbsolute(($rect.Left + [int]($w / 2)), ($rect.Top + [int]($h / 2)))
    Start-Sleep -Milliseconds 500
    [Drive]::ForceForeground($hwnd)
    Start-Sleep -Milliseconds 500
}
if ([Drive]::GetForegroundWindow() -ne $hwnd) {
    Write-Host "REFUSING TO DRIVE: the game will not take the foreground, so every"
    Write-Host "key below would land in another application. Nothing was sent."
    if (-not $proc.HasExited) { $proc.Kill() }
    exit 2
}
Write-Host "the game has the keyboard and the mouse"

# THE GAME'S OWN SHUTTER, NOT A SCREEN GRAB.
#
# F12 is bound to a screenshot inside the game and writes its own framebuffer,
# so what comes out is exactly the pixels the renderer produced -- no window
# chrome, no DPI scaling, no compositor. The first version of this took a
# CopyFromScreen of the window rect and got the title bar and half the desktop,
# because the window rect is in physical pixels and a DPI-unaware PowerShell
# reads it in logical ones. Asking the game to photograph itself sidesteps the
# whole question.
function Capture([string]$name) {
    $shot = Join-Path $repo "granadad-screenshot.png"
    if (Test-Path $shot) { Remove-Item $shot -Force }
    Send-Key 'f12' $true
    Start-Sleep -Milliseconds 80
    Send-Key 'f12' $false
    Start-Sleep -Milliseconds 700
    $path = Join-Path $OutDir "$name.png"
    if (Test-Path $shot) {
        Move-Item $shot $path -Force
        Write-Host "  shot -> $path"
    } else {
        Write-Host "  NO FRAME WRITTEN -- the game did not take the F12 press"
    }
}

# The shipped default sensitivity, so `turn:DEG` means degrees. 65536 BAM is a
# full turn and the mouse contributes `counts * sensitivity` BAM.
$sensitivity = 14

# ---------------------------------------------------------------------------
# play it
# ---------------------------------------------------------------------------
foreach ($beat in $Script.Split(',')) {
    $beat = $beat.Trim()
    if ($beat -eq '') { continue }
    $verb, $arg = $beat.Split(':', 2)
    Write-Host "beat: $beat"
    # CHECKED EVERY BEAT, not once at the start. A window that loses focus half
    # way through a run -- a notification, a stray click -- would otherwise send
    # the rest of the script into whatever took it.
    if ([Drive]::GetForegroundWindow() -ne $hwnd) {
        Write-Host "  LOST FOCUS. Stopping here rather than typing into something else."
        break
    }
    switch -Regex ($verb) {
        '^shot$' { Capture $arg; continue }
        '^wait$' { Start-Sleep -Milliseconds ([int]$arg); continue }
        '^tap$'  {
            Send-Key $arg $true
            Start-Sleep -Milliseconds 60
            Send-Key $arg $false
            Start-Sleep -Milliseconds 250
            continue
        }
        '^look$' {
            $dx, $dy = $arg.Split(',')
            # In small steps, the way a hand moves, so the game sees a stream of
            # relative deltas rather than one teleport.
            $steps = 20
            for ($i = 0; $i -lt $steps; $i++) {
                [Drive]::MoveRelative([int]([int]$dx / $steps), [int]([int]$dy / $steps))
                Start-Sleep -Milliseconds 8
            }
            continue
        }
        '^turn$' {
            $counts = [int](([double]$arg / 360.0) * 65536.0 / $sensitivity)
            $steps = 30
            for ($i = 0; $i -lt $steps; $i++) {
                [Drive]::MoveRelative([int]($counts / $steps), 0)
                Start-Sleep -Milliseconds 8
            }
            continue
        }
        default {
            # One or more held keys joined with '+', for MS milliseconds.
            $keys = $verb.Split('+')
            foreach ($k in $keys) { Send-Key $k $true }
            Start-Sleep -Milliseconds ([int]$arg)
            foreach ($k in $keys) { Send-Key $k $false }
            Start-Sleep -Milliseconds 120
        }
    }
}

# ---------------------------------------------------------------------------
# stop it, and read what the body says it did
# ---------------------------------------------------------------------------
Send-Key 'esc' $true; Start-Sleep -Milliseconds 60; Send-Key 'esc' $false
Start-Sleep -Milliseconds 900
if (-not $proc.HasExited) {
    # ESC backs out of a page before it quits, so a second one may be needed --
    # which is itself worth knowing.
    Send-Key 'esc' $true; Start-Sleep -Milliseconds 60; Send-Key 'esc' $false
    Start-Sleep -Milliseconds 900
}
if (-not $proc.HasExited) {
    Write-Host "the game did not quit on ESC; closing it"
    $proc.CloseMainWindow() | Out-Null
    Start-Sleep -Milliseconds 800
    if (-not $proc.HasExited) { $proc.Kill() }
}
Start-Sleep -Milliseconds 300
Write-Host "--- the game's own stdout ---"
Get-Content $stdout
