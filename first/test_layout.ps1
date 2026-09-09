$layout = @{}
$n = 0
foreach ($line in Get-Content "E:\project\clion\project_system\first\layout.h") {
    $n++
    if ($line -match '^\s*#define\s+(\w+)\s+(\d+)') {
        $layout[$matches[1]] = [int]$matches[2]
        Write-Host ("line {0}: matched key='{1}' value={2}" -f $n, $matches[1], $matches[2])
    } else {
        Write-Host ("line {0}: NO MATCH  [{1}]" -f $n, $line.Substring(0, [Math]::Min(50, $line.Length)))
    }
}
Write-Host "---- result ----"
Write-Host ("LBA_MOD_CINT=" + $layout["LBA_MOD_CINT"])