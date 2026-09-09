$qemu = Get-Command qemu-system-x86_64 -ErrorAction SilentlyContinue
if (-not $qemu) {
    $cands = @(
        "C:\Program Files\qemu\qemu-system-x86_64.exe",
        "C:\Program Files (x86)\qemu\qemu-system-x86_64.exe",
        "$env:LOCALAPPDATA\Programs\qemu\qemu-system-x86_64.exe",
        "D:\Program Files\qemu\qemu-system-x86_64.exe"
    )
    foreach ($c in $cands) { if (Test-Path $c) { $qemu = [PSCustomObject]@{ Source = $c }; break } }
}
if (-not $qemu) { Write-Host "[!] qemu not found"; exit 1 }
$src = if ($qemu.Source) { $qemu.Source } else { $qemu }

$serf = "$env:TEMP\qemu_iso_com1.log"
$p = Start-Process -FilePath $src `
    -ArgumentList '-cdrom','E:\project\clion\project_system\iso\FSOS.iso','-serial',("file:$serf"),'-display','none','-no-reboot' `
    -NoNewWindow -PassThru
Start-Sleep -Seconds 12
if (-not $p.HasExited) { $p | Stop-Process -Force }
Write-Host "---- raw COM1 bytes (hex) ----"
$bytes = [System.IO.File]::ReadAllBytes($serf)
for ($i=0; $i -lt $bytes.Length; $i++) { Write-Host -NoNewline ("{0:X2} " -f $bytes[$i]) }
Write-Host ""
Write-Host "---- as ASCII ----"
[System.Text.Encoding]::ASCII.GetString($bytes)
