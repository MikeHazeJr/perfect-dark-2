#requires -Version 5.1
<# Read-only caller-desktop preflight. Never launches/focuses a game or switches desktops. #>
[CmdletBinding()]
param([switch] $CompileOnly, [string] $OutputPath = '')
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'lib/Readiness-Output.ps1')
$source = @'
using System;
using System.Diagnostics;
using System.Runtime.InteropServices;
using System.Text;
public static class PdInteractiveReadinessV1 {
    [DllImport("kernel32.dll")] static extern uint GetCurrentThreadId();
    [DllImport("user32.dll")] static extern IntPtr GetThreadDesktop(uint thread);
    [DllImport("user32.dll")] static extern IntPtr GetProcessWindowStation();
    [DllImport("user32.dll", SetLastError=true)] static extern IntPtr OpenInputDesktop(uint flags, bool inherit, uint access);
    [DllImport("user32.dll", SetLastError=true)] static extern bool CloseDesktop(IntPtr desktop);
    [DllImport("user32.dll", CharSet=CharSet.Unicode, SetLastError=true)]
    static extern bool GetUserObjectInformation(IntPtr handle, int index, StringBuilder text, int bytes, out int needed);
    [DllImport("user32.dll")] static extern IntPtr GetForegroundWindow();
    static string Name(IntPtr handle) {
        if (handle == IntPtr.Zero) return "";
        var text = new StringBuilder(256);
        int needed;
        return GetUserObjectInformation(handle, 2, text, text.Capacity * 2, out needed) ? text.ToString() : "";
    }
    public sealed class Result {
        public bool ready;
        public string reason;
        public int caller_session;
        public string caller_desktop;
        public string caller_station;
        public string input_desktop;
        public int input_read_error;
        public bool foreground_present;
        public bool user_interactive;
    }
    public static Result Probe() {
        var result = new Result();
        result.caller_session = Process.GetCurrentProcess().SessionId;
        result.caller_desktop = Name(GetThreadDesktop(GetCurrentThreadId()));
        result.caller_station = Name(GetProcessWindowStation());
        result.user_interactive = Environment.UserInteractive;
        // DESKTOP_READOBJECTS only; no write/switch/hook access requested.
        IntPtr input = OpenInputDesktop(0, false, 0x0001);
        result.input_read_error = input == IntPtr.Zero ? Marshal.GetLastWin32Error() : 0;
        try { result.input_desktop = Name(input); }
        finally { if (input != IntPtr.Zero) CloseDesktop(input); }
        result.foreground_present = GetForegroundWindow() != IntPtr.Zero;
        result.ready = result.user_interactive && result.caller_session > 0 && result.input_read_error == 0
            && String.Equals(result.caller_station, "WinSta0", StringComparison.OrdinalIgnoreCase)
            && String.Equals(result.caller_desktop, "Default", StringComparison.OrdinalIgnoreCase)
            && String.Equals(result.caller_desktop, result.input_desktop, StringComparison.OrdinalIgnoreCase)
            && result.foreground_present;
        result.reason = result.ready ? "ordinary caller desktop prerequisites available; game input ownership remains untested"
            : "ordinary caller desktop prerequisites unavailable; stop without launch or access override";
        return result;
    }
}
'@
$probeAttempted = $false
try {
    if (-not ('PdInteractiveReadinessV1' -as [type])) { Add-Type -TypeDefinition $source }
    if ($CompileOnly) {
        [pscustomobject]@{schema='pd2.interactive-readiness.v1';compiled=$true;probe_run=$false;ready=$false} | ConvertTo-Json
        return
    }
    $probeAttempted = $true
    $result = [PdInteractiveReadinessV1]::Probe()
    $document = [ordered]@{
    schema='pd2.interactive-readiness.v1';utc=[DateTime]::UtcNow.ToString('o')
    probe_run=$true
    ready=$result.ready;reason=$result.reason;caller_session=$result.caller_session
    caller_desktop=$result.caller_desktop;caller_station=$result.caller_station
    input_desktop=$result.input_desktop;input_read_error=$result.input_read_error
    foreground_present=$result.foreground_present;user_interactive=$result.user_interactive
    game_launched=$false;focus_changed=$false;desktop_changed=$false
    }
} catch {
    if ($CompileOnly) { throw }
    $document = [ordered]@{
        schema='pd2.interactive-readiness.v1';utc=[DateTime]::UtcNow.ToString('o')
        ready=$false;probe_run=$probeAttempted;reason='Readiness check could not complete; stop without launch or access override'
        error_message=[string]$_.Exception.Message
        game_launched=$false;focus_changed=$false;desktop_changed=$false
    }
}
$saved = Save-PdReadinessOutput -Document $document -OutputPath $OutputPath
Write-Output $saved.Json
Write-Host ('Readiness JSON saved to: ' + $saved.OutputPath)
if (-not $document['ready']) { exit 2 }
