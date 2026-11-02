[CmdletBinding()]
param(
    [ValidateSet('x64')]
    [string] $Target = 'x64',
    [ValidateSet('Debug', 'RelWithDebInfo', 'Release', 'MinSizeRel')]
    [string] $Configuration = 'RelWithDebInfo'
)

$ErrorActionPreference = 'Stop'

if ( $DebugPreference -eq 'Continue' ) {
    $VerbosePreference = 'Continue'
    $InformationPreference = 'Continue'
}

if ( $env:CI -eq $null ) {
    throw "Test-Windows.ps1 requires CI environment"
}

if ( ! ( [System.Environment]::Is64BitOperatingSystem ) ) {
    throw "A 64-bit system is required to build the project."
}

if ( $PSVersionTable.PSVersion -lt '7.2.0' ) {
    Write-Warning 'The obs-studio PowerShell build script requires PowerShell Core 7. Install or upgrade your PowerShell version: https://aka.ms/pscore6'
    exit 2
}

function Test {
    trap {
        Pop-Location -Stack TestTemp -ErrorAction 'SilentlyContinue'
        Write-Error $_
        Log-Group
        exit 2
    }

    $ScriptHome = $PSScriptRoot
    $ProjectRoot = Resolve-Path -Path "$PSScriptRoot/../.."

    $UtilityFunctions = Get-ChildItem -Path $PSScriptRoot/utils.pwsh/*.ps1 -Recurse

    foreach($Utility in $UtilityFunctions) {
        Write-Debug "Loading $($Utility.FullName)"
        . $Utility.FullName
    }

    Push-Location -Stack TestTemp
    Ensure-Location $ProjectRoot

    $CmakeArgs = @('--preset', "windows-ci-${Target}", '-DENABLE_TESTS=ON')
    $CmakeBuildArgs = @('--build')
    $CtestArgs = @()

    if ( $DebugPreference -eq 'Continue' ) {
        $CmakeArgs += ('--debug-output')
        $CmakeBuildArgs += ('--verbose')
        $CtestArgs += ('--verbose')
    }

    $CmakeBuildArgs += @(
        "build_${Target}"
        '--config', $Configuration
        '--target', 'pptlink-tests'
        '--parallel'
        '--', '/consoleLoggerParameters:Summary', '/noLogo'
    )

    $CtestArgs += @(
        '--test-dir', "build_${Target}"
        '-C', $Configuration
        '--output-on-failure'
    )

    Log-Group "Configuring tests..."
    Invoke-External cmake @CmakeArgs

    Log-Group "Building tests..."
    Invoke-External cmake @CmakeBuildArgs

    Log-Group "Running tests..."
    Invoke-External ctest @CtestArgs

    Pop-Location -Stack TestTemp
    Log-Group
}

Test
