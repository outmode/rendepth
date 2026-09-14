param([Parameter(Mandatory = $true)][string]$OutputPath)

# Synthetic view sweep in CubeVi's right-to-left, bottom-to-top quilt order.
# Keep _qs8x5a0.5625 in the filename so Rendepth recognizes the quilt.
Add-Type -AssemblyName System.Drawing
$tileWidth = 270
$tileHeight = 480
$bitmap = [System.Drawing.Bitmap]::new($tileWidth * 8, $tileHeight * 5)
$graphics = [System.Drawing.Graphics]::FromImage($bitmap)
$font = [System.Drawing.Font]::new('Arial', 20)
$smallFont = [System.Drawing.Font]::new('Arial', 12)
$frame = [System.Drawing.Pen]::new([System.Drawing.Color]::White, 3)
try {
    $graphics.Clear([System.Drawing.Color]::FromArgb(20, 24, 32))
    for ($view = 0; $view -lt 40; $view++) {
        $column = 7 - ($view % 8)
        $row = 4 - [int][Math]::Floor($view / 8)
        $x = $column * $tileWidth
        $y = $row * $tileHeight
        $parallax = ($view / 39.0 - 0.5)
        $graphics.DrawRectangle($frame, $x + 15, $y + 15, 240, 450)
        $graphics.DrawString(('VIEW {0:D2}' -f $view), $font,
            [System.Drawing.Brushes]::White, $x + 65, $y + 45)
        $graphics.FillRectangle([System.Drawing.Brushes]::CornflowerBlue,
            [single]($x + 100 - 35 * $parallax), [single]($y + 145), 70, 200)
        $graphics.FillEllipse([System.Drawing.Brushes]::Coral,
            [single]($x + 80 + 100 * $parallax), [single]($y + 225), 110, 110)
        $graphics.DrawString('Move left / right', $smallFont,
            [System.Drawing.Brushes]::White, $x + 65, $y + 400)
    }
    $bitmap.Save([IO.Path]::GetFullPath($OutputPath), [System.Drawing.Imaging.ImageFormat]::Png)
} finally {
    $frame.Dispose()
    $font.Dispose()
    $smallFont.Dispose()
    $graphics.Dispose()
    $bitmap.Dispose()
}
