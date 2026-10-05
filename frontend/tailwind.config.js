/** @type {import('tailwindcss').Config} */
export default {
  content: ["./index.html", "./src/**/*.{js,ts,jsx,tsx}"],
  theme: {
    extend: {
      fontFamily: {
        display: ['"Cormorant Garamond"', "Georgia", "serif"],
        sans: ["Manrope", "system-ui", "sans-serif"],
        mono: ['"IBM Plex Mono"', "ui-monospace", "monospace"],
      },
      colors: {
        ink: {
          950: "#05080e",
          900: "#080d16",
          850: "#0b111c",
          800: "#0e1624",
          700: "#142033",
          600: "#1c2b42",
          500: "#2a3d5a",
        },
        mist: {
          100: "#e3edf5",
          200: "#c5d4e2",
          300: "#9fb2c6",
          400: "#7a8ea6",
          500: "#5c6f88",
          600: "#43536a",
        },
        rain: { 300: "#a8e6f4", 400: "#7dd3e8", 500: "#4fbcd8", 600: "#2f97b5" },
        bolt: { 200: "#fffbe0", 300: "#fbf1b5", 400: "#f3e188", 500: "#e6c95a" },
        earth: { 300: "#e6c3a0", 400: "#d4a373", 500: "#b9824f", 600: "#8c5f3a" },
      },
    },
  },
  plugins: [],
};
