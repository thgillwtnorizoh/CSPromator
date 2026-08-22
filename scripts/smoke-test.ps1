param(
    [int]$Port = 3010
)

$uri = "http://127.0.0.1:$Port/"
$headers = @{ "Content-Type" = "application/json" }

$payloads = @(
    '{"provider":{"name":"Counter-Strike 2"},"round":{"phase":"freezetime"},"player":{"state":{"health":100}}}',
    '{"provider":{"name":"Counter-Strike 2"},"round":{"phase":"live"},"player":{"state":{"health":100}}}',
    '{"provider":{"name":"Counter-Strike 2"},"round":{"phase":"live"},"player":{"state":{"health":72}}}'
)

foreach ($body in $payloads) {
    Invoke-WebRequest -UseBasicParsing -Method Post -Uri $uri -Headers $headers -Body $body | Out-Null
    Start-Sleep -Milliseconds 80
}

Write-Host "Sent $($payloads.Count) synthetic GSI snapshots to $uri"
