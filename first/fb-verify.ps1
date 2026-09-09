# fb-verify.ps1 - 从 QEMU monitor 直接转储 0xA0000 帧缓冲并重建 320x200 PNG
$ErrorActionPreference = "Stop"
Add-Type -AssemblyName System.Drawing

$qemu = "C:\Program Files\qemu\qemu-system-i386.exe"
$dir  = "e:\project\clion\project_system\first\output"
$img  = Join-Path $dir "image.img"
$out  = Join-Path $dir "fbverify"
$port = 12400

if (Test-Path $out) { Remove-Item $out -Recurse -Force }
New-Item -ItemType Directory -Path $out | Out-Null

$p = Start-Process -FilePath $qemu -ArgumentList @(
    "-drive", "file=$img,format=raw",
    "-display", "none",
    "-accel", "tcg,tb-size=32",
    "-monitor", "tcp:127.0.0.1:$port,server,nowait",
    "-serial", "file:$($out)\serial.log",
    "-no-reboot"
) -PassThru -NoNewWindow

function Strip-Ansi($s) { return [regex]::Replace($s, "\e\[[0-9;]*[A-Za-z]|[\r\x07]", "") }

function Dump-FB() {
    $c = New-Object System.Net.Sockets.TcpClient
    $c.Connect("127.0.0.1", $port)
    $s = $c.GetStream()
    $r = New-Object System.IO.StreamReader($s)
    $w = New-Object System.IO.StreamWriter($s)
    $w.AutoFlush = $true

    # consume banner
    Start-Sleep -Milliseconds 200
    while ($s.DataAvailable) { [void]$s.ReadByte() }

    $w.WriteLine("xp /64000bx 0xa0000")
    Start-Sleep -Milliseconds 1200
    $sb = New-Object System.Text.StringBuilder
    $deadline = (Get-Date).AddSeconds(3)
    while ((Get-Date) -lt $deadline) {
        while ($s.DataAvailable) { [void]$sb.Append([char]$s.ReadByte()) }
        Start-Sleep -Milliseconds 100
    }
    Start-Sleep -Milliseconds 200
    $c.Close()
    return (Strip-Ansi $sb.ToString())
}

function Save-Png($pixels, $path) {
    $w = 320; $h = 200
    $bmp = New-Object System.Drawing.Bitmap($w, $h)
    for ($y = 0; $y -lt $h; $y++) {
        for ($x = 0; $x -lt $w; $x++) {
            $c = $palette[$pixels[$y * $w + $x]]
            $bmp.SetPixel($x, $y, [System.Drawing.Color]::FromArgb($c[0], $c[1], $c[2]))
        }
    }
    $bmp.Save($path, [System.Drawing.Imaging.ImageFormat]::Png)
    $bmp.Dispose()
}

# palette from vga.c (6-bit -> 8-bit)
$palette = New-Object 'byte[][]' 256
for ($i = 0; $i -lt 256; $i++) { $palette[$i] = @(0,0,0) }
function SetPal($i,$r,$g,$b) { $palette[$i] = @([byte]([Math]::Min(63,$r)*255/63), [byte]([Math]::Min(63,$g)*255/63), [byte]([Math]::Min(63,$b)*255/63)) }
SetPal 0  0  0  0
SetPal 1  0  0 40
SetPal 2  0 40  0
SetPal 3  0 40 40
SetPal 4 40  0  0
SetPal 5 40  0 40
SetPal 6 40 40  0
SetPal 7 36 36 36
SetPal 8 16 16 16
SetPal 9  0  0 63
SetPal 10 0 63  0
SetPal 11 0 63 63
SetPal 12 63  0  0
SetPal 13 63  0 63
SetPal 14 63 63  0
SetPal 15 63 63 63
SetPal 16 24 28 40
SetPal 17 36 72 110
SetPal 18 16 20 36
SetPal 19 63 40  0
SetPal 20 0 36 28

function Send-Mon($cmd) {
    $c = New-Object System.Net.Sockets.TcpClient
    $c.Connect("127.0.0.1", $port)
    $s = $c.GetStream()
    $w = New-Object System.IO.StreamWriter($s)
    $w.AutoFlush = $true
    $w.WriteLine($cmd)
    Start-Sleep -Milliseconds 200
    $c.Close()
}
function Send-Key($k) { Send-Mon "sendkey $k" }

function Shot($name) {
    Start-Sleep -Milliseconds 300
    $txt = Dump-FB
    [regex]::Matches($txt, "[0-9a-fA-F]{8}:\s*((?:0x[0-9a-fA-F]{2}\s+)+)") | ForEach-Object {
        $_.Groups[1].Value -split "\s+" | Where-Object { $_ -match "^0x" } | ForEach-Object { [Convert]::ToByte($_.Substring(2), 16) }
    } | Set-Content -Path "$out\$name.bin" -Encoding Byte
    $bytes = [System.IO.File]::ReadAllBytes("$out\$name.bin")
    if ($bytes.Length -lt 64000) { throw "FB dump incomplete: $($bytes.Length)" }
    Save-Png $bytes "$out\$name.png"
    Write-Host "captured $name.png ($($bytes.Length) bytes)"
}

Write-Host "[1/9] boot and login screen..."
Start-Sleep -Seconds 5
Shot "01_login"

Write-Host "[2/9] login as admin ..."
Send-Key "ret"
Send-Key "a"; Send-Key "d"; Send-Key "m"; Send-Key "i"; Send-Key "n"
Send-Key "ret"
Shot "02_main"

Write-Host "[3/9] open terminal ..."
Send-Key "t"
Shot "03_terminal"

Write-Host "[4/9] run: help"
foreach ($k in @("h","e","l","p")) { Send-Key $k }
Send-Key "ret"
Shot "04_help"

Write-Host "[5/9] run: whoami"
foreach ($k in @("w","h","o","a","m","i")) { Send-Key $k }
Send-Key "ret"
Shot "05_whoami"

Write-Host "[6/9] run: users"
foreach ($k in @("u","s","e","r","s")) { Send-Key $k }
Send-Key "ret"
Shot "06_users"

Write-Host "[7/9] run: meminfo"
foreach ($k in @("m","e","m","i","n","f","o")) { Send-Key $k }
Send-Key "ret"
Shot "07_meminfo"

Write-Host "[8/9] run: uptime"
foreach ($k in @("u","p","t","i","m","e")) { Send-Key $k }
Send-Key "ret"
Shot "08_uptime"

Write-Host "[9/9] exit terminal ..."
foreach ($k in @("e","x","i","t")) { Send-Key $k }
Send-Key "ret"
Shot "09_main_back"

Stop-Process -Id $p.Id -Force -ErrorAction SilentlyContinue
Write-Host "Done. Results in $out"
