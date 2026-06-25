import type { Config } from "tailwindcss";

const config: Config = {
  content: ["./index.html", "./src/**/*.{ts,tsx}"],
  theme: {
    extend: {
      fontFamily: {
        sans: [
          "Geist",
          "Aptos",
          "ui-sans-serif",
          "system-ui",
          "-apple-system",
          "BlinkMacSystemFont",
          "Segoe UI",
          "sans-serif",
        ],
        mono: [
          "Geist Mono",
          "SFMono-Regular",
          "Cascadia Code",
          "Liberation Mono",
          "monospace",
        ],
      },
      boxShadow: {
        panel: "0 18px 45px rgba(15, 23, 42, 0.09)",
        shell: "0 20px 70px rgba(15, 23, 42, 0.08)",
      },
    },
  },
  plugins: [],
};

export default config;
