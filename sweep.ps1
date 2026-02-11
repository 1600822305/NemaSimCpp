# Universal parallel sweep script for celegans_diag (max 16 concurrent)
# Usage:
#   .\sweep.ps1                                              # 10-seed baseline
#   .\sweep.ps1 -Param pulse_amp -Values 40,50,60            # sweep param
#   .\sweep.ps1 -ExtraArgs '--no-toxin'                      # non-toxic food
#   .\sweep.ps1 -ExtraArgs '--no-food'                       # empty arena

param(
    [string]$Param = "",
    [double[]]$Values = @(),
    [int[]]$Seeds = 100..109,
    [int]$Duration = 300,
    [string[]]$ExtraArgs = @()
)

$exe = (Resolve-Path ".\build\Release\celegans_diag.exe").Path
$MaxJobs = 16

# Build task list (val, seed, args) — not launched yet
$tasks = @()
if ($Param -ne "" -and $Values.Count -gt 0) {
    foreach ($val in $Values) {
        foreach ($seed in $Seeds) {
            $tasks += ,@($val, $seed, @('--duration', $Duration, '--seed', $seed, "--$Param", $val) + $ExtraArgs)
        }
    }
} else {
    foreach ($seed in $Seeds) {
        $tasks += ,@(0, $seed, @('--duration', $Duration, '--seed', $seed) + $ExtraArgs)
    }
}

Write-Host "$($tasks.Count) tasks, max $MaxJobs parallel..." -ForegroundColor Cyan

# Run one job — shared scriptblock
$sb = {
    param($e, $a)
    $o = & $e @a 2>&1 | Out-String
    $ci=if($o-match'CI=([-\d.]+)'){$Matches[1]}else{'0'}
    $sp=if($o-match'mean=([\d.]+).*mm/s'){$Matches[1]}else{'0'}
    $om=if($o-match'omega/reversal ratio: ([\d.]+)'){$Matches[1]}else{'0'}
    $rv=if($o-match'pirouettes.*: (\d+)'){$Matches[1]}else{'0'}
    $sh=if($o-match'5-HT: conc=([\d.]+)'){$Matches[1]}else{'0'}
    $dv=if($o-match'D/V ratio: ([\d.]+)'){$Matches[1]}else{'0'}
    $wv=if($o-match'Wave quality: (\w+)'){$Matches[1]}else{'?'}
    "$ci|$sp|$om|$rv|$sh|$dv|$wv"
}

# Launch in batches of $MaxJobs
$data = @()
for ($i = 0; $i -lt $tasks.Count; $i += $MaxJobs) {
    $end = [Math]::Min($i + $MaxJobs - 1, $tasks.Count - 1)
    $batch_jobs = @()
    for ($j = $i; $j -le $end; $j++) {
        $t = $tasks[$j]
        $batch_jobs += Start-Job -ScriptBlock $sb -ArgumentList $exe, $t[2]
    }
    $batch_jobs | Wait-Job | Out-Null
    for ($k = 0; $k -lt $batch_jobs.Count; $k++) {
        $l = Receive-Job $batch_jobs[$k]; Remove-Job $batch_jobs[$k]
        $t = $tasks[$i + $k]
        $p = $l -split '\|'
        $data += [pscustomobject]@{
            Val=[double]$t[0]; Seed=[int]$t[1]; CI=[double]$p[0]; Sp=[double]$p[1]
            Om=[double]$p[2]; Rv=[int]$p[3]; SH=[double]$p[4]; DV=[double]$p[5]; Wv=$p[6]
        }
    }
    Write-Host "  Batch $([Math]::Floor($i/$MaxJobs)+1) done ($($end+1)/$($tasks.Count))" -ForegroundColor DarkGray
}

# Stats helper
function Show-Stats($rows, $label) {
    function S($v,$n) {
        $m=($v|Measure-Object -Average).Average
        $var=($v|%{($_-$m)*($_-$m)}|Measure-Object -Average).Average
        "{0}={1,7:F3}+/-{2:F3}" -f $n,$m,[Math]::Sqrt($var)
    }
    $wg = ($rows|?{$_.Wv-eq'GOOD'}).Count
    Write-Host ("  {0,-12} {1}  {2}  {3}  {4}  {5}  wave={6}/{7}" -f $label,
        (S $rows.CI 'CI'), (S $rows.Sp 'spd'), (S $rows.Om 'omega'),
        (S $rows.SH '5HT'), (S $rows.DV 'DV'), $wg, $rows.Count)
}

Write-Host "`n===== RESULTS =====" -ForegroundColor Yellow
if ($Param -ne "" -and $Values.Count -gt 0) {
    foreach ($val in $Values) {
        Show-Stats ($data | Where-Object { $_.Val -eq $val }) "$Param=$val"
    }
} else {
    # Per-seed detail
    $data | Sort-Object Seed | ForEach-Object {
        Write-Host ("  seed={0,3} CI={1,7:F3} spd={2:F3} omega={3:F2} rev={4,2} 5HT={5:F3} DV={6:F2} {7}" -f $_.Seed,$_.CI,$_.Sp,$_.Om,$_.Rv,$_.SH,$_.DV,$_.Wv)
    }
    Write-Host "---"
    Show-Stats $data "ALL"
}
