import { defineConfig } from "vite";
import react from "@vitejs/plugin-react";
import { viteSingleFile } from "vite-plugin-singlefile";

// The plugin editor serves one self-contained page (scripts, styles and fonts inlined), so the
// bundle must not reference anything on the network. React's production build embeds a link to its
// error decoder; drop the scheme so the page carries no http(s) URLs at all.
function stripReactErrorDecoderUrl() {
  return {
    name: "petrichor-strip-react-error-url",
    renderChunk(code) {
      const out = code.replace(/https?:\/\/(reactjs\.org|react\.dev)\//g, "$1/");
      return out === code ? null : { code: out, map: null };
    },
  };
}

// https://vitejs.dev/config/
export default defineConfig({
  plugins: [react(), stripReactErrorDecoderUrl(), viteSingleFile()],
  build: {
    target: "es2020",
    assetsInlineLimit: 100000000, // Force everything (fonts included) to inline
    chunkSizeWarningLimit: 100000000,
    cssCodeSplit: false,
    reportCompressedSize: false,
    rollupOptions: {
      output: { inlineDynamicImports: true },
    },
  },
});
