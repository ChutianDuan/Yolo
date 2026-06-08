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
        panel: "0 18px 50px rgba(0, 0, 0, 0.26)",
      },
    },
  },
  plugins: [],
};

export default config;
