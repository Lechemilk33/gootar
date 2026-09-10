import type { NextConfig } from "next";

const nextConfig: NextConfig = {
  transpilePackages: ["@gootar/preset-schema"],
  async headers() {
    return [
      {
        // The wasm engine deliberately avoids SharedArrayBuffer, so no COOP/COEP
        // is required. These two just make the worklet and wasm cache properly.
        source: "/nam/:file*",
        headers: [{ key: "Cache-Control", value: "public, max-age=31536000, immutable" }],
      },
    ];
  },
};

export default nextConfig;
