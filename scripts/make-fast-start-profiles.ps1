param([switch]$Install)
$ErrorActionPreference = 'Stop'
$workspace = Split-Path $PSScriptRoot -Parent
$source = Join-Path $workspace 'upstream/OrcaSlicer/resources/profiles/BBL/machine'
$output = Join-Path $workspace 'artifacts/fast-start-profiles'
New-Item -ItemType Directory -Force $output | Out-Null
$previousHashes = @{}
$manifestPath = Join-Path $output 'manifest.json'
if (Test-Path -LiteralPath $manifestPath) {
    foreach ($entry in (Get-Content -LiteralPath $manifestPath -Raw | ConvertFrom-Json)) {
        $previousHashes[$entry.name] = $entry.output_sha256
    }
}

function Replace-Section([string]$text, [string]$start, [string]$end, [string]$replacement) {
    $a = $text.IndexOf($start, [StringComparison]::Ordinal)
    if ($a -lt 0) { throw "Missing section: $start" }
    $b = $text.IndexOf($end, $a + $start.Length, [StringComparison]::Ordinal)
    if ($b -lt 0) { throw "Missing section end: $end" }
    $b = $text.IndexOf("`n", $b)
    if ($b -lt 0) { $b = $text.Length } else { $b++ }
    return $text.Substring(0,$a) + $replacement + "`n" + $text.Substring($b)
}

$manifest = @()
foreach ($model in @('X1 Carbon', 'X2D', 'H2S')) {
    foreach ($diameter in @('0.2','0.4','0.6','0.8')) {
        $baseName = "Bambu Lab $model $diameter nozzle"
        $base = Get-Content -LiteralPath (Join-Path $source "$baseName.json") -Raw | ConvertFrom-Json
        $startInclude = @($base.include | Where-Object { $_ -match 'template machine_start_gcode$' })
        # H2S and X2D variants inherit the 0.4 template through their parent.
        if ($startInclude.Count -eq 0) { $startInclude = @("Bambu Lab $model 0.4 nozzle template machine_start_gcode") }
        if ($startInclude.Count -ne 1) { throw "Ambiguous start template: $baseName" }
        $templatePath = Join-Path $source ($startInclude[0] + '.json')
        $g = (Get-Content -LiteralPath $templatePath -Raw | ConvertFrom-Json).machine_start_gcode.Replace("`r",'')
        # Keep M17 motor activation, remove only the startup tune and its one-second wait.
        $g = [regex]::Replace($g, '(?m)^M17\nM400 S1\n(?=M1006)', "M17`n")
        $g = [regex]::Replace($g, '(?m)^M1006[^\n]*\n', '')
        if ($model -eq 'X1 Carbon') {
            $g = Replace-Section $g ';===== check scanner clarity ===' ';===== check scanner clarity end' '; Fast Start: scanner clarity test omitted; post-wipe homing below retained.'
            $g = Replace-Section $g ';===== bed leveling ===' ';===== bed leveling end' '; Fast Start: reuse existing bed mesh.'
            $g = Replace-Section $g ';===== home after wipe mouth===' ';===== home after wipe mouth end' "; Fast Start: unconditional post-wipe homing (independent of send options).`nM1002 gcode_claim_action : 13`nG28"
            $g = Replace-Section $g ';===== mech mode fast check===' ';===== mech mode fast check===' '; Fast Start: vibration test omitted; existing input shaping retained.'
            $g = [regex]::Replace($g, '(?s)\{if scan_first_layer\}.*?\{endif\}', '; Fast Start: first-layer scan omitted.')
            $g = Replace-Section $g ';===== draw extrinsic para cali paint' ';========turn off light and wait extrude temperature' ';========turn off light and wait extrude temperature ============='
        } else {
            $g = Replace-Section $g ';===== auto extrude cali start' ';===== auto extrude cali end' '; Fast Start: automatic flow calibration omitted; use a calibrated filament preset.'
            $g = Replace-Section $g ';===== mech mode sweep start' ';===== mech mode sweep end' '; Fast Start: vibration sweep omitted; existing input shaping retained.'
            $homingCommand = if ($model -eq 'X2D') { 'G28 R' } else { 'G28' }
            $g = Replace-Section $g ';===== bed leveling ===' ';===== bed leveling end' "; Fast Start: reuse bed mesh; retain temperature waits and safe post-wipe homing.`nM190 S[bed_temperature_initial_layer_single]`nM109 S140`nM106 S0`n$homingCommand`nG29.2 S1"
            # Remove plate identification/alignment camera tests, preserving foreign-object
            # and trash-bin collision checks, camera state restoration and tall-part checks.
            $g = [regex]::Replace($g, '(?m)^\s*M972 S(?:19|31|34)\b[^\n]*\n', '')
            $g = [regex]::Replace($g, '(?m)^\s*M1002 gcode_claim_action\s*:\s*(?:11|73)\b[^\n]*\n', '')
            if ($model -eq 'X2D') {
                $g = Replace-Section $g ';===== xy ofst cali start' ';===== xy ofst cali end' "; Fast Start: use saved toolhead XY offset.`nM1012.5 N1 R1"
            }
        }
        if ($model -eq 'X1 Carbon') {
            # Same-material priming: 10 + 5 mm instead of 50 + 50 + 5 mm.
            # Keep the final 5 mm and retraction/shake that detach the waste.
            $purges = [regex]::Matches($g, '(?m)^G1 E50 F200$')
            if ($purges.Count -ne 2) { throw "Unexpected X1 purge sequence: $baseName" }
            $purgeRegex = [regex]::new('(?m)^G1 E50 F200$')
            $g = $purgeRegex.Replace($g, 'G1 E10 F200', 1)
            # Replace the second occurrence explicitly, leaving no zero-length extrusion.
            $pos = $g.IndexOf('G1 E50 F200')
            $g = $g.Remove($pos, 'G1 E50 F200'.Length)
            # The final purge line already primes the nozzle; omit the earlier duplicate.
            $g = Replace-Section $g ';===== nozzle load line ===' ';===== for Textured PEI Plate' ';===== for Textured PEI Plate , preserve stock plate offset below ==='
            # Keep one brush pass before nozzle-touch homing; omit the steel scrub,
            # repeated brush traversals and circular cleaning motions.
            $quickWipe = @'
;===== quick nozzle wipe (no steel scrub) =====
M1002 gcode_claim_action : 14
M975 S1
M106 S255
G1 X65 Y230 F18000
G1 Y264 F6000
M109 S{nozzle_temperature_initial_layer[initial_no_support_extruder]-20}
G1 X100 F18000
G0 X135 Y253 F20000
M109 S140
M106 S0
G29.2 S1
'@
            $g = Replace-Section $g ';===== wipe nozzle ===' ';===== wipe nozzle end' $quickWipe
        } elseif ($model -eq 'H2S') {
            # Keep stock volumetric feed rates, temperatures, retraction and prime line.
            foreach ($amount in @('60','45')) {
                $reduced = if ($amount -eq '60') { '15' } else { '10' }
                $pattern = '(?m)^(\s*G1 E)' + $amount + '( F\{filament_max_volumetric_speed[^\n]*)$'
                if ([regex]::Matches($g,$pattern).Count -ne 1) { throw "Unexpected H2S purge: $baseName" }
                $g = [regex]::Replace($g,$pattern,('${1}' + $reduced + '${2}'))
            }
            $cycle = "    G150.2`n    G150.1`n"
            if (!$g.Contains($cycle + $cycle + $cycle)) { throw 'Unexpected TPU cleaning sequence' }
            $g = $g.Replace($cycle + $cycle + $cycle, $cycle)
        } else {
            $cycle = "  G150.2`n  G150.1 F8000`n"
            if (!$g.Contains($cycle + $cycle)) { throw 'Unexpected X2D cleaning sequence' }
            $g = $g.Replace($cycle + $cycle, $cycle)
        }
        if ($model -ne 'X1 Carbon') {
            # G150 is the separate full cleaning routine. G150.1/.2/.3 are
            # distinct wipe / waste handling / parking operations, not aliases.
            $g = Replace-Section $g ';===== wipe right nozzle start' ';===== wipe left nozzle end' '; Fast Start: full G150 nozzle cleaning omitted; quick wipe retained.'
            # Remove bare redundant wipe calls after cooling, retaining the first
            # pre-homing wipe and the single waste-detachment/wipe cycle.
            $g = [regex]::Replace($g, '(?m)^(?:G150\.1|  G150\.1)[ \t]*$', '')
            if ($g -match '(?m)^\s*G150\s') { throw 'Full cleaning unexpectedly retained' }
            # No aggressive cleaning remains to disturb the established Z reference.
            # Retain initial homing and X2D offset measurement, omit the later rehome.
            $g = [regex]::Replace($g, '(?m)^G28(?: R)?[ \t]*\n', '; Fast Start: redundant post-cleaning homing omitted.' + "`n")
        }
        # Loaded-nozzle path: deliberately no automatic material switching. The
        # operator selects the material/nozzle that is already physically loaded.
        if ($model -eq 'X1 Carbon') {
            $loadedStart = @'
;===== already-loaded nozzle: home during heating =====
G28 X
M109 S140
G28 Z
M104 S[nozzle_temperature_initial_layer]
M412 S1
M975 S1
'@
            $g = Replace-Section $g ';===== prepare print temperature and material ===' ';===== prepare print temperature and material end' $loadedStart
            $g = Replace-Section $g ';===== quick nozzle wipe' 'G29.2 S1' '; Fast Start: final wipe moved to immediately before prime line.'
            $g = $g.Replace("M1002 gcode_claim_action : 13`nG28", '; Fast Start: already homed.')
            $g = $g.Replace('M104 S75 ;set extruder temp to turn on the HB fan and prevent filament oozing from nozzle', 'M104 S140 ; nonblocking preheat for homing')
            $g = $g.Replace('M190 S[bed_temperature_initial_layer_single] ;wait for bed temp', '; Fast Start: bed heating overlaps homing; wait before prime line.')
            $finalWipe = @'
;===== final wipe before purge line =====
M190 S[bed_temperature_initial_layer_single]
M109 S{nozzle_temperature_initial_layer[initial_no_support_extruder]}
G90
G1 Z5 F1200
G1 X65 Y230 F18000
G1 Y264 F6000
G1 X100 F18000
;===== purge line to wipe the nozzle ============================
'@
            $g = $g.Replace(';===== purge line to wipe the nozzle ============================', $finalWipe)
        } else {
            # Remove the now-empty detection setup, retaining the tall-part
            # obstruction check at the end of that section.
            $g = Replace-Section $g ';===== detection start' 'M17 D' '; Fast Start: plate detection setup omitted.'
            # Remove the whole material load sequence, not just its extrusion.
            $loadedSetup = if ($model -eq 'X2D') {
                "M620 D[initial_no_support_hotend] ; select already-loaded physical hotend, no filament load`nM975 S1`nG29.2 S1"
            } else { "M975 S1`nG29.2 S1" }
            $g = Replace-Section $g ';===== prepare print temperature and material ===' ';===== prepare print temperature and material ===' $loadedSetup
            if ($model -eq 'H2S') {
                $g = Replace-Section $g '{if filament_type[initial_no_support_extruder] == "TPU"}' 'G1 Y-16 F12000' '; Fast Start: no bin purge or waste-detachment cycle.'
                # No intermediate cooling step: nozzle continues toward print temp.
                $g = $g.Replace("M109 S140`nM106 S0", "M106 S0")
            } else {
                $g = Replace-Section $g '  {if filament_type[initial_filament_id] == "TPU" || filament_type[initial_filament_id] == "PVA"}' 'G1 Y-16 F12000' '; Fast Start: no purge retraction/waste cycle.'
                $g = Replace-Section $g ';============switch again===' ';============switch again===' '; Fast Start: no second material switch.'
                # Z offset measurement remains at its required temperature.
            }
            # All explicit wipe calls are replaced by exactly one final pass.
            $g = [regex]::Replace($g, '(?m)^[ \t]*G150\.[12]\b[^\n]*\n', '')
            $wipeFeed = if ($model -eq 'X2D') { '8000' } else { '18000' }
            $nozzleIndex = if ($model -eq 'X2D') { 'initial_no_support_filament_id' } else { 'initial_no_support_extruder' }
            $finalWipe = ";===== final wipe before purge line =====`nM190 S[bed_temperature_initial_layer_single]`nM109 S{nozzle_temperature_initial_layer[$nozzleIndex]}`nG150.1 F$wipeFeed`n;===== nozzle load line ==="
            $g = $g.Replace(';===== nozzle load line ===', $finalWipe)
            if ($model -eq 'X2D') {
                # Begin nozzle heating before initial homing, without waiting.
                $g = $g.Replace(';===== first homing start', "M104 S140 A`n;===== first homing start")
            }
        }
        # No opaque cleaning/parking/priming macros. Explicit prime-line motion
        # uses the already established Z reference and saved nozzle calibration.
        $deniedMotion = '(?m)^[ \t]*(?:G150(?:\.\d+)?|G151|G383(?:\.\d+)?|G390(?:\.\d+)?|G28\.14\d*|G130)\b[^\n]*\n?'
        $g = [regex]::Replace($g, $deniedMotion, '')
        if ($model -ne 'X1 Carbon') {
            $primeHeight = [Math]::Min(0.3, [double]::Parse($diameter, [Globalization.CultureInfo]::InvariantCulture) * 0.5)
            $primeVolume = 40 * $primeHeight * ([double]::Parse($diameter, [Globalization.CultureInfo]::InvariantCulture) * 1.2)
            $primeE = ($primeVolume / 2.4053).ToString('0.000', [Globalization.CultureInfo]::InvariantCulture)
            $primeZ = $primeHeight.ToString('0.000', [Globalization.CultureInfo]::InvariantCulture)
            $primeX = if ($model -eq 'H2S') { 250 } else { 100 }
            $primeEndX = $primeX + 40
            $explicitPrime = ";===== explicit prime line; no firmware wipe macro =====`nG90`nM83`nG29.2 S1`nM190 S[bed_temperature_initial_layer_single]`nM109 S{nozzle_temperature_initial_layer[$nozzleIndex]}`nG1 Z5 F1200`nG1 X$primeX Y1 F12000`nG1 Z$primeZ F600`nG92 E0`nG1 X$primeEndX E$primeE F600`nG1 Z1 F1200`nG92 E0"
            $g = Replace-Section $g ';===== nozzle load line ===' ';===== noozle load line end' $explicitPrime
            # Remove the now-unused offset-measurement temperature cycle.
            if ($model -eq 'X2D') {
                $g = Replace-Section $g '{if filament_type[initial_filament_id] == "PC"}' ';===== z ofst cali end' '; Fast Start: saved nozzle offset; no opaque offset-measurement routine.'
                $g = $g.Replace("M109 S140`nM106 S0", 'M106 S0')
            }
        }
        $g = $g.Replace(';===== final wipe before purge line =====', ';===== temperature check before prime line; no macro wipe =====')
        # Warm repeat prints must not shut off the bed to cool the enclosure.
        # Preserve fan/airduct management and positive chamber-heating waits.
        $g = [regex]::Replace($g, '(?m)^[ \t]*M140 S0\b[^\n]*\n?', '; Fast Start: keep bed heating during chamber ventilation.' + "`n")
        $g = [regex]::Replace($g, '(?m)^[ \t]*M191 S0\b[^\n]*\n?', '; Fast Start: ventilate without blocking for chamber cooldown.' + "`n")
        $g = [regex]::Replace($g, '(?m)^[ \t]*M1002 gcode_claim_action\s*:\s*29\b[^\n]*\n?', '')
        $g = "M140 S[bed_temperature_initial_layer_single] ; begin bed heating immediately`n; ORQA FAST START: ALREADY LOADED MATERIAL/NOZZLE ONLY. No startup bin purge or material loading.`n" + $g
        if ($g -match '(?m)^[ \t]*(?:M140 S0|M191 S0)\b') { throw 'Startup cooling wait or bed shutdown remains' }
        if ($g -match $deniedMotion) { throw "Opaque cleaning-related motion remains: $baseName" }
        if ($g -match '(?m)^\s*(?:M620 S|M621 S|T\[|G1 E(?:10|15|45|50|60) F)') { throw "Unexpected loading or bin purge: $baseName" }
        # Fail closed if a source change leaves survey/calibration operations behind.
        if ($g -match '(?m)^\s*(G29\s|M970\.|M974\s|M976\s|M977\s|M983\.3\s)' -or $g -match 'judge_flag (g29_before_print_flag|extrude_cali_flag)') { throw "Unexpected calibration in $baseName" }
        foreach ($required in @('M190','M109','G28','M975')) {
            if (!$g.Contains($required)) { throw "Missing safeguard $required in $baseName" }
        }
        $depth = 0
        foreach ($line in ($g -split "`n")) {
            if ($line -match '^\s*M622\s') { $depth++ }
            if ($line -match '^\s*M623\b') { $depth--; if ($depth -lt 0) { throw 'Unbalanced firmware conditional' } }
        }
        if ($depth -ne 0) { throw 'Unbalanced firmware conditional' }
        $profile = [ordered]@{
            type = 'machine'; name = "$baseName - Fast Start"; inherits = $baseName
            from = 'User'; instantiation = 'true'; version = '2.5.0.0'
            machine_start_gcode = $g; scan_first_layer = '0'
        }
        # End routines otherwise unload to AMS and invoke wipe macros, defeating
        # the next already-loaded startup. Keep the filament loaded and retain
        # stock cooling, Z clearance and motor restoration.
        $endInclude = @($base.include | Where-Object { $_ -match 'template machine_end_gcode$' })
        if ($endInclude.Count -eq 0) { $endInclude = @("Bambu Lab $model 0.4 nozzle template machine_end_gcode") }
        $endGcode = (Get-Content -LiteralPath (Join-Path $source ($endInclude[0] + '.json')) -Raw | ConvertFrom-Json).machine_end_gcode.Replace("`r",'')
        $endGcode = [regex]::Replace($endGcode, $deniedMotion, '')
        $endGcode = [regex]::Replace($endGcode, '(?m)^[ \t]*(?:M620 S\d+[^\n]*|M621 S\d+[^\n]*|T(?:255|65279|65535)[^\n]*)\n?', '')
        if ($endGcode -match $deniedMotion) { throw 'Cleaning macro remains in end code' }
        $profile.machine_end_gcode = "; Fast Start: leave material loaded; no firmware wipe macros.`n" + $endGcode
        # Timelapse motion is irrelevant to the single-material prototype path.
        $profile.time_lapse_gcode = '; Fast Start: no timelapse motion.'
        $path = Join-Path $output ($profile.name + '.json')
        [IO.File]::WriteAllText($path, ($profile | ConvertTo-Json -Depth 20), [Text.UTF8Encoding]::new($false))
        $manifest += [ordered]@{ name=$profile.name; source=$startInclude[0]; source_sha256=(Get-FileHash $templatePath).Hash; output_sha256=(Get-FileHash $path).Hash }
    }
}
if ($Install) {
    $userRoot = Join-Path $workspace 'versions/0.1.0-dev.1/data_dir/user'
    $users = @(Get-ChildItem -LiteralPath $userRoot -Directory | Where-Object { $_.Name -eq 'default' -or (Test-Path (Join-Path $_.FullName 'sync_state')) -or (Test-Path (Join-Path $_.FullName 'machine')) })
    foreach ($user in $users) {
        $dest = Join-Path $user.FullName 'machine'
        New-Item -ItemType Directory -Force $dest | Out-Null
        foreach ($item in $manifest) {
            $file = $item.name + '.json'
            $target = Join-Path $dest $file
            if (Test-Path -LiteralPath $target) {
                $existingHash = (Get-FileHash $target).Hash
                if ($existingHash -ne $item.output_sha256) {
                    $knownOldCopy = $existingHash -eq $previousHashes[$item.name]
                    if (!$knownOldCopy) {
                        $backupRoot = Join-Path $output 'backup'
                        if (Test-Path -LiteralPath $backupRoot) {
                            foreach ($savedCopy in (Get-ChildItem -LiteralPath $backupRoot -Recurse -File | Where-Object { $_.Name -eq $file })) {
                                if ((Get-FileHash -LiteralPath $savedCopy.FullName).Hash -eq $existingHash) { $knownOldCopy = $true; break }
                            }
                        }
                    }
                    if (!$knownOldCopy) { throw "Existing edited profile protected: $target" }
                    $backupDir = Join-Path $output ('backup/' + (Get-Date -Format 'yyyyMMdd-HHmmss') + '/' + $user.Name)
                    New-Item -ItemType Directory -Force $backupDir | Out-Null
                    Copy-Item -LiteralPath $target -Destination (Join-Path $backupDir $file)
                    Copy-Item -LiteralPath (Join-Path $output $file) -Destination $target
                }
            } else { Copy-Item -LiteralPath (Join-Path $output $file) -Destination $target }
        }
    }
}
[IO.File]::WriteAllText((Join-Path $output 'manifest.json'), ($manifest | ConvertTo-Json -Depth 10))
Write-Output "Generated and structurally checked $($manifest.Count) Fast Start profiles. Installed: $Install"
