<#
.SYNOPSIS
  Like-for-like benchmark of deck.gl (+MapLibre) vs CesiumJS in the Qt WebEngine host.

.DESCRIPTION
  For each unit count and scenario it launches cesium_proto.exe once per engine, lets the page
  run a scripted camera path (pan + zoom + rotate) while the host streams unit frames, and
  collects the page's metrics plus the host's CPU / memory sampling. Results go to
  bench\results\<timestamp>\ as one JSON per run plus summary.md.

  Validity rules (a run is retried, then flagged INVALID, when it breaks one):
    * the machine must be on AC power (laptops switch to the integrated GPU and throttle on
      battery; refused unless -AllowBattery),
    * every run must use the same GPU as the first run,
    * the page must have drawn frames during the measured window.
  A browser window opens for every run: do not cover, minimise or use the machine heavily
  while it runs (Chromium throttles hidden windows and the GPU is shared).

.EXAMPLE
  .\run-bench.ps1                                  # full matrix, ~25 minutes
  .\run-bench.ps1 -Quick -Ns 300,5000              # short smoke run
  .\run-bench.ps1 -Ns 20000,50000 -OutDir bench\results\full   # redo some cells in place
  .\run-bench.ps1 -Scenarios tilt-terrain -Ns 300,5000 -OutDir bench\results\full
  .\run-bench.ps1 -SummarizeOnly bench\results\20261022-101500
#>
param(
    [string]$Exe = (Join-Path $PSScriptRoot '..\build-642\Release\cesium_proto.exe'),
    [string]$OutDir,
    [int[]]$Ns = @(300, 5000, 20000, 50000),
    [string[]]$Engines = @('deck', 'cesium'),
    [string[]]$Scenarios = @('2d-icons', '2d-full', 'tilt-icons', 'tilt-terrain'),
    [int]$Warm = 10,
    [int]$Measure = 15,
    [int]$PickSecs = 6,
    [int]$Hz = 20,
    [string]$ChromiumFlags = '--force-high-performance-gpu',
    [switch]$Quick,
    [switch]$AllowBattery,
    [string]$SummarizeOnly
)
$ErrorActionPreference = 'Stop'
$inv = [System.Globalization.CultureInfo]::InvariantCulture
Add-Type -AssemblyName System.Windows.Forms
$utf8 = New-Object System.Text.UTF8Encoding $false

function Get-PowerLine { [System.Windows.Forms.SystemInformation]::PowerStatus.PowerLineStatus.ToString() }
function Read-Json([string]$path) { [System.IO.File]::ReadAllText($path).TrimStart([char]0xFEFF) | ConvertFrom-Json }
function Write-Json([string]$path, $obj) { [System.IO.File]::WriteAllText($path, ($obj | ConvertTo-Json -Depth 10), $utf8) }

function Get-Results([string]$dir) {
    Get-ChildItem $dir -Filter *.json | ForEach-Object { Read-Json $_.FullName }
}

function Format-Cell($v, [string]$fmt = '0.0') {
    if ($null -eq $v) { return '-' }
    return ([double]$v).ToString($fmt, $inv)
}

function Write-Summary([string]$dir) {
    $results = @(Get-Results $dir)
    $lines = New-Object System.Collections.Generic.List[string]
    $lines.Add('# Engine benchmark results')
    $lines.Add('')
    $gpus = @($results | Where-Object { $_.env } | ForEach-Object { $_.env.gl } | Sort-Object -Unique)
    $first = $results | Where-Object { $_.env } | Select-Object -First 1
    if ($first) {
        $lines.Add(('Window {0}x{1} CSS px, devicePixelRatio {2}, GPU: {3}' -f $first.env.width, $first.env.height, $first.env.dpr, ($gpus -join ' / ')))
        $lines.Add('')
    }
    if ($gpus.Count -gt 1) { $lines.Add('**WARNING: more than one GPU appears in these results; they are not comparable across runs.**'); $lines.Add('') }
    $names = @{ '2d-icons' = '2D map, icons only'; '2d-full' = '2D map, icons + 1000 labels + 1000 trails';
                'tilt-icons' = 'Tilted 3D view (55 deg), icons only, no terrain'; 'tilt-terrain' = 'Tilted 3D view with DTED terrain (Cesium only)' }
    foreach ($key in '2d-icons', '2d-full', 'tilt-icons', 'tilt-terrain') {
        $rows = @($results | Where-Object { $_.scenario -eq $key } | Sort-Object { [int]$_.cfg.n }, { $_.cfg.engine })
        if (-not $rows) { continue }
        $lines.Add("## $($names[$key])")
        $lines.Add('')
        $lines.Add('| N | engine | fps | frame p95 ms | frames >33 ms | JS update ms p50 / p95 | camera ms p50 | latency p95 ms | app CPU % | web CPU % | memory MB | pick ms p50 / p95 | fps while picking |')
        $lines.Add('|--:|:--|--:|--:|--:|--:|--:|--:|--:|--:|--:|--:|--:|')
        foreach ($r in $rows) {
            if ($r.error) {
                $lines.Add(('| {0} | {1} | FAILED: {2} |||||||||||' -f $r.cfg.n, $r.cfg.engine, $r.error))
                continue
            }
            $f = $r.frames; $u = $r.updates; $h = $r.host; $p = $r.pick
            $over = if ($f.count) { 100.0 * $f.over33 / $f.count } else { 0 }
            $mem = if ($h) { $h.appRssMb + $h.webRssMb } else { $null }
            $flag = if ($r.invalid) { " (INVALID: $($r.invalid))" } else { '' }
            $lines.Add(('| {0} | {1}{15} | {2} | {3} | {4}% | {5} / {6} | {7} | {8} | {9} | {10} | {11} | {12} / {13} | {14} |' -f
                $r.cfg.n, $r.cfg.engine, (Format-Cell $f.fps), (Format-Cell $f.dt_p95), (Format-Cell $over),
                (Format-Cell $u.apply_p50), (Format-Cell $u.apply_p95), (Format-Cell $u.camera_p50),
                (Format-Cell $u.latency_p95), (Format-Cell $h.appCpuPct '0'), (Format-Cell $h.webCpuPct '0'),
                (Format-Cell $mem '0'), (Format-Cell $p.p50 '0.00'), (Format-Cell $p.p95 '0.00'), (Format-Cell $p.fps_during), $flag))
        }
        $lines.Add('')
    }
    $lines.Add('fps = requestAnimationFrame rate of the page during the measured window; frames >33 ms = share of frames slower than two 60 Hz refreshes. JS update = main-thread time to apply one 20 Hz unit frame (copying typed arrays, setting positions, building layers); it excludes the GPU upload deck.gl does inside its draw. App CPU includes the in-process GPU thread; web CPU is the Chromium renderer process. Both are % of one core.')
    [System.IO.File]::WriteAllLines((Join-Path $dir 'summary.md'), $lines, $utf8)
    $lines | ForEach-Object { Write-Host $_ }
}

if ($SummarizeOnly) { Write-Summary $SummarizeOnly; return }

if ($Quick) { $Warm = 4; $Measure = 6; $PickSecs = 3 }
if (-not (Test-Path $Exe)) { throw "Build first: $Exe not found." }
$Exe = (Resolve-Path $Exe).Path
if (-not $AllowBattery -and (Get-PowerLine) -ne 'Online') {
    throw 'This machine is on battery. Windows then switches to the integrated GPU and throttles CPU and GPU, which invalidates the comparison. Plug in AC power (or pass -AllowBattery to run anyway).'
}
if (-not $OutDir) { $OutDir = Join-Path $PSScriptRoot ("results\" + (Get-Date -Format 'yyyyMMdd-HHmmss')) }
New-Item -ItemType Directory -Force $OutDir | Out-Null
$OutDir = (Resolve-Path $OutDir).Path
if ($ChromiumFlags) { $env:QTWEBENGINE_CHROMIUM_FLAGS = $ChromiumFlags }

$all = @(
    @{ Name = '2d-icons';     View = '2d';   Labels = 0;    Trails = 0;    Terrain = 0 },
    @{ Name = '2d-full';      View = '2d';   Labels = 1000; Trails = 1000; Terrain = 0 },
    @{ Name = 'tilt-icons';   View = 'tilt'; Labels = 0;    Trails = 0;    Terrain = 0 },
    @{ Name = 'tilt-terrain'; View = 'tilt'; Labels = 0;    Trails = 0;    Terrain = 1 }
)
$cells = @()
$index = 0
foreach ($n in $Ns) {
    foreach ($s in ($all | Where-Object { $Scenarios -contains $_.Name })) {
        if ($s.Terrain) {
            # Terrain is Cesium only (deck.gl has no comparable terrain), and only at small N.
            if ($n -le 5000 -and $Engines -contains 'cesium') { $cells += @{ N = $n; S = $s; Engine = 'cesium' } }
            continue
        }
        # Alternate which engine goes first so thermal drift does not favour one of them.
        $order = @($Engines)
        if ($index % 2 -eq 1) { [array]::Reverse($order) }
        foreach ($engine in $order) { $cells += @{ N = $n; S = $s; Engine = $engine } }
        $index++
    }
}

$exeDir = Split-Path $Exe
$referenceGpu = $null
$i = 0
foreach ($c in $cells) {
    $i++
    $s = $c.S; $n = $c.N; $engine = $c.Engine
    $tag = '{0}_{1}_{2}' -f $s.Name, $engine, $n
    $json = Join-Path $OutDir "$tag.json"
    $log = Join-Path $OutDir "$tag.log"
    $query = 'engine={0}&n={1}&hz={2}&view={3}&labels={4}&trails={5}&terrain={6}&warm={7}&measure={8}&pickSecs={9}&pick=1' -f `
        $engine, $n, $Hz, $s.View, $s.Labels, $s.Trails, $s.Terrain, $Warm, $Measure, $PickSecs
    $hostArgs = @('--units', $n, '--hz', $Hz, '--radius', 8, '--size', '1600x900', '--page', 'bench.html',
                  '--query', "`"$query`"", '--bench-out', "`"$json`"")
    if (-not $s.Terrain) { $hostArgs = @('--flat-sim') + $hostArgs }

    $problem = $null
    for ($attempt = 1; $attempt -le 3; $attempt++) {
        Write-Host ('[{0}/{1}] {2} (attempt {3})' -f $i, $cells.Count, $tag, $attempt)
        Remove-Item $json -ErrorAction SilentlyContinue
        $p = Start-Process $Exe -ArgumentList $hostArgs -WorkingDirectory $exeDir -PassThru -RedirectStandardError $log
        $limit = ($Warm + $Measure + $PickSecs + 150) * 1000
        if (-not $p.WaitForExit($limit)) { $p.Kill(); Write-Host '  timed out, killed' }
        Get-Process QtWebEngineProcess -ErrorAction SilentlyContinue | Stop-Process -Force -ErrorAction SilentlyContinue
        $power = Get-PowerLine
        Start-Sleep -Seconds 3

        $problem = $null
        if (-not (Test-Path $json)) { $problem = 'no result (timeout or crash)' }
        else {
            $r = Read-Json $json
            if ($r.error) { $problem = "page error: $($r.error)" }
            elseif ($r.frames.count -lt 30) { $problem = "no frames drawn ($($r.frames.count)); window hidden or display off?" }
            elseif ($referenceGpu -and $r.env.gl -ne $referenceGpu) { $problem = "GPU changed to $($r.env.gl)" }
            elseif ($power -ne 'Online') { $problem = 'AC power lost during the run' }
            elseif (-not $referenceGpu) { $referenceGpu = $r.env.gl; Write-Host "  reference GPU: $referenceGpu" }
        }
        if (-not $problem) { break }
        Write-Host "  invalid: $problem"
        if ($power -ne 'Online' -and -not $AllowBattery) { throw "Stopped: AC power lost ($tag). Plug in and rerun the missing cells (see -Ns / -Scenarios)." }
    }

    if (Test-Path $json) {
        $r = Read-Json $json
        $r | Add-Member -NotePropertyName scenario -NotePropertyValue $s.Name -Force
        $r | Add-Member -NotePropertyName powerLine -NotePropertyValue $power -Force
        if ($problem) { $r | Add-Member -NotePropertyName invalid -NotePropertyValue $problem -Force }
        Write-Json $json $r
    } else {
        Write-Json $json @{ scenario = $s.Name; cfg = @{ engine = $engine; n = $n }; error = $problem }
    }
}

Write-Summary $OutDir
Write-Host "Done: $OutDir"
