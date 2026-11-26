param([string]$Output = "build/content")
$ErrorActionPreference = 'Stop'
if (-not $env:IDF_PATH) { throw 'Activate ESP-IDF first (export.ps1).' }
python tools/content/build_packages.py --output $Output
if ($LASTEXITCODE) { throw 'Package generation failed' }
python "$env:IDF_PATH/components/fatfs/wl_fatfsgen.py" "$Output/seed" --output_file "$Output/documents.bin" --partition_size 0xbf0000 --sector_size 4096 --long_name_support
if ($LASTEXITCODE) { throw 'Filesystem image generation failed' }
Write-Output "Created $Output/documents.bin for NEW layout at 0x410000. This script does not flash or erase a device."
