import react from '@vitejs/plugin-react';
import { defineConfig } from 'vite';

// In development the API and the SSE stream are proxied to the server on
// 5055; in production the server serves the built client itself.
export default defineConfig({
  plugins: [react()],
  server: {
    port: 5173,
    proxy: {
      '/api': { target: 'http://localhost:5055', changeOrigin: false },
      '/ingest': { target: 'http://localhost:5055' },
    },
    fs: { allow: ['..'] },
  },
  build: { outDir: 'dist', sourcemap: false },
});
