# Reconstruct a clean ELF from a fake-signed PS5 SELF.
# Segment data in a fake-signed SELF is plain (enc=0, cmp=0); it just sits at
# container offsets instead of the offsets the program headers declare.

param(
    [Parameter(Mandatory=$true)][string]$In,
    [Parameter(Mandatory=$true)][string]$Out
)

$b = [System.IO.File]::ReadAllBytes($In)

function U16($o) { [int]$b[$o] + ([int]$b[$o+1] -shl 8) }
function U32($o) { [uint32]([int]$b[$o] + ([int]$b[$o+1] -shl 8) + ([int]$b[$o+2] -shl 16) + ([int]$b[$o+3] -shl 24)) }
function U64($o) { $v=[uint64]0; for ($i=7; $i -ge 0; $i--) { $v = ($v -shl 8) + $b[$o+$i] }; $v }

if (-not ($b[0] -eq 0x4f -and $b[1] -eq 0x15 -and $b[2] -eq 0x3d -and $b[3] -eq 0x1d)) {
    throw "not a SELF container"
}

$numEntries = U16 0x18
$elfBase = 0x20 + $numEntries * 32
if (-not ($b[$elfBase] -eq 0x7f -and $b[$elfBase+1] -eq 0x45)) { throw "no ELF at 0x$('{0:x}' -f $elfBase)" }

$phoff = [int](U64 ($elfBase + 32))
$phentsize = U16 ($elfBase + 54)
$phnum = U16 ($elfBase + 56)

# output size = furthest point any program header claims
$outSize = 0
$phdrs = @()
for ($i = 0; $i -lt $phnum; $i++) {
    $p = $elfBase + $phoff + $i * $phentsize
    $h = [pscustomobject]@{
        Index = $i
        Type  = U32 $p
        Off   = [int](U64 ($p+8))
        FileSz= [int](U64 ($p+32))
    }
    $phdrs += $h
    if ($h.Off + $h.FileSz -gt $outSize) { $outSize = $h.Off + $h.FileSz }
}
Write-Host ("output ELF size: 0x{0:x} ({0:N0} bytes)" -f $outSize)

$out = New-Object byte[] $outSize

# ELF header + program headers
$hdrBytes = $phoff + $phnum * $phentsize
[Array]::Copy($b, $elfBase, $out, 0, $hdrBytes)
Write-Host ("copied header + $phnum program headers (0x{0:x} bytes)" -f $hdrBytes)

# segment data from the entry table
$placed = @{}
for ($i = 0; $i -lt $numEntries; $i++) {
    $p = 0x20 + $i*32
    $props = U64 $p
    $off   = [int](U64 ($p+8))
    $fsz   = [int](U64 ($p+16))
    $hasBlocks = (($props -shr 11) -band 1) -eq 1
    $isData = (($props -band 0x800) -ne 0)      # blocked entries carry segment data
    $segIdx = [int](($props -shr 20) -band 0xfff)
    if (-not $isData) { continue }
    $ph = $phdrs | Where-Object { $_.Index -eq $segIdx } | Select-Object -First 1
    if (-not $ph) { Write-Host ("  entry {0}: no phdr {1}, skipped" -f $i, $segIdx); continue }
    if ($fsz -ne $ph.FileSz) { Write-Host ("  entry {0}: size 0x{1:x} != phdr {2} size 0x{3:x}, skipped" -f $i,$fsz,$segIdx,$ph.FileSz); continue }
    [Array]::Copy($b, $off, $out, $ph.Off, $fsz)
    $placed[$segIdx] = $true
    Write-Host ("  entry {0,2} -> phdr {1,2}  0x{2:x8} bytes  container 0x{3:x8} -> elf 0x{4:x8}" -f $i,$segIdx,$fsz,$off,$ph.Off)
}

# any remaining tail bytes belong to the one unplaced segment of that exact size
$lastEntryEnd = 0
for ($i = 0; $i -lt $numEntries; $i++) {
    $p = 0x20 + $i*32
    $e = [int](U64 ($p+8)) + [int](U64 ($p+16))
    if ($e -gt $lastEntryEnd) { $lastEntryEnd = $e }
}
$tail = $b.Length - $lastEntryEnd
if ($tail -gt 0) {
    $cand = $phdrs | Where-Object { -not $placed.ContainsKey($_.Index) -and $_.FileSz -eq $tail }
    if ($cand.Count -eq 1) {
        [Array]::Copy($b, $lastEntryEnd, $out, $cand[0].Off, $tail)
        Write-Host ("  tail 0x{0:x} bytes -> phdr {1} at elf 0x{2:x8}" -f $tail, $cand[0].Index, $cand[0].Off)
        $placed[$cand[0].Index] = $true
    } else {
        Write-Host ("  tail 0x{0:x} bytes: {1} candidate phdrs, left unplaced" -f $tail, $cand.Count)
    }
}

# report any segment with data we could not source
foreach ($h in $phdrs) {
    if ($h.FileSz -eq 0 -or $placed.ContainsKey($h.Index)) { continue }
    $covered = $false
    foreach ($o in $phdrs) {
        if ($placed.ContainsKey($o.Index) -and $h.Off -ge $o.Off -and ($h.Off + $h.FileSz) -le ($o.Off + $o.FileSz)) { $covered = $true; break }
    }
    if (-not $covered) { Write-Host ("  WARNING phdr {0} type=0x{1:x} off=0x{2:x} size=0x{3:x} has no source" -f $h.Index,$h.Type,$h.Off,$h.FileSz) }
}

[System.IO.File]::WriteAllBytes($Out, $out)
Write-Host ("wrote {0} ({1:N0} bytes)" -f $Out, (Get-Item $Out).Length)
