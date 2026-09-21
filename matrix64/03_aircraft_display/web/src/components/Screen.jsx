import { useEffect, useState } from 'react';
import Box from '@mui/material/Box';

const SCREENSHOT_POLL_MS = 1000;

// The bezel-framed live panel view -- polls /debug/screenshot.bmp, which is
// a BMP dump of the exact frame the physical 64x64 panel is showing right
// now, no camera needed.
export default function Screen() {
  const [src, setSrc] = useState('/debug/screenshot.bmp');

  useEffect(() => {
    const id = setInterval(() => setSrc(`/debug/screenshot.bmp?t=${Date.now()}`), SCREENSHOT_POLL_MS);
    return () => clearInterval(id);
  }, []);

  return (
    <Box
      sx={{
        bgcolor: '#000',
        border: '1px solid',
        borderColor: 'divider',
        borderRadius: 2,
        p: { xs: 2, sm: 3 },
        display: 'flex',
        justifyContent: 'center',
      }}
    >
      {/* aspect-ratio lives on this wrapper, not the <img> -- as a flex
          item the <img> gets stretched to fill the row's cross-axis size
          (flex's default align-items: stretch), which fights its own
          aspect-ratio and renders it non-square. The wrapper is the flex
          item instead; the image just fills it. The panel is a fixed
          64x64 (PANEL_RES_X/Y in the firmware), so this is always a
          square regardless of viewport width. */}
      <Box sx={{ width: '100%', maxWidth: 480, aspectRatio: '1 / 1', position: 'relative' }}>
        <Box
          component="img"
          src={src}
          alt="live panel preview"
          sx={{
            position: 'absolute',
            inset: 0,
            width: '100%',
            height: '100%',
            objectFit: 'contain',
            imageRendering: 'pixelated',
            display: 'block',
          }}
        />
      </Box>
    </Box>
  );
}
