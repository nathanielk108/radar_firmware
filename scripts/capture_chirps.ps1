[CmdletBinding()]
param(
  [string] $Port = 'COM13',
  [Parameter(Mandatory)] [string] $OutputPath,
  [ValidateRange(1, 1000)] [int] $Count = 8,
  [ValidateRange(200, 3100)] [int] $GainMv
)

$ErrorActionPreference = 'Stop'

function Read-RadarMatch($Serial, [string] $Pattern) {
  $deadline = [Diagnostics.Stopwatch]::StartNew()
  while ($deadline.ElapsedMilliseconds -lt 3000) {
    try {
      $line = $Serial.ReadLine().TrimEnd([char]13)
    } catch [TimeoutException] {
      continue
    }
    if ($line -match '^(RADAR (busy|unavailable|start:|capture:)|GAIN_DAC (range|unavailable))') {
      throw $line
    }
    if ($line -match $Pattern) {
      return $line
    }
  }
  throw "No expected response from $Port. Check the USB connection and firmware console."
}

function Read-RadarCapture($Serial) {
  $Serial.Write('c')
  $metadata = Read-RadarMatch $Serial '^# sample_rate_hz='
  if ($metadata -notmatch ',samples=(\d+)(,|$)') {
    throw "Invalid capture metadata: $metadata"
  }
  $samples = [int] $Matches[1]
  if ($samples -lt 1 -or $samples -gt 65535) {
    throw "Invalid capture sample count: $samples"
  }
  $header = $Serial.ReadLine().TrimEnd([char]13)
  if ($header -ne 'index,adc_a,adc_b') {
    throw "Unexpected CSV header: $header"
  }
  $rows = [Collections.Generic.List[string]]::new($samples + 2)
  $rows.Add($metadata)
  $rows.Add($header)
  for ($sample = 0; $sample -lt $samples; ++$sample) {
    $row = $Serial.ReadLine().TrimEnd([char]13)
    if ($row -notmatch '^(\d+),(-?\d+),(-?\d+)$' -or [int] $Matches[1] -ne $sample) {
      throw "Invalid CSV row for index ${sample}: $row"
    }
    $rows.Add($row)
  }
  return $rows.ToArray()
}

$filePath = $ExecutionContext.SessionState.Path.GetUnresolvedProviderPathFromPSPath($OutputPath)
if ([IO.File]::Exists($filePath)) {
  throw "Capture file already exists: $filePath"
}
$serial = [IO.Ports.SerialPort]::new($Port, 115200)
$serial.DtrEnable = $true
$serial.NewLine = "`n"
$serial.ReadTimeout = 250
$serial.WriteTimeout = 1000

try {
  try {
    $serial.Open()
  } catch [UnauthorizedAccessException] {
    throw "Windows denied access to $Port. Close any serial terminal using it and retry."
  }
  $serial.Write("`rstatus`r")
  $status = Read-RadarMatch $serial '^uptime=.*\bradar=[01]\b'
  if ($status -notmatch '\bradar=1\b') {
    throw 'RADAR unavailable. Attach the RF board with power off, reboot, and check status.'
  }

  if ($PSBoundParameters.ContainsKey('GainMv')) {
    $serial.Write("g $GainMv`r")
    Read-RadarMatch $serial '^GAIN_DAC \d+ mV \(nominal\)$'
  }

  $stream = [IO.File]::Open($filePath, [IO.FileMode]::CreateNew, [IO.FileAccess]::Write)
  try {
    $writer = [IO.StreamWriter]::new($stream, [Text.UTF8Encoding]::new($false))
    try {
      for ($capture = 1; $capture -le $Count; ++$capture) {
        $rows = Read-RadarCapture $serial
        foreach ($row in $rows) {
          $writer.WriteLine($row)
        }
        $writer.Flush()
        Write-Host "Saved capture $capture of $Count"
      }
    } finally {
      $writer.Dispose()
    }
  } finally {
    $stream.Dispose()
  }
} finally {
  $serial.Dispose()
}
