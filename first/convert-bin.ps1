Add-Type -AssemblyName System.Drawing
$out = "e:\project\clion\project_system\first\output\fbverify"
$pal = New-Object 'byte[][]' 256
for ($i = 0; $i -lt 256; $i++) { $pal[$i] = @(0,0,0) }
function SetPal($idx,$r,$g,$b) { $pal[$idx] = @([byte]([Math]::Min(63,$r)*255/63), [byte]([Math]::Min(63,$g)*255/63), [byte]([Math]::Min(63,$b)*255/63)) }
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
Get-ChildItem "$out\*.bin" | ForEach-Object {
    $px = [System.IO.File]::ReadAllBytes($_.FullName)
    $w = 320; $h = 200
    $bmp = New-Object System.Drawing.Bitmap($w, $h)
    for ($y = 0; $y -lt $h; $y++) {
        for ($x = 0; $x -lt $w; $x++) {
            $c = $pal[$px[$y * $w + $x]]
            $bmp.SetPixel($x, $y, [System.Drawing.Color]::FromArgb($c[0], $c[1], $c[2]))
        }
    }
    $png = [System.IO.Path]::ChangeExtension($_.FullName, ".png")
    $bmp.Save($png, [System.Drawing.Imaging.ImageFormat]::Png)
    $bmp.Dispose()
    Write-Host $png
}
