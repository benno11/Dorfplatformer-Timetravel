const fs = require("fs");
const path = require("path");
const { minify: minifyJs } = require("terser");
const { minify: minifyHtml } = require("html-minifier-terser");
const CleanCSS = require("clean-css");

const ROOT = path.resolve(__dirname, "..");
const SRC_DIR = path.join(ROOT, "pages");
const OUT_DIR = path.join(ROOT, "dist", "account-manager");

function readUtf8(filePath) {
  return fs.readFileSync(filePath, "utf8");
}

function writeUtf8(filePath, contents) {
  fs.mkdirSync(path.dirname(filePath), { recursive: true });
  fs.writeFileSync(filePath, contents, "utf8");
}

async function run() {
  const indexPath = path.join(SRC_DIR, "index.html");
  const jsPath = path.join(SRC_DIR, "account-manager.js");
  const cssPath = path.join(SRC_DIR, "theme.css");
  const apiPath = path.join(SRC_DIR, "api.json");

  const [indexHtml, accountJs, themeCss] = [
    readUtf8(indexPath),
    readUtf8(jsPath),
    readUtf8(cssPath)
  ];

  const jsResult = await minifyJs(accountJs, {
    compress: true,
    mangle: true,
    format: { comments: false }
  });
  if (!jsResult.code) {
    throw new Error("JS minification produced no output.");
  }

  const cssResult = new CleanCSS({ level: 2 }).minify(themeCss);
  if (cssResult.errors && cssResult.errors.length > 0) {
    throw new Error(`CSS minification failed: ${cssResult.errors.join("; ")}`);
  }

  const htmlResult = await minifyHtml(indexHtml, {
    collapseWhitespace: true,
    removeComments: true,
    removeRedundantAttributes: true,
    removeScriptTypeAttributes: true,
    removeStyleLinkTypeAttributes: true,
    minifyCSS: true,
    minifyJS: true
  });

  fs.rmSync(OUT_DIR, { recursive: true, force: true });
  fs.mkdirSync(OUT_DIR, { recursive: true });

  writeUtf8(path.join(OUT_DIR, "index.html"), htmlResult);
  writeUtf8(path.join(OUT_DIR, "account-manager.js"), jsResult.code);
  writeUtf8(path.join(OUT_DIR, "theme.css"), cssResult.styles || "");
  fs.copyFileSync(apiPath, path.join(OUT_DIR, "api.json"));

  console.log("Account manager production build complete.");
  console.log(`Output: ${OUT_DIR}`);
}

run().catch((err) => {
  console.error("Build failed:", err && err.message ? err.message : err);
  process.exit(1);
});
