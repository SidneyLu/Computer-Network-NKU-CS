Add-Type -AssemblyName UIAutomationClient
Add-Type -AssemblyName UIAutomationTypes
Add-Type -AssemblyName System.Drawing
Add-Type @"
using System;
using System.Runtime.InteropServices;
public class WinPos {
  [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr hWnd, out RECT lpRect);
  [DllImport("user32.dll")] public static extern bool SetWindowPos(IntPtr hWnd, IntPtr hWndInsertAfter, int X, int Y, int cx, int cy, uint uFlags);
  [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr hWnd);
  public struct RECT { public int Left; public int Top; public int Right; public int Bottom; }
}
"@

$ErrorActionPreference = "Stop"
$root = [System.Windows.Automation.AutomationElement]::RootElement
$figure = "d:\NetWork\lab2\figure"
New-Item -ItemType Directory -Force -Path $figure | Out-Null

function Find-Window([int]$processId) {
    $deadline = (Get-Date).AddSeconds(20)
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
    $deadline = (Get-Date).AddSeconds(8)
    do {
        $el = $parent.FindFirst([System.Windows.Automation.TreeScope]::Descendants, $cond)
        if ($el) { return $el }
        Start-Sleep -Milliseconds 200
    } while ((Get-Date) -lt $deadline)
    throw "missing $id"
}

function Set-Value($el, [string]$text) {
    $pattern = $el.GetCurrentPattern([System.Windows.Automation.ValuePattern]::Pattern)
    $pattern.SetValue($text)
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

function Save-Window($process, [string]$path) {
    $hwnd = $process.MainWindowHandle
    $rect = New-Object WinPos+RECT
    [WinPos]::GetWindowRect($hwnd, [ref]$rect) | Out-Null
    $width = [Math]::Max(1, $rect.Right - $rect.Left)
    $height = [Math]::Max(1, $rect.Bottom - $rect.Top)
    $bmp = New-Object System.Drawing.Bitmap $width, $height
    $graphics = [System.Drawing.Graphics]::FromImage($bmp)
    $graphics.CopyFromScreen($rect.Left, $rect.Top, 0, 0, $bmp.Size)
    $bmp.Save($path, [System.Drawing.Imaging.ImageFormat]::Png)
    $graphics.Dispose()
    $bmp.Dispose()
}

function Show-Front($process) {
    [WinPos]::SetForegroundWindow($process.MainWindowHandle) | Out-Null
    [WinPos]::SetWindowPos($process.MainWindowHandle, [IntPtr]::Zero, 20, 20, 1680, 980, 0x0040) | Out-Null
    Start-Sleep -Milliseconds 500
}

$proc = Start-Process "d:\NetWork\lab2\bin\capture\capture.exe" -WorkingDirectory "d:\NetWork\lab2\bin\capture" -PassThru
$win = Find-Window $proc.Id
Show-Front $proc
Start-Sleep -Milliseconds 800
$status = Find-Id $win "StatusText"
$combo = Find-Id $win "DeviceBox"
$expand = $combo.GetCurrentPattern([System.Windows.Automation.ExpandCollapsePattern]::Pattern)
$expand.Expand()
Start-Sleep -Milliseconds 400
$itemCond = New-Object System.Windows.Automation.PropertyCondition(
    [System.Windows.Automation.AutomationElement]::ControlTypeProperty,
    [System.Windows.Automation.ControlType]::ListItem)
$items = $combo.FindAll([System.Windows.Automation.TreeScope]::Descendants, $itemCond)
if ($items.Count -eq 0) {
    $items = $root.FindAll([System.Windows.Automation.TreeScope]::Descendants, $itemCond)
}
$names = @()
foreach ($item in $items) {
    $name = $item.Current.Name
    if ($name) { $names += $name }
}
"STATUS=$(Element-Text $status)"
"DEVICES=$($names -join ' || ')"

$preferred = @()
$fallback = @()
foreach ($item in $items) {
    $name = $item.Current.Name
    if (-not $name) { continue }
    if ($name -match 'Loopback|回环|WAN Miniport|Filter|QoS|Virtual|Bluetooth|蓝牙') {
        $fallback += $item
    } else {
        $preferred += $item
    }
}
$ordered = @($preferred + $fallback)
$ping = Start-Process ping -ArgumentList "-n","40","1.1.1.1" -WindowStyle Hidden -PassThru
$captured = $false
$packetText = ""
foreach ($item in $ordered) {
    $select = $item.GetCurrentPattern([System.Windows.Automation.SelectionItemPattern]::Pattern)
    $select.Select()
    $expand.Collapse()
    Start-Sleep -Milliseconds 200
    Set-Value (Find-Id $win "CountBox") "12"
    Invoke-Button (Find-Id $win "StartButton")
    $deadline = (Get-Date).AddSeconds(6)
    do {
        $packetText = Element-Text (Find-Id $win "PacketList")
        if ($packetText -match "0x") { break }
        Start-Sleep -Milliseconds 300
    } while ((Get-Date) -lt $deadline)
    if ($packetText -match "0x") {
        "SELECTED=$($item.Current.Name)"
        $captured = $true
        break
    }
    "SKIP=$($item.Current.Name) TEXT=$packetText STATUS=$(Element-Text $status)"
    if ((Find-Id $win "StopButton").Current.IsEnabled) {
        Invoke-Button (Find-Id $win "StopButton")
        Start-Sleep -Milliseconds 400
    }
}
if (-not $captured) { throw "no packets captured" }
Show-Front $proc
Start-Sleep -Milliseconds 600
Save-Window $proc "$figure\capture.png"
$before = (Element-Text (Find-Id $win "PacketList"))
$stop = Find-Id $win "StopButton"
if ($stop.Current.IsEnabled) {
    Invoke-Button $stop
    Start-Sleep -Seconds 2
}
$after = Element-Text (Find-Id $win "PacketList")
$finalStatus = Element-Text (Find-Id $win "StatusText")
$startEnabled = (Find-Id $win "StartButton").Current.IsEnabled
"PACKETS=$before"
"STATUS_AFTER=$finalStatus"
"START_ENABLED=$startEnabled"
"LIST_STABLE=$($before -eq $after)"
if ($ping -and -not $ping.HasExited) { Stop-Process -Id $ping.Id -Force -ErrorAction SilentlyContinue }
Stop-Process -Id $proc.Id -Force
