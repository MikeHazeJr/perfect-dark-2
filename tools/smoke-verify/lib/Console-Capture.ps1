# Bound native console retention while continuously draining both child pipes.
# The game's ordinary log remains the authoritative assertion source.
if (-not ('PdSmokeConsoleDrain' -as [type])) {
    Add-Type -TypeDefinition @'
using System;
using System.IO;
using System.Text;
using System.Threading.Tasks;

public sealed class PdSmokeConsoleDrain {
    private readonly Stream input;
    private readonly FileStream output;
    private readonly int limit;
    private readonly Task task;
    public long BytesRead { get; private set; }
    public long BytesWritten { get; private set; }
    public bool Truncated { get; private set; }
    public string Error { get; private set; }
    public string Path { get; private set; }

    public PdSmokeConsoleDrain(Stream source, string path, int byteLimit) {
        if (source == null || byteLimit < 256 || byteLimit > 1048576)
            throw new ArgumentException("Console capture requires a stream and bounded limit");
        input = source; limit = byteLimit; Path = path;
        output = new FileStream(path, FileMode.CreateNew, FileAccess.Write, FileShare.Read);
        task = Task.Factory.StartNew(Drain, TaskCreationOptions.LongRunning);
    }

    private void Drain() {
        byte[] marker = Encoding.UTF8.GetBytes("\n[console capture truncated; final bytes follow]\n");
        int head = limit / 2;
        byte[] tail = new byte[limit - head - marker.Length];
        byte[] buffer = new byte[8192];
        int tailPosition = 0, tailCount = 0;
        try {
            int n;
            while ((n = input.Read(buffer, 0, buffer.Length)) != 0) {
                int prefix = (int)Math.Min(n, Math.Max(0L, limit - BytesRead));
                if (prefix > 0) output.Write(buffer, 0, prefix);
                BytesRead += n;
                // Retain only a bounded suffix even while excess bytes drain.
                int offset = Math.Max(0, n - tail.Length);
                int remaining = n - offset;
                while (remaining > 0) {
                    int take = Math.Min(remaining, tail.Length - tailPosition);
                    Buffer.BlockCopy(buffer, offset, tail, tailPosition, take);
                    tailPosition = (tailPosition + take) % tail.Length;
                    tailCount = Math.Min(tail.Length, tailCount + take);
                    remaining -= take; offset += take;
                }
            }
            Truncated = BytesRead > limit;
            if (Truncated) {
                output.SetLength(head); output.Position = head;
                output.Write(marker, 0, marker.Length);
                int start = tailCount == tail.Length ? tailPosition : 0;
                int first = Math.Min(tailCount, tail.Length - start);
                output.Write(tail, start, first);
                if (tailCount > first) output.Write(tail, 0, tailCount - first);
            }
            BytesWritten = output.Length;
            output.Flush();
        } catch (Exception error) {
            Error = error.GetType().Name + ": " + error.Message;
        } finally {
            output.Dispose();
        }
    }

    public bool Wait(int milliseconds) { return task.Wait(milliseconds); }
    public void Stop() { input.Dispose(); }
}
'@
}

function Start-SmokeConsoleCapture {
    param([Parameter(Mandatory)][System.Diagnostics.Process]$Process,
          [Parameter(Mandatory)][string]$Directory,
          [ValidateRange(256,1048576)][int]$LimitBytes=131072)
    Assert-SmokePlainTree -Path $Directory -Descendants
    [void][IO.Directory]::CreateDirectory($Directory)
    $label='native-'+$Process.Id+'-'+[guid]::NewGuid().ToString('N')
    $stdout=New-Object PdSmokeConsoleDrain($Process.StandardOutput.BaseStream, (Join-Path $Directory ($label+'-stdout.log')), $LimitBytes)
    try {
        $stderr=New-Object PdSmokeConsoleDrain($Process.StandardError.BaseStream, (Join-Path $Directory ($label+'-stderr.log')), $LimitBytes)
    } catch {
        $stdout.Stop()
        [void]$stdout.Wait(1000)
        throw
    }
    return [pscustomobject]@{Stdout=$stdout;Stderr=$stderr;MetadataPath=(Join-Path $Directory ($label+'-console.json'))}
}

function Complete-SmokeConsoleCapture {
    param([Parameter(Mandatory)][psobject]$Capture,
          [ValidateRange(1,30000)][int]$WaitMilliseconds=5000)
    $rows=@()
    foreach($name in @('Stdout','Stderr')) {
        $drain=$Capture.$name
        $complete=$drain.Wait($WaitMilliseconds)
        if(-not $complete) {
            $drain.Stop()
            [void]$drain.Wait(1000)
        }
        $rows+=[pscustomobject]@{Stream=$name;Path=[string]$drain.Path;BytesRead=[long]$drain.BytesRead;BytesWritten=[long]$drain.BytesWritten;Truncated=[bool]$drain.Truncated;Completed=$complete;Error=[string]$drain.Error}
    }
    $passed=@($rows|Where-Object{-not $_.Completed -or $_.Error}).Count -eq 0
    $summary=[pscustomobject]@{Passed=$passed;Streams=$rows;AuthoritativeGameLog='unchanged'}
    [IO.File]::WriteAllText($Capture.MetadataPath, ($summary|ConvertTo-Json -Depth 5), (New-Object Text.UTF8Encoding($false)))
    return $summary
}
