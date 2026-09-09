# ppm2png.ps1 - 临时工具: 将 QEMU screendump 的 PPM 转为 PNG
$ErrorActionPreference = "Stop"
Add-Type -AssemblyName System.Drawing

function Convert-PpmToPng($ppmPath, $pngPath) {
    $data = [System.IO.File]::ReadAllBytes($ppmPath)
    $len = [Math]::Min(256, $data.Length)
    $head = [System.Text.Encoding]::ASCII.GetString($data, 0, $len)
    $m = [regex]::Match($head, "P6\s+(\d+)\s+(\d+)\s+(\d+)\s*")
    if (-not $m.Success) { throw "bad ppm: $ppmPath" }
    $w = [int]$m.Groups[1].Value; $h = [int]$m.Groups[2].Value
    $pos = $m.Index + $m.Length
    # PPM P6 是 RGB; GDI+ Format24bppRgb 内存顺序为 BGR, 需要交换 R/B
    $srcStride = $w * 3
    $rgb = [byte[]]::new($srcStride * $h)
    for ($y = 0; $y -lt $h; $y++) {
        for ($x = 0; $x -lt $w; $x++) {
            $si = $pos + $y * $srcStride + $x * 3
            $di = $y * $srcStride + $x * 3
            $rgb[$di]     = $data[$si + 2]  # B
            $rgb[$di + 1] = $data[$si + 1]  # G
            $rgb[$di + 2] = $data[$si]      # R
        }
    }
    $bmp = New-Object System.Drawing.Bitmap($w, $h)
    $rect = New-Object System.Drawing.Rectangle(0, 0, $w, $h)
    $bd = $bmp.LockBits($rect, [System.Drawing.Imaging.ImageLockMode]::WriteOnly,
                        [System.Drawing.Imaging.PixelFormat]::Format24bppRgb)
    for ($y = 0; $y -lt $h; $y++) {
        $dstRow = $bd.Scan0.ToInt64() + ($h - 1 - $y) * $bd.Stride
        [System.Runtime.InteropServices.Marshal]::Copy($rgb, $y * $srcStride, [IntPtr]$dstRow, $srcStride)
    }
    $bmp.UnlockBits($bd)
    $bmp.Save($pngPath, [System.Drawing.Imaging.ImageFormat]::Png)
    $bmp.Dispose()
}

$out = "e:\project\clion\project_system\first\output\verify"
Get-ChildItem "$out\*.png" | ForEach-Object {
    $ppm = $_.FullName
    $png = [System.IO.Path]::ChangeExtension($ppm, ".view.png")
    Convert-PpmToPng $ppm $png
    Write-Host "converted: $($_.Name) -> $([System.IO.Path]::GetFileName($png))"
}
