Add-Type -AssemblyName UIAutomationClient
Add-Type -AssemblyName UIAutomationTypes
Add-Type -AssemblyName System.Drawing
Add-Type @"
using System;
using System.Runtime.InteropServices;
public class WinPos {
  [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr hWnd, out RECT lpRect);
  [DllImport("user32.dll")] public static extern bool SetWindowPos(IntPtr hWnd, IntPtr hWndInsertAfter, int X, int Y, int cx, int cy, uint uFlags);
  public struct RECT { public int Left; public int Top; public int Right; public int Bottom; }
}
"@

$ErrorActionPreference = "Stop"
$root = [System.Windows.Automation.AutomationElement]::RootElement
$figure = "d:\NetWork\lab1\figure"
New-Item -ItemType Directory -Force -Path $figure | Out-Null

function Find-Window([int]$processId) {
    $deadline = (Get-Date).AddSeconds(15)
    do {
        $cond = New-Object System.Windows.Automation.PropertyCondition(
            [System.Windows.Automation.AutomationElement]::ProcessIdProperty, $processId)
        $win = $root.FindFirst([System.Windows.Automation.TreeScope]::Children, $cond)
        if ($win) { return $win }
        Start-Sleep -Milliseconds 300
    } while ((Get-Date) -lt $deadline)
    throw "window for pid $processId not found"
}

function Find-Id($parent, [string]$id) {
    $cond = New-Object System.Windows.Automation.PropertyCondition(
        [System.Windows.Automation.AutomationElement]::AutomationIdProperty, $id)
    $el = $parent.FindFirst([System.Windows.Automation.TreeScope]::Descendants, $cond)
    if (-not $el) { throw "missing $id" }
    return $el
}

function Set-Value($el, [string]$text) {
    $id = $el.Current.AutomationId
    foreach ($patternId in @(
            [System.Windows.Automation.ValuePattern]::Pattern,
            [System.Windows.Automation.RangeValuePattern]::Pattern)) {
        if (($el.GetSupportedPatterns() | Where-Object { $_.Id -eq $patternId.Id })) {
            $pattern = $el.GetCurrentPattern($patternId)
            if ($pattern -is [System.Windows.Automation.RangeValuePattern]) {
                $pattern.SetValue([double]$text)
            } else {
                $pattern.SetValue($text)
            }
            return
        }
    }
    throw "cannot set $id"
}

function Wait-Enabled($el, [int]$seconds) {
    $deadline = (Get-Date).AddSeconds($seconds)
    do {
        if ($el.Current.IsEnabled) { return }
        Start-Sleep -Milliseconds 200
    } while ((Get-Date) -lt $deadline)
    throw "$($el.Current.AutomationId) stayed disabled"
}

function Invoke-Button($el) {
    $pattern = $el.GetCurrentPattern([System.Windows.Automation.InvokePattern]::Pattern)
    $pattern.Invoke()
}

function Element-Text($el) {
    $names = New-Object System.Collections.Generic.List[string]
    if ($el.Current.Name) { $names.Add($el.Current.Name) }
    $desc = $el.FindAll([System.Windows.Automation.TreeScope]::Descendants, [System.Windows.Automation.Condition]::TrueCondition)
    foreach ($child in $desc) {
        $name = $child.Current.Name
        if ($name) { $names.Add($name) }
    }
    return ($names -join " | ")
}

function Wait-Text($el, [string]$needle, [int]$seconds) {
    $deadline = (Get-Date).AddSeconds($seconds)
    do {
        $name = Element-Text $el
        if ($name.Contains($needle)) { return $name }
        Start-Sleep -Milliseconds 200
    } while ((Get-Date) -lt $deadline)
    return (Element-Text $el)
}

function Save-Window($process, [string]$path) {
    $hwnd = $process.MainWindowHandle
    $rect = New-Object WinPos+RECT
    [WinPos]::GetWindowRect($hwnd, [ref]$rect) | Out-Null
    $width = $rect.Right - $rect.Left
    $height = $rect.Bottom - $rect.Top
    $bmp = New-Object System.Drawing.Bitmap $width, $height
    $graphics = [System.Drawing.Graphics]::FromImage($bmp)
    $graphics.CopyFromScreen($rect.Left, $rect.Top, 0, 0, $bmp.Size)
    $bmp.Save($path, [System.Drawing.Imaging.ImageFormat]::Png)
    $graphics.Dispose()
    $bmp.Dispose()
}

function Place($process, [int]$x, [int]$y, [int]$w, [int]$h) {
    [WinPos]::SetWindowPos($process.MainWindowHandle, [IntPtr]::Zero, $x, $y, $w, $h, 0x0040) | Out-Null
}

function Show-Front($process) {
    Add-Type -Namespace Win -Name Fg -MemberDefinition '[DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr hWnd);' -ErrorAction SilentlyContinue
    [Win.Fg]::SetForegroundWindow($process.MainWindowHandle) | Out-Null
    Place $process 80 40 1040 760
    Start-Sleep -Milliseconds 400
}

$server = Start-Process "d:\NetWork\lab1\bin\chat_server\chat_server.exe" -WorkingDirectory "d:\NetWork\lab1\bin\chat_server" -PassThru
$serverWindow = Find-Window $server.Id
Invoke-Button (Find-Id $serverWindow "StartButton")
Start-Sleep -Seconds 1

$clients = @()
foreach ($nick in @("张三", "Alice", "李四")) {
    $proc = Start-Process "d:\NetWork\lab1\bin\chat_client\chat_client.exe" -WorkingDirectory "d:\NetWork\lab1\bin\chat_client" -PassThru
    $win = Find-Window $proc.Id
    Set-Value (Find-Id $win "NickBox") $nick
    Invoke-Button (Find-Id $win "ConnectButton")
    Wait-Enabled (Find-Id $win "InputBox") 8
    $clients += [pscustomobject]@{ Name = $nick; Process = $proc; Window = $win }
    Start-Sleep -Milliseconds 600
}

$zhang = $clients[0]
$alice = $clients[1]
$li = $clients[2]
Write-Output "connected"
Set-Value (Find-Id $zhang.Window "InputBox") "Hello"
Invoke-Button (Find-Id $zhang.Window "SendButton")
Start-Sleep -Milliseconds 300
Set-Value (Find-Id $zhang.Window "InputBox") "你好，晚上见"
Invoke-Button (Find-Id $zhang.Window "SendButton")
$aliceChat = Wait-Text (Find-Id $alice.Window "MessageList") "你好，晚上见" 8
Set-Value (Find-Id $alice.Window "InputBox") "Good evening 晚上好"
Invoke-Button (Find-Id $alice.Window "SendButton")
$zhangChat = Wait-Text (Find-Id $zhang.Window "MessageList") "晚上好" 8
Set-Value (Find-Id $alice.Window "BurstBox") "20"
Invoke-Button (Find-Id $alice.Window "BurstButton")
$zhangStatus = Wait-Text (Find-Id $zhang.Window "StatusText") "连续发送收到 20/20" 10
Invoke-Button (Find-Id $li.Window "ListButton")
$liChat = Wait-Text (Find-Id $li.Window "MessageList") "在线用户" 8

Show-Front $server
Save-Window $server "$figure\server.png"
Show-Front $zhang.Process
Save-Window $zhang.Process "$figure\client-zhang.png"
Show-Front $alice.Process
Save-Window $alice.Process "$figure\client-alice.png"
Show-Front $li.Process
Save-Window $li.Process "$figure\client-li.png"

Invoke-Button (Find-Id $zhang.Window "DisconnectButton")
$leave = Wait-Text (Find-Id $alice.Window "MessageList") "张三 离开了聊天" 8
Start-Sleep -Milliseconds 400
Show-Front $alice.Process
Save-Window $alice.Process "$figure\client-leave.png"
Show-Front $server
Save-Window $server "$figure\server-after-leave.png"

"ALICE_LIST=$aliceChat"
"ZHANG_CHAT=$zhangChat"
"ZHANG_STATUS=$zhangStatus"
"LI_LIST=$liChat"
"LEAVE=$leave"

Get-Process chat_client, chat_server -ErrorAction SilentlyContinue | Stop-Process -Force
