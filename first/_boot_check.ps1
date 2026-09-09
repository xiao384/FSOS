$ok=0;$fail=0
for ($i=1; $i -le 4; $i++) {
    $out = & "C:/Users/Administrator/AppData/Local/Programs/Python/Python314/python.exe" check-iso.py 2>&1 | Out-String
    if ($out -match 'RIP=0x([0-9a-f]+)') {
        $rip = [Convert]::ToUInt64($Matches[1], 16)
        if ($rip -ge 0x100000 -and $rip -lt 0x459800) { $ok++; Write-Host ("run {0}: RIP=0x{1:X} IN-KERNEL(ok)" -f $i,$rip) }
        else { $fail++; Write-Host ("run {0}: RIP=0x{1:X} OUT-OF-RANGE(FAIL)" -f $i,$rip) }
    } else { Write-Host ("run {0}: NO RIP OUTPUT" -f $i) }
}
Write-Host ("==== OK={0} FAIL={1} ====" -f $ok,$fail)
