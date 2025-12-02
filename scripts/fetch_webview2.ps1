$ErrorActionPreference = "Stop"

$pkgName = "Microsoft.Web.WebView2"
$version = "1.0.1901.177"
$url = "https://www.nuget.org/api/v2/package/$pkgName/$version"
$outputDir = "packages"
$pkgDir = Join-Path $outputDir $pkgName
$zipFile = Join-Path $outputDir "$pkgName.zip"

# Create packages dir
if (-not (Test-Path $outputDir)) {
    New-Item -ItemType Directory -Path $outputDir | Out-Null
}

# Check if already installed
if (Test-Path $pkgDir) {
    Write-Host "WebView2 package already present in $pkgDir"
    exit 0
}

Write-Host "Downloading WebView2 SDK ($version)..."
try {
    Invoke-WebRequest -Uri $url -OutFile $zipFile
}
catch {
    Write-Error "Failed to download WebView2 SDK. Please check your internet connection."
    exit 1
}

Write-Host "Extracting WebView2 SDK..."
try {
    Expand-Archive -Path $zipFile -DestinationPath $pkgDir -Force
}
catch {
    Write-Error "Failed to extract WebView2 SDK."
    exit 1
}

# Cleanup
Remove-Item $zipFile -Force

Write-Host "WebView2 SDK ready."
