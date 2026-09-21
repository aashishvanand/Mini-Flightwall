import { defineConfig } from 'vite';
import react from '@vitejs/plugin-react';

// base: './' -- the board serves this from /web/, not the domain root, and
// the ESP32 static handler resolves plain relative paths (see
// handleWebStatic() in the firmware), so asset URLs must not be absolute.
export default defineConfig({
  plugins: [react()],
  base: './',
  build: {
    outDir: 'dist',
  },
});
