# verify-py64.ps1 - 64 位内核 MicroPython REPL 验证脚本 v2
# QEMU 无头模式 + monitor sendkey 注入键盘 + 串口捕获输出
$ErrorActionPreference = "Stop"

$qemu = "C:\Program Files\qemu\qemu-system-x86_64.exe"
if (-not (Test-Path $qemu)) {
    $qemu = (Get-Command qemu-system-x86_64 -ErrorAction SilentlyContinue).Source
}
$dir  = "e:\project\clion\project_system\first\output"
$img  = Join-Path $dir "image.img"
$out  = Join-Path $dir "verify-py64"
$port = 12397

if (Test-Path $out) { Remove-Item $out -Recurse -Force -ErrorAction SilentlyContinue }
New-Item -ItemType Directory -Path $out | Out-Null

$p = Start-Process -FilePath $qemu -ArgumentList @(
    "-drive", "file=$img,format=raw",
    "-display", "none",
    "-monitor", "tcp:127.0.0.1:$port,server,nowait",
    "-serial", "file:$($out)\serial.log",
    "-no-reboot"
) -PassThru -NoNewWindow

function Send-Mon($cmd, $delayMs = 300) {
    $c = New-Object System.Net.Sockets.TcpClient
    $c.Connect("127.0.0.1", $port)
    $s = $c.GetStream()
    $w = New-Object System.IO.StreamWriter($s)
    $w.AutoFlush = $true
    $w.Write($cmd + "`n")
    Start-Sleep -Milliseconds $delayMs
    $c.Close()
}
function Send-Key($k) { Send-Mon "sendkey $k" 150 }
function Shot($name) { Send-Mon "screendump $($out)\$name" 400 }

Write-Host "[1] waiting for boot ..."
Start-Sleep -Seconds 4

Write-Host "[2] login as admin ..."
Send-Key "ret"
Send-Key "a"; Send-Key "d"; Send-Key "m"; Send-Key "i"; Send-Key "n"
Send-Key "ret"
Start-Sleep -Milliseconds 800

Write-Host "[3] open terminal (T) ..."
Send-Key "t"
Start-Sleep -Milliseconds 800

Write-Host "[4] run: python"
Send-Key "p"; Send-Key "y"; Send-Key "t"; Send-Key "h"; Send-Key "o"; Send-Key "n"
Send-Key "ret"
Start-Sleep -Milliseconds 3500

Write-Host "[5] repl: 1+1"
Send-Key "1"; Send-Key "shift-equal"; Send-Key "1"
Send-Key "ret"
Start-Sleep -Milliseconds 1000

Write-Host "[6] repl: 2*3"
Send-Key "2"; Send-Key "shift-8"; Send-Key "3"
Send-Key "ret"
Start-Sleep -Milliseconds 1000

Write-Host "[7] repl: print(2) with parens"
Send-Key "p"; Send-Key "r"; Send-Key "i"; Send-Key "n"; Send-Key "t"
Send-Key "shift-9"; Send-Key "2"; Send-Key "shift-0"
Send-Key "ret"
Start-Sleep -Milliseconds 1000

Write-Host "[8] repl: ctrl-d exit"
Send-Key "ctrl-d"
Start-Sleep -Milliseconds 1000

Write-Host "[9] terminal: exit"
Send-Key "e"; Send-Key "x"; Send-Key "i"; Send-Key "t"
Send-Key "ret"
Start-Sleep -Milliseconds 800
Shot "14_back.png"

Send-Mon "quit" 300
Stop-Process -Id $p.Id -Force -ErrorAction SilentlyContinue
Write-Host "Done. serial log in $out"
