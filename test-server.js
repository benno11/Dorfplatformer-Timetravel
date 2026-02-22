const http = require("http");
const fs = require("fs");
const path = require("path");

const HOST = process.env.HOST || "127.0.0.1";
const PORT = Number(process.env.PORT || 8080);
const WEB_ROOT = path.resolve(__dirname, "pages");

const MIME_TYPES = {
  ".html": "text/html; charset=utf-8",
  ".js": "text/javascript; charset=utf-8",
  ".css": "text/css; charset=utf-8",
  ".json": "application/json; charset=utf-8",
  ".png": "image/png",
  ".jpg": "image/jpeg",
  ".jpeg": "image/jpeg",
  ".svg": "image/svg+xml",
  ".ico": "image/x-icon",
  ".txt": "text/plain; charset=utf-8"
};

function safePathFromUrl(urlPath) {
  const cleanPath = decodeURIComponent(urlPath.split("?")[0]);
  const relPath = cleanPath === "/" ? "/index.html" : cleanPath;
  const absolutePath = path.resolve(WEB_ROOT, "." + relPath);
  if (!absolutePath.startsWith(WEB_ROOT + path.sep) && absolutePath !== WEB_ROOT) {
    return null;
  }
  return absolutePath;
}

function send(res, status, body, contentType) {
  res.writeHead(status, { "Content-Type": contentType });
  res.end(body);
}

const server = http.createServer((req, res) => {
  if (!req.url) {
    send(res, 400, "Bad Request", "text/plain; charset=utf-8");
    return;
  }

  const filePath = safePathFromUrl(req.url);
  if (!filePath) {
    send(res, 403, "Forbidden", "text/plain; charset=utf-8");
    return;
  }

  fs.stat(filePath, (statErr, stat) => {
    if (statErr || !stat.isFile()) {
      send(res, 404, "Not Found", "text/plain; charset=utf-8");
      return;
    }

    const ext = path.extname(filePath).toLowerCase();
    const contentType = MIME_TYPES[ext] || "application/octet-stream";
    const stream = fs.createReadStream(filePath);
    res.writeHead(200, { "Content-Type": contentType });
    stream.pipe(res);
    stream.on("error", () => {
      if (!res.headersSent) {
        send(res, 500, "Internal Server Error", "text/plain; charset=utf-8");
      } else {
        res.end();
      }
    });
  });
});

server.listen(PORT, HOST, () => {
  console.log(`Test server running at http://${HOST}:${PORT}/`);
  console.log(`Serving: ${WEB_ROOT}`);
});
