# verify-term.ps1 - 临时验证脚本: QEMU 无头模式启动内核, 模拟登录并测试终端
$ErrorActionPreference = "Stop"

$qemu = "C:\Program Files\qemu\qemu-system-i386.exe"
$dir  = "e:\project\clion\project_system\first\output"
$img  = Join-Path $dir "image.img"
$out  = Join-Path $dir "verify"
$port = 12399

if (Test-Path $out) { Remove-Item $out -Recurse -Force }
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
function Send-Key($k) { Send-Mon "sendkey $k" 180 }
function Shot($name) { Send-Mon "screendump $($out)\$name" 500 }

Write-Host "[1/8] waiting for boot ..."
Start-Sleep -Seconds 4
Shot "01_login.png"

Write-Host "[2/8] login as admin ..."
Send-Key "ret"                       # 选择 admin
Send-Key "a"; Send-Key "d"; Send-Key "m"; Send-Key "i"; Send-Key "n"
Send-Key "ret"
Start-Sleep -Milliseconds 800
Shot "02_main.png"

Write-Host "[3/8] open terminal ..."
Send-Key "t"
Shot "03_terminal.png"

Write-Host "[4/8] run: help"
Send-Key "h"; Send-Key "e"; Send-Key "l"; Send-Key "p"
Send-Key "ret"
Shot "04_help.png"

Write-Host "[5/8] run: whoami"
Send-Key "w"; Send-Key "h"; Send-Key "o"; Send-Key "a"; Send-Key "m"; Send-Key "i"
Send-Key "ret"
Shot "05_whoami.png"

Write-Host "[6/8] run: users"
Send-Key "u"; Send-Key "s"; Send-Key "e"; Send-Key "r"; Send-Key "s"
Send-Key "ret"
Shot "06_users.png"

Write-Host "[7/8] run: meminfo"
Send-Key "m"; Send-Key "e"; Send-Key "m"; Send-Key "i"; Send-Key "n"; Send-Key "f"; Send-Key "o"
Send-Key "ret"
Shot "07_meminfo.png"

Write-Host "[8/8] exit terminal ..."
Send-Key "e"; Send-Key "x"; Send-Key "i"; Send-Key "t"
Send-Key "ret"
Shot "08_main_back.png"

Send-Mon "quit" 300
Stop-Process -Id $p.Id -Force -ErrorAction SilentlyContinue
Write-Host "Done. screenshots in $out"
