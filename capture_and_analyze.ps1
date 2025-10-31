# capture_and_analyze.ps1
# 用法：在仓库根目录的 PowerShell 中运行： pwsh -ExecutionPolicy Bypass -File .\capture_and_analyze.ps1
# 确保 demo 正在或准备播放，在提示时开始播放

# 可选：若 ffmpeg 不在 PATH，请把下面的 $ffmpegPath 设置为完整 exe 路径
$ffmpegPath = "ffmpeg"   # 或 "C:\path\to\ffmpeg.exe"

# Python 可执行路径（可改为 `python`）
$pythonPath = "C:/Users/Admin/AppData/Local/Programs/Python/Python313/python.exe"

Write-Host "1) ffmpeg version check..."
& $ffmpegPath -version

Write-Host ""
Write-Host "准备录音：请确保 demo 将在接下来的录音时播放（或当前正播放）。"
Read-Host "按回车开始 5 秒录音（开始前请先切换到播放 demo）"

Write-Host "2) Recording 5s via WASAPI loopback -> tools\\sound_test\\captured.wav"
& $ffmpegPath -f wasapi -i default -t 5 -ac 1 -ar 44100 .\tools\sound_test\captured.wav

if (Test-Path .\tools\sound_test\captured.wav) {
    $f = Get-Item .\tools\sound_test\captured.wav
    Write-Host "Recorded:" $f.FullName " size:" ($f.Length/1KB -as [math]::Round) "KB"
} else {
    Write-Error "录音文件未找到，请确认 ffmpeg 可执行，或改用 Audacity 的 WASAPI(loopback) 录制。"
    Read-Host "按回车退出"
    exit 1
}

Write-Host "`n3) 安装 Python 依赖（numpy matplotlib scipy）并运行分析脚本..."
& $pythonPath -m pip install --upgrade pip
& $pythonPath -m pip install numpy matplotlib scipy

& $pythonPath tools\sound_test\analyze_sine.py tools\sound_test\captured.wav

Write-Host "`n完成。频谱图片保存在与 WAV 相同目录下（*_spectrum.png）。"
Read-Host "按回车退出"