$ErrorActionPreference = "Stop"

$htmlPath = Join-Path "frontend" "dist" "index.html"
$headerPath = Join-Path "Source" "FrontendAssets.h"

if (-not (Test-Path $htmlPath)) {
    Write-Error "Error: $htmlPath does not exist. Run 'npm run build' first."
    exit 1
}

$bytes = [System.IO.File]::ReadAllBytes($htmlPath)
$hexContent = ($bytes | ForEach-Object { "0x{0:x2}" -f $_ }) -join ", "
$size = $bytes.Length

$headerContent = @"
#pragma once
#include <cstddef>
#include <vector>

namespace FrontendAssets
{
    const unsigned char index_html_data[] = { $hexContent };
    const size_t index_html_size = $size;
}
"@

[System.IO.File]::WriteAllText($headerPath, $headerContent)

Write-Host "Successfully generated $headerPath"
