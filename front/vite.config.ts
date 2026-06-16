import { defineConfig } from "vite";
import react from "@vitejs/plugin-react";

export default defineConfig({
  plugins: [react()],
  server: {
    proxy: {
      "/infer_video": {
        target: "http://127.0.0.1:8080",
        changeOrigin: true,
      },
      "/infer": {
        target: "http://127.0.0.1:8080",
        changeOrigin: true,
      },
    },
  },
});
