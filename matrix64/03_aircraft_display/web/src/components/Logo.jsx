import Box from '@mui/material/Box';

// A small dot-matrix mark echoing the physical panel this console controls
// -- no image asset needed, just a CSS grid of dots.
//
// Padding/gap are computed here as pixel values from `size`, not left as
// percentages in sx -- CSS resolves *vertical* percentage padding against
// the containing block's WIDTH, not the element's own size, so "p: '10%'"
// on an icon sitting in a full-width toolbar blows up to ~10% of the
// toolbar's width on every side, not 10% of the 28px icon.
export default function Logo({ size = 28 }) {
  const dots = Array.from({ length: 9 });
  const padding = Math.round(size * 0.1);
  const gap = Math.round(size * 0.15);
  return (
    <Box
      sx={{
        width: size,
        height: size,
        display: 'grid',
        gridTemplateColumns: 'repeat(3, 1fr)',
        gap: `${gap}px`,
        p: `${padding}px`,
        bgcolor: 'primary.main',
        borderRadius: 1,
        flexShrink: 0,
        boxSizing: 'border-box',
      }}
    >
      {dots.map((_, i) => (
        <Box key={i} sx={{ bgcolor: 'background.default', borderRadius: '1px' }} />
      ))}
    </Box>
  );
}
