param(
    [Parameter(Mandatory = $true)]
    [ValidateRange(1, 10)]
    [int] $Slot,

    [string] $Label = 'checkpoint',

    [string] $HostName = '127.0.0.1',

    [ValidateRange(1, 65535)]
    [int] $Port = 9201,

    [ValidateRange(1, 300)]
    [int] $TimeoutSeconds = 15
)

$ErrorActionPreference = 'Stop'
if ($Label -match '[\r\n]') {
    throw 'The save label cannot contain line breaks.'
}

$command = "save $Slot"
if (-not [string]::IsNullOrWhiteSpace($Label)) {
    $command += " $Label"
}

$client = [System.Net.Sockets.TcpClient]::new()
try {
    $client.Connect($HostName, $Port)
    $stream = $client.GetStream()
    $stream.ReadTimeout = $TimeoutSeconds * 1000

    $bytes = [System.Text.Encoding]::UTF8.GetBytes("$command`n")
    $stream.Write($bytes, 0, $bytes.Length)
    $stream.Flush()

    # Keep the socket open until the server has processed the line and sent its
    # reply; disconnecting immediately can drop the reply or the pending command.
    $reader = [System.IO.StreamReader]::new($stream, [System.Text.Encoding]::UTF8)
    try {
        while ($true) {
            $reply = $reader.ReadLine()
            if ($null -eq $reply) {
                throw 'The server closed the admin connection before replying.'
            }
            if ($reply -match '^save:') {
                Write-Output $reply
                if ($reply -match 'FAILED|no world loaded|want a slot') {
                    exit 1
                }
                break
            }
        }
    } finally {
        $reader.Dispose()
    }
} catch [System.IO.IOException] {
    throw "No save reply received from ${HostName}:$Port within $TimeoutSeconds seconds. The command may have been accepted; check the server before retrying."
} finally {
    $client.Dispose()
}
