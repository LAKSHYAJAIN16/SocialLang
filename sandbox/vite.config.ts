import react from '@vitejs/plugin-react'
import { defineConfig } from 'vite'

// https://vite.dev/config/
export default defineConfig({
  plugins: [react()],
  // Relative asset URLs: Electron loads dist/index.html over file://, where
  // Vite's default absolute "/assets/..." resolves to the drive root and the
  // window comes up blank. "./" works for both file:// and a web server.
  base: './',
})
