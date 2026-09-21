import Box from '@mui/material/Box';
import LinearProgress from '@mui/material/LinearProgress';
import Typography from '@mui/material/Typography';

export default function StatCard({ icon, label, value, unit, sub, subColor, progress, progressColor = 'primary' }) {
  return (
    <Box sx={{ border: '1px solid', borderColor: 'divider', borderRadius: 2, p: 2, minWidth: 0 }}>
      <Box sx={{ display: 'flex', alignItems: 'center', gap: 0.75, color: 'text.secondary', mb: 1 }}>
        {icon}
        <Typography variant="body2" noWrap>
          {label}
        </Typography>
      </Box>
      <Typography
        variant="h5"
        sx={{ fontWeight: 600, display: 'flex', alignItems: 'baseline', gap: 0.5, overflow: 'hidden' }}
        noWrap
      >
        {value}
        {unit && (
          <Typography component="span" variant="body2" color="text.secondary">
            {unit}
          </Typography>
        )}
      </Typography>
      {progress !== undefined && (
        <LinearProgress
          variant="determinate"
          value={Math.max(0, Math.min(100, progress))}
          color={progressColor}
          sx={{ mt: 1, borderRadius: 1, height: 4 }}
        />
      )}
      {sub && (
        <Typography variant="caption" color={subColor || 'text.secondary'} sx={{ display: 'block', mt: 0.5 }} noWrap>
          {sub}
        </Typography>
      )}
    </Box>
  );
}
