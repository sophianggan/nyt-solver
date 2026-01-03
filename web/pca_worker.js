let canvas = null;
let ctx = null;
let width = 0;
let height = 0;
let dpr = 1;
let groupColors = ["#fbbf24", "#22c55e", "#3b82f6", "#a855f7"];

function resizeCanvas(nextWidth, nextHeight, nextDpr) {
  width = nextWidth;
  height = nextHeight;
  dpr = nextDpr || 1;
  if (canvas) {
    canvas.width = width;
    canvas.height = height;
  }
}

function drawPlot(points, envelopes) {
  if (!ctx || width <= 0 || height <= 0) {
    return;
  }
  if (!Array.isArray(points) || points.length === 0) {
    ctx.clearRect(0, 0, width, height);
    ctx.fillStyle = "#0b1220";
    ctx.fillRect(0, 0, width, height);
    return;
  }
  let minX = points[0].x;
  let maxX = points[0].x;
  let minY = points[0].y;
  let maxY = points[0].y;
  for (const point of points) {
    if (point.x < minX) minX = point.x;
    if (point.x > maxX) maxX = point.x;
    if (point.y < minY) minY = point.y;
    if (point.y > maxY) maxY = point.y;
  }
  const spanX = Math.max(1e-6, maxX - minX);
  const spanY = Math.max(1e-6, maxY - minY);
  const extraX = spanX * 0.08;
  const extraY = spanY * 0.08;
  minX -= extraX;
  maxX += extraX;
  minY -= extraY;
  maxY += extraY;
  const paddedSpanX = Math.max(1e-6, maxX - minX);
  const paddedSpanY = Math.max(1e-6, maxY - minY);
  const pad = 10 * dpr;
  const scaleX = (width - pad * 2) / paddedSpanX;
  const scaleY = (height - pad * 2) / paddedSpanY;
  ctx.clearRect(0, 0, width, height);
  ctx.fillStyle = "#0b1220";
  ctx.fillRect(0, 0, width, height);
  if (Array.isArray(envelopes)) {
    envelopes.forEach((env) => {
      const x = pad + (env.cx - minX) * scaleX;
      const y = pad + (env.cy - minY) * scaleY;
      const py = height - y;
      const rx = Math.max(1, env.rx * scaleX);
      const ry = Math.max(1, env.ry * scaleY);
      ctx.save();
      ctx.translate(x, py);
      ctx.rotate(-env.angle);
      ctx.beginPath();
      ctx.ellipse(0, 0, rx, ry, 0, 0, Math.PI * 2);
      ctx.strokeStyle = env.color || groupColors[0];
      ctx.lineWidth = Math.max(1, 1.4 * dpr);
      ctx.globalAlpha = 0.55;
      ctx.stroke();
      ctx.restore();
    });
  }
  const radius = 3 * dpr;
  for (const point of points) {
    const group = point.group >= 0 && point.group < 4 ? point.group : 0;
    const color = groupColors[group] || groupColors[0];
    const x = pad + (point.x - minX) * scaleX;
    const y = pad + (point.y - minY) * scaleY;
    const py = height - y;
    ctx.beginPath();
    ctx.arc(x, py, radius, 0, Math.PI * 2);
    ctx.fillStyle = color;
    ctx.fill();
  }
}

self.addEventListener("message", (event) => {
  const data = event.data || {};
  if (data.type === "init") {
    canvas = data.canvas || null;
    if (Array.isArray(data.colors)) {
      groupColors = data.colors;
    }
    resizeCanvas(data.width || 1, data.height || 1, data.dpr || 1);
    ctx = canvas ? canvas.getContext("2d") : null;
    return;
  }
  if (data.type === "resize") {
    resizeCanvas(data.width || 1, data.height || 1, data.dpr || 1);
    return;
  }
  if (data.type === "render") {
    drawPlot(data.points || [], data.envelopes || []);
  }
});
