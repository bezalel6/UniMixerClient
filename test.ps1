# Test 1: Check if OpenOCD exists
Write-Host "Checking if OpenOCD exists..." -ForegroundColor Yellow
$openocdPath = "C:\Users\bezal\.platformio\packages\tool-openocd-esp32\bin\openocd.exe"
if (Test-Path $openocdPath) {
    Write-Host "✓ OpenOCD found at: $openocdPath" -ForegroundColor Green
} else {
    Write-Host "✗ OpenOCD not found at: $openocdPath" -ForegroundColor Red
    Write-Host "Checking alternative locations..." -ForegroundColor Yellow
    
    # Check for openocd without .exe extension
    $openocdAlt = "C:\Users\bezal\.platformio\packages\tool-openocd-esp32\bin\openocd"
    if (Test-Path $openocdAlt) {
        Write-Host "✓ Found OpenOCD (no .exe): $openocdAlt" -ForegroundColor Green
        $openocdPath = $openocdAlt
    }
}

# Test 2: List directory contents
Write-Host "`nDirectory contents:" -ForegroundColor Yellow
Get-ChildItem "C:\Users\bezal\.platformio\packages\tool-openocd-esp32\bin\" -ErrorAction SilentlyContinue

# Test 3: Try to run OpenOCD with version check
Write-Host "`nTesting OpenOCD execution..." -ForegroundColor Yellow
try {
    & $openocdPath --version
    Write-Host "✓ OpenOCD executable works!" -ForegroundColor Green
} catch {
    Write-Host "✗ Failed to run OpenOCD: $($_.Exception.Message)" -ForegroundColor Red
}

# Test 4: Test connection to ESP-Prog (if OpenOCD works)
Write-Host "`nTesting ESP-Prog connection..." -ForegroundColor Yellow
$scriptsDir = "C:\Users\bezal\.platformio\packages\tool-openocd-esp32\share\openocd\scripts"

# Basic connection test command
$testCommand = @(
    $openocdPath
    "-s"
    $scriptsDir
    "-f"
    "interface/ftdi/esp32_devkitj_v1.cfg"
    "-f"
    "target/esp32s3.cfg"
    "-c"
    "init; halt; shutdown"
)

Write-Host "Running: $($testCommand -join ' ')" -ForegroundColor Cyan
try {
    & $testCommand[0] $testCommand[1..($testCommand.Length-1)]
} catch {
    Write-Host "Connection test failed: $($_.Exception.Message)" -ForegroundColor Red
}

# Test 5: Test with your custom config (if it exists)
$customConfig = "custom_esp32s3.cfg"
if (Test-Path $customConfig) {
    Write-Host "`nTesting with custom config..." -ForegroundColor Yellow
    $customCommand = @(
        $openocdPath
        "-s"
        $scriptsDir
        "-f"
        "interface/ftdi/esp32_devkitj_v1.cfg"
        "-f"
        $customConfig
        "-c"
        "init; halt; shutdown"
    )
    
    Write-Host "Running: $($customCommand -join ' ')" -ForegroundColor Cyan
    try {
        & $customCommand[0] $customCommand[1..($customCommand.Length-1)]
    } catch {
        Write-Host "Custom config test failed: $($_.Exception.Message)" -ForegroundColor Red
    }
} else {
    Write-Host "`nCustom config file not found in current directory" -ForegroundColor Yellow
}
