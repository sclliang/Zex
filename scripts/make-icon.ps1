$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Drawing
$projectRoot = Split-Path -Parent $PSScriptRoot
$sizes = @(16,24,32,48,64,128,256)
$images = @()
foreach ($size in $sizes) {
    $bitmap = [System.Drawing.Bitmap]::new($size * 4, $size * 4)
    $g = [System.Drawing.Graphics]::FromImage($bitmap)
    $g.SmoothingMode = [System.Drawing.Drawing2D.SmoothingMode]::AntiAlias
    $g.Clear([System.Drawing.Color]::Transparent)
    $g.ScaleTransform($size * 4 / 64.0, $size * 4 / 64.0)
    $brush = [System.Drawing.SolidBrush]::new([System.Drawing.ColorTranslator]::FromHtml('#252a37'))
    $path = [System.Drawing.Drawing2D.GraphicsPath]::new()
    $path.AddArc(2,2,18,18,180,90); $path.AddArc(44,2,18,18,270,90)
    $path.AddArc(44,44,18,18,0,90); $path.AddArc(2,44,18,18,90,90); $path.CloseFigure()
    $g.FillPath($brush,$path)
    $pen = [System.Drawing.Pen]::new([System.Drawing.ColorTranslator]::FromHtml('#b2c9ff'),4)
    $pen.StartCap = 'Square'; $pen.EndCap = 'Square'
    $points = [System.Drawing.PointF[]]@([System.Drawing.PointF]::new(19,18),[System.Drawing.PointF]::new(45,18),[System.Drawing.PointF]::new(19,46),[System.Drawing.PointF]::new(45,46))
    $g.DrawLines($pen,$points); $g.DrawLine($pen,28,32,44,32)
    $small = [System.Drawing.Bitmap]::new($size,$size)
    $scale = [System.Drawing.Graphics]::FromImage($small)
    $scale.InterpolationMode = 'HighQualityBicubic'; $scale.DrawImage($bitmap,0,0,$size,$size)
    $memory = [System.IO.MemoryStream]::new(); $small.Save($memory,[System.Drawing.Imaging.ImageFormat]::Png)
    $images += ,$memory.ToArray()
    $memory.Dispose(); $scale.Dispose(); $small.Dispose(); $pen.Dispose(); $path.Dispose(); $brush.Dispose(); $g.Dispose(); $bitmap.Dispose()
}
$stream = [System.IO.File]::Create((Join-Path $projectRoot 'assets\zex.ico'))
$writer = [System.IO.BinaryWriter]::new($stream)
$writer.Write([uint16]0); $writer.Write([uint16]1); $writer.Write([uint16]$sizes.Count)
$offset = 6 + 16 * $sizes.Count
for ($i=0; $i -lt $sizes.Count; $i++) {
    $dimension = if($sizes[$i] -eq 256){0}else{$sizes[$i]}
    $writer.Write([byte]$dimension); $writer.Write([byte]$dimension); $writer.Write([byte]0); $writer.Write([byte]0)
    $writer.Write([uint16]1); $writer.Write([uint16]32); $writer.Write([uint32]$images[$i].Length); $writer.Write([uint32]$offset)
    $offset += $images[$i].Length
}
foreach($image in $images){$writer.Write([byte[]]$image)}
$writer.Dispose()
