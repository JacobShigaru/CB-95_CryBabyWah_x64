<#
    CB-95 Wah — установка VST3-плагина.

    По умолчанию копирует бандл в системную папку VST3
    (C:\Program Files\Common Files\VST3) — для этого нужны права администратора.

    С ключом -User копирует в пользовательскую папку
    (%LOCALAPPDATA%\Programs\Common\VST3); её нужно добавить в список
    VST3-папок в настройках DAW.
#>
param(
    [switch]$User
)

$ErrorActionPreference = 'Stop'

$source = Join-Path $PSScriptRoot 'dist\CryBabyWah.vst3'
if (-not (Test-Path $source)) {
    throw "Не найден бандл: $source`nСначала запустите build.bat."
}

if ($User) {
    $base = Join-Path $env:LOCALAPPDATA 'Programs\Common\VST3'
} else {
    $base = Join-Path $env:ProgramFiles 'Common Files\VST3'
}

$destination = Join-Path $base 'CryBabyWah.vst3'

Write-Host "Источник : $source"
Write-Host "Цель     : $destination"

New-Item -ItemType Directory -Force -Path $base | Out-Null

if (Test-Path $destination) {
    Remove-Item $destination -Recurse -Force
}

Copy-Item $source $destination -Recurse -Force

Write-Host "Готово. Плагин установлен как 'CB-95 Wah'."
if ($User) {
    Write-Host "Добавьте папку '$base' в список VST3-папок вашей DAW."
}
Write-Host "Перезапустите DAW или запустите пересканирование плагинов."
