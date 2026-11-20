param([string]$Image = "\\wsl.localhost\archlinux\root\aos\anyps5-os.img", [int]$Disk = 3, [string]$Name = "Verbatim", [double]$SizeGB = 28.9)
$Host.UI.RawUI.WindowTitle = "Writing AnyPS5 OS to the USB stick"
Write-Host "Writing $Image to disk $Disk ($Name, $SizeGB GB)" -ForegroundColor Cyan
python -u "$PSScriptRoot\write_stick.py" $Image $Disk $Name $SizeGB 2>&1 | Tee-Object -FilePath "C:\AnyPS5-work\probes\write-image.log"
Write-Host ""
Write-Host "Finished. Unplug the stick and put it in the Chromebook." -ForegroundColor Green
Read-Host "Press Enter to close"
