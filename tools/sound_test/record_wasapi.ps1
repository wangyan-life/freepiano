<#
record_wasapi.ps1

Simple wrapper that uses ffmpeg (if available) to record default WASAPI loopback
and save to a WAV file. If ffmpeg is not available, instruct the user to use
Audacity with the WASAPI loopback device.

Usage: .\record_wasapi.ps1 -Out captured.wav -Seconds 5
#>

param(
    [string]$Out = "captured.wav",
    [int]$Seconds = 5
)

$ff = Get-Command ffmpeg -ErrorAction SilentlyContinue
if ($null -ne $ff) {
    Write-Host "Recording $Seconds seconds of default WASAPI loopback to $Out"
    & ffmpeg -f wasapi -i default -t $Seconds -ac 1 -ar 44100 $Out
} else {
    Write-Host "ffmpeg not found. Use Audacity: set Audio Host to 'Windows WASAPI', select the loopback device (eg 'Speakers (loopback)'), then record and export WAV to $Out"
}
