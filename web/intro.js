(function () {
  "use strict";

  window.__oracleIntroDone = false;

  var overlay = document.getElementById("intro");
  var canvas = document.getElementById("intro-canvas");
  var skipEl = document.getElementById("intro-skip");
  var finished = false;

  function finish() {
    if (finished) return;
    finished = true;
    window.__oracleIntroDone = true;
    if (overlay) {
      overlay.classList.add("gone");
      window.setTimeout(function () {
        if (overlay.parentNode) overlay.parentNode.removeChild(overlay);
      }, 800);
    }
    document.dispatchEvent(new CustomEvent("oracle:intro-done"));
  }

  var reduced = window.matchMedia && window.matchMedia("(prefers-reduced-motion: reduce)").matches;
  if (!overlay || !canvas || !canvas.getContext || reduced) {
    finish();
    return;
  }

  var ctx = canvas.getContext("2d");
  var MONO = '"Consolas", "Segoe UI Symbol", "Courier New", monospace';

  // Elder Futhark runes, planetary / alchemical signs, crosses and sigils.
  var RUNES = "\u16A0\u16A2\u16A6\u16A8\u16B1\u16B2\u16B7\u16B9\u16BA\u16BE\u16C1\u16C3\u16C7\u16C8\u16C9\u16CB\u16CF\u16D2\u16D6\u16D7\u16DA\u16DC\u16DE\u16DF";
  var CELESTIAL = "\u2609\u263D\u263F\u2640\u2642\u2643\u2644\u26E2\u2646\u2295\u2736\u2739";
  var OCCULT = "\u2625\u2626\u2627\u2628\u2629\u262A\u262C\u262F\u269A\u269B\u269D\u26E4\u26E7\u2720\u2721\u271D";
  var GREEK = "\u0391\u0392\u0393\u0394\u0398\u039B\u039E\u03A0\u03A3\u03A6\u03A8\u03A9";
  var HEXDIGITS = "0123456789ABCDEF";

  var RAIN_SET = (RUNES + GREEK + HEXDIGITS + CELESTIAL).split("");
  var RING_SET = (CELESTIAL + OCCULT + RUNES).split("");
  var BURST_SET = (OCCULT + CELESTIAL + RUNES + GREEK).split("");

  // Kamea of Saturn: rows, columns and diagonals each sum to 15.
  var KAMEA = [[4, 9, 2], [3, 5, 7], [8, 1, 6]];

  // 16 x 24 pixel hand, fingers up, palm and thumb facing right.
  var HAND = [
    "................",
    "....OOOOO.......",
    "...OSSSSSO......",
    "...OSSSSSO......",
    "...OSSSSSO......",
    "...OSSSSSO......",
    "...OSSSSSO.OO...",
    "...OSSSSSO.OSO..",
    "...OSSSSSOOSSO..",
    "...OSSSSSSSSSO..",
    "...OSSSSSSSSSO..",
    "..OSSSSSSSSSSO..",
    "..OSSSSSSSSSO...",
    "..OSSSSSSSSSO...",
    "..OSSSSSSSSO....",
    "..OSSSSSSSSO....",
    "...OSSSSSSSO....",
    "...OSSSSSSO.....",
    "...OSSSSSO......",
    "....OSSSSO......",
    "....OSSSSO......",
    "....OSSSSO......",
    "....OOOOOO......",
    "................"
  ];
  var HAND_W = 16;
  var HAND_H = 24;

  var RAIN_IN = 300;
  var CIRCLE_IN = 550;
  var KAMEA_IN = 800;
  var HANDS_IN = 900;
  var HEPTA_IN = 950;
  var HEXA_IN = 1150;
  var PENTA_IN = 1350;
  var TRIANGLE_IN = 1550;
  var ARC_IN = 1700;
  var IMPACT = 2400;
  var TITLE_IN = 2650;
  var FADE_IN = 3700;
  var END = 4500;

  var w = 0, h = 0, unit = 0, cell = 0, centerX = 0, centerY = 0;
  var cols = [];
  var embers = [];
  var particles = [];
  var dissolve = [];
  var dissolveStep = 0;
  var leftPalm = 0, rightPalm = 0;
  var burst = false;
  var start = 0;
  var last = 0;

  function pick(list) { return list[(Math.random() * list.length) | 0]; }

  function resize() {
    var dpr = Math.min(window.devicePixelRatio || 1, 2);
    w = window.innerWidth;
    h = window.innerHeight;
    canvas.width = Math.floor(w * dpr);
    canvas.height = Math.floor(h * dpr);
    canvas.style.width = w + "px";
    canvas.style.height = h + "px";
    ctx.setTransform(dpr, 0, 0, dpr, 0, 0);

    // Hands sit in the upper area so the title always fits underneath.
    unit = Math.max(3, Math.floor(Math.min(w / 46, h / 46)));
    cell = Math.max(12, Math.floor(unit * 1.6));
    centerX = w / 2;
    centerY = Math.round(h * 0.38);

    buildRain();
    buildEmbers();
    buildDissolve();
  }

  // ---------- pixel primitives ----------

  function pixelLine(x0, y0, x1, y1, size) {
    var dx = x1 - x0, dy = y1 - y0;
    var dist = Math.sqrt(dx * dx + dy * dy);
    var steps = Math.max(2, Math.ceil(dist / (size * 0.75)));
    for (var i = 0; i <= steps; i++) {
      ctx.fillRect(Math.round(x0 + (dx * i) / steps), Math.round(y0 + (dy * i) / steps), size, size);
    }
  }

  function pixelEllipse(rx, ry, size, steps) {
    for (var i = 0; i < steps; i++) {
      var a = (i / steps) * Math.PI * 2;
      ctx.fillRect(Math.round(centerX + Math.cos(a) * rx), Math.round(centerY + Math.sin(a) * ry), size, size);
    }
  }

  // Star polygon {n/step}: pentagram {5/2}, hexagram {6/2}, heptagram {7/3}.
  function pixelStar(n, step, rx, ry, rot, size) {
    var visited = [];
    for (var s = 0; s < n; s++) visited.push(false);
    for (var s2 = 0; s2 < n; s2++) {
      if (visited[s2]) continue;
      var i = s2;
      do {
        visited[i] = true;
        var j = (i + step) % n;
        var a1 = rot + (i / n) * Math.PI * 2 - Math.PI / 2;
        var a2 = rot + (j / n) * Math.PI * 2 - Math.PI / 2;
        pixelLine(
          centerX + Math.cos(a1) * rx, centerY + Math.sin(a1) * ry,
          centerX + Math.cos(a2) * rx, centerY + Math.sin(a2) * ry,
          size
        );
        i = j;
      } while (i !== s2);
    }
  }

  // ---------- background ----------

  function resetColumn(col, above) {
    col.y = above ? -Math.random() * h : -Math.random() * cell * 8;
    col.speed = cell * (2.5 + Math.random() * 5);
    col.color = pick(["#55ffff", "#55ff55", "#5555ff", "#ffff55", "#aa00aa"]);
    var len = 6 + ((Math.random() * 11) | 0);
    col.chars = [];
    for (var i = 0; i < len; i++) col.chars.push(pick(RAIN_SET));
  }

  function buildRain() {
    cols = [];
    var count = Math.ceil(w / cell);
    for (var i = 0; i < count; i++) {
      var col = { x: i * cell, y: 0, speed: 0, color: "#55ffff", chars: [] };
      resetColumn(col, true);
      cols.push(col);
    }
  }

  function buildEmbers() {
    embers = [];
    var count = Math.min(120, Math.floor((w * h) / 16000));
    for (var i = 0; i < count; i++) {
      embers.push({
        x: Math.random() * w,
        y: Math.random() * h,
        vy: -(unit * (0.6 + Math.random() * 2.2)),
        size: Math.random() < 0.8 ? 1 : 2,
        alpha: 0.2 + Math.random() * 0.6
      });
    }
  }

  function buildDissolve() {
    dissolve = [];
    dissolveStep = Math.max(8, unit * 2);
    var cx = Math.ceil(w / dissolveStep);
    var cy = Math.ceil(h / dissolveStep);
    for (var y = 0; y < cy; y++) {
      for (var x = 0; x < cx; x++) dissolve.push({ x: x * dissolveStep, y: y * dissolveStep });
    }
    for (var i = dissolve.length - 1; i > 0; i--) {
      var j = (Math.random() * (i + 1)) | 0;
      var tmp = dissolve[i]; dissolve[i] = dissolve[j]; dissolve[j] = tmp;
    }
  }

  function drawEmbers(dt) {
    for (var i = 0; i < embers.length; i++) {
      var e = embers[i];
      e.y += e.vy * dt;
      if (e.y < -4) { e.y = h + 4; e.x = Math.random() * w; }
      ctx.globalAlpha = e.alpha;
      ctx.fillStyle = "#aa5500";
      ctx.fillRect(e.x | 0, e.y | 0, e.size * 2, e.size * 2);
    }
    ctx.globalAlpha = 1;
  }

  function drawRain(dt, alpha) {
    if (alpha <= 0) return;
    ctx.font = cell + "px " + MONO;
    ctx.textBaseline = "top";
    ctx.textAlign = "left";
    for (var i = 0; i < cols.length; i++) {
      var col = cols[i];
      col.y += col.speed * dt;
      if (Math.random() < 0.1) col.chars[(Math.random() * col.chars.length) | 0] = pick(RAIN_SET);
      for (var k = 0; k < col.chars.length; k++) {
        var y = col.y - k * cell;
        if (y < -cell || y > h) continue;
        ctx.globalAlpha = alpha * (1 - k / col.chars.length) * 0.45;
        ctx.fillStyle = k === 0 ? "#ffffff" : col.color;
        ctx.fillText(col.chars[k], col.x, y);
      }
      if (col.y - col.chars.length * cell > h) resetColumn(col, false);
    }
    ctx.globalAlpha = 1;
  }

  // ---------- summoning apparatus ----------

  function blowout(t) {
    return t > IMPACT ? Math.min(1, (t - IMPACT) / 700) : 0;
  }

  // Double circle with a rotating band of signs: the circle the operator
  // stands inside, drawn to establish authority and hold the working.
  function drawMagicCircle(t) {
    if (t < CIRCLE_IN) return;
    var grow = Math.min(1, (t - CIRCLE_IN) / 800);
    var blown = blowout(t);
    var fade = 1 - blown;
    if (fade <= 0) return;

    var tension = Math.min(1, Math.max(0, (t - HANDS_IN) / (IMPACT - HANDS_IN)));
    var scale = grow * (1 + blown * 2.4);
    var outer = unit * 17 * scale;
    var inner = unit * 14.5 * scale;
    var dot = Math.max(1, Math.floor(unit / 2));

    ctx.fillStyle = "#ffff55";
    ctx.globalAlpha = fade * 0.55;
    pixelEllipse(outer * 1.35, outer * 0.8, dot, 150);
    pixelEllipse(inner * 1.35, inner * 0.8, dot, 140);

    var band = ((outer + inner) / 2);
    var spin = t / 1400 + tension * 3.5;
    var n = 28;
    ctx.textAlign = "center";
    ctx.textBaseline = "middle";
    ctx.font = "bold " + Math.max(11, unit * 1.5) + "px " + MONO;
    for (var i = 0; i < n; i++) {
      var a = (i / n) * Math.PI * 2 + spin;
      ctx.globalAlpha = fade * (0.35 + 0.5 * Math.abs(Math.sin(a * 2 + t / 260)));
      ctx.fillStyle = i % 3 === 0 ? "#ffff55" : i % 3 === 1 ? "#55ffff" : "#aa00aa";
      ctx.fillText(
        RING_SET[i % RING_SET.length],
        centerX + Math.cos(a) * band * 1.35,
        centerY + Math.sin(a) * band * 0.8
      );
    }
    ctx.globalAlpha = 1;
  }

  // Heptagram and heptagon: the sevenfold figure.
  // Hexagram: shown to compel. Pentagram: worn for protection.
  function drawStars(t) {
    var blown = blowout(t);
    var fade = 1 - blown;
    if (fade <= 0) return;
    var scale = 1 + blown * 2.4;
    var dot = Math.max(1, Math.floor(unit * 0.55));

    if (t >= HEPTA_IN) {
      var p7 = Math.min(1, (t - HEPTA_IN) / 500);
      var r7 = unit * 12.5 * p7 * scale;
      ctx.globalAlpha = fade * 0.75;
      ctx.fillStyle = "#55ffff";
      pixelStar(7, 3, r7 * 1.3, r7 * 0.78, t / 2600, dot);
      ctx.globalAlpha = fade * 0.4;
      pixelStar(7, 1, r7 * 1.3, r7 * 0.78, t / 2600, dot);
    }

    if (t >= HEXA_IN) {
      var p6 = Math.min(1, (t - HEXA_IN) / 500);
      var r6 = unit * 9 * p6 * scale;
      ctx.globalAlpha = fade * 0.85;
      ctx.fillStyle = "#ffff55";
      pixelStar(6, 2, r6 * 1.3, r6 * 0.78, -t / 1800, dot);
    }

    if (t >= PENTA_IN) {
      var p5 = Math.min(1, (t - PENTA_IN) / 500);
      var r5 = unit * 5.6 * p5 * scale;
      ctx.globalAlpha = fade * 0.95;
      ctx.fillStyle = "#aa00aa";
      pixelStar(5, 2, r5 * 1.3, r5 * 0.78, t / 1200, dot);
    }

    ctx.globalAlpha = 1;
  }

  // Triangle of art: the figure outside the circle that the working is
  // commanded into. Here it frames the point where the hands meet.
  function drawTriangle(t) {
    if (t < TRIANGLE_IN) return;
    var blown = blowout(t);
    var fade = 1 - blown;
    if (fade <= 0) return;
    var p = Math.min(1, (t - TRIANGLE_IN) / 500);
    var r = unit * 20 * p * (1 + blown * 2.2);
    var pulse = 0.65 + 0.35 * Math.sin(t / 150);
    var dot = Math.max(1, Math.floor(unit * 0.6));

    ctx.globalAlpha = fade * pulse * 0.8;
    ctx.fillStyle = "#55ff55";
    var pts = [];
    for (var i = 0; i < 3; i++) {
      var a = -Math.PI / 2 + (i / 3) * Math.PI * 2;
      pts.push({ x: centerX + Math.cos(a) * r * 1.3, y: centerY + Math.sin(a) * r * 0.8 });
    }
    for (var j = 0; j < 3; j++) {
      var q = pts[(j + 1) % 3];
      pixelLine(pts[j].x, pts[j].y, q.x, q.y, dot);
    }
    ctx.globalAlpha = 1;
  }

  // Kamea of Saturn: the bare 3x3 square, no seal traced over it.
  function drawKamea(t, side) {
    if (t < KAMEA_IN) return;
    var blown = blowout(t);
    var fade = (1 - blown) * Math.min(1, (t - KAMEA_IN) / 600);
    if (fade <= 0) return;

    var step = unit * 2.4;
    var ox = centerX + side * (unit * 26 + Math.sin(t / 900 + side) * unit);
    var oy = centerY + side * unit * 3;
    if (ox < step * 2 || ox > w - step * 2) return;

    var dot = Math.max(1, Math.floor(unit * 0.4));
    ctx.globalAlpha = fade * 0.35;
    ctx.fillStyle = "#5555ff";
    for (var g = 0; g <= 3; g++) {
      pixelLine(ox - step * 1.5, oy - step * 1.5 + g * step, ox + step * 1.5, oy - step * 1.5 + g * step, dot);
      pixelLine(ox - step * 1.5 + g * step, oy - step * 1.5, ox - step * 1.5 + g * step, oy + step * 1.5, dot);
    }

    ctx.globalAlpha = fade * 0.65;
    ctx.fillStyle = "#55ffff";
    ctx.textAlign = "center";
    ctx.textBaseline = "middle";
    ctx.font = Math.max(10, unit * 1.2) + "px " + MONO;
    for (var r = 0; r < 3; r++) {
      for (var c = 0; c < 3; c++) {
        ctx.fillText(String(KAMEA[r][c]), ox + (c - 1) * step, oy + (r - 1) * step);
      }
    }

    ctx.globalAlpha = 1;
  }

  // ---------- hands ----------

  function drawHand(originX, originY, mirrored, bodyColor, edgeColor) {
    for (var r = 0; r < HAND_H; r++) {
      var row = HAND[r];
      for (var c = 0; c < HAND_W; c++) {
        var ch = row.charAt(c);
        if (ch === ".") continue;
        var col = mirrored ? HAND_W - 1 - c : c;
        ctx.fillStyle = ch === "O" ? edgeColor : bodyColor;
        ctx.fillRect(originX + col * unit, originY + r * unit, unit, unit);
      }
    }
  }

  function easeInCubic(p) { return p * p * p; }

  function drawHands(t) {
    if (t < HANDS_IN) return;
    var handW = HAND_W * unit;
    var handH = HAND_H * unit;
    var topY = centerY - handH / 2;

    var leftEnd = centerX - handW + unit * 2;
    var rightEnd = centerX - unit * 2;
    var leftStart = -handW - w * 0.2;
    var rightStart = w + w * 0.2;

    var p = Math.min(1, (t - HANDS_IN) / (IMPACT - HANDS_IN));
    var e = easeInCubic(p);
    var lx = leftStart + (leftEnd - leftStart) * e;
    var rx = rightStart + (rightEnd - rightStart) * e;

    if (t >= IMPACT) {
      var k = t - IMPACT;
      var recoil = Math.exp(-k / 130) * Math.sin(k / 38) * unit * 1.1;
      lx = leftEnd - recoil;
      rx = rightEnd + recoil;
    }

    leftPalm = lx + (HAND_W - 2) * unit;
    rightPalm = rx + 2 * unit;

    var hot = t >= IMPACT && t < IMPACT + 260;
    var body = hot ? "#ffffff" : "#55ffff";
    var edge = hot ? "#ffff55" : "#5555ff";

    ctx.globalAlpha = 0.35;
    drawHand(Math.round(lx) + unit, Math.round(topY) + unit, false, "#aa0000", "#aa0000");
    drawHand(Math.round(rx) - unit, Math.round(topY) + unit, true, "#aa0000", "#aa0000");
    ctx.globalAlpha = 1;

    drawHand(Math.round(lx), Math.round(topY), false, body, edge);
    drawHand(Math.round(rx), Math.round(topY), true, body, edge);
  }

  function drawArcs(t) {
    if (t < ARC_IN || t > IMPACT + 200) return;
    var gap = rightPalm - leftPalm;
    if (gap <= 0) return;
    var intensity = Math.min(1, (t - ARC_IN) / (IMPACT - ARC_IN));
    var bolts = 1 + Math.floor(intensity * 3);
    var block = Math.max(1, Math.floor(unit * 0.7));

    for (var b = 0; b < bolts; b++) {
      if (Math.random() > 0.55 + intensity * 0.4) continue;
      var steps = 14;
      var y = centerY + (Math.random() - 0.5) * HAND_H * unit * 0.45;
      ctx.fillStyle = b === 0 ? "#ffffff" : (Math.random() < 0.5 ? "#55ffff" : "#ffff55");
      ctx.globalAlpha = 0.55 + intensity * 0.45;
      for (var s = 0; s <= steps; s++) {
        var x = leftPalm + (gap * s) / steps;
        y += (Math.random() - 0.5) * unit * 2.6;
        ctx.fillRect(Math.round(x), Math.round(y), block, block);
      }
    }
    ctx.globalAlpha = 1;
  }

  // ---------- impact ----------

  function spawnBurst() {
    for (var i = 0; i < 130; i++) {
      var a = Math.random() * Math.PI * 2;
      var speed = unit * (7 + Math.random() * 30);
      particles.push({
        x: centerX,
        y: centerY,
        vx: Math.cos(a) * speed,
        vy: Math.sin(a) * speed * 0.7,
        life: 0.8 + Math.random() * 1.1,
        age: 0,
        glyph: pick(BURST_SET),
        color: pick(["#55ffff", "#55ff55", "#5555ff", "#ffff55", "#aa00aa"]),
        size: Math.max(12, unit * (1.2 + Math.random() * 1.8))
      });
    }
  }

  function drawParticles(dt) {
    ctx.textAlign = "center";
    ctx.textBaseline = "middle";
    for (var i = particles.length - 1; i >= 0; i--) {
      var p = particles[i];
      p.age += dt;
      if (p.age >= p.life) { particles.splice(i, 1); continue; }
      p.x += p.vx * dt;
      p.y += p.vy * dt;
      p.vy += unit * 12 * dt;
      ctx.globalAlpha = 1 - p.age / p.life;
      ctx.fillStyle = p.color;
      ctx.font = "bold " + p.size + "px " + MONO;
      ctx.fillText(p.glyph, p.x, p.y);
    }
    ctx.globalAlpha = 1;
  }

  function drawShock(t) {
    var k = t - IMPACT;
    if (k < 0) return;
    var waves = [0, 160, 340];
    for (var wv = 0; wv < waves.length; wv++) {
      var age = k - waves[wv];
      if (age < 0 || age > 900) continue;
      var progress = age / 900;
      var radius = progress * Math.max(w, h) * 0.8;
      ctx.globalAlpha = (1 - progress) * 0.8;
      ctx.fillStyle = wv === 0 ? "#ffff55" : wv === 1 ? "#55ffff" : "#aa00aa";
      pixelEllipse(radius, radius * 0.72, unit, 90);
    }
    ctx.globalAlpha = 1;
  }

  function drawBeam(t) {
    if (t < IMPACT) return;
    var k = Math.min(1, (t - IMPACT) / 500);
    var width = unit * 2 * (1 + Math.sin(t / 90) * 0.25);
    ctx.globalAlpha = 0.3 * k;
    ctx.fillStyle = "#ffff55";
    ctx.fillRect(centerX - width / 2, 0, width, h);
    ctx.globalAlpha = 0.14 * k;
    ctx.fillStyle = "#55ffff";
    ctx.fillRect(centerX - width * 1.8, 0, width * 3.6, h);
    ctx.globalAlpha = 1;
  }

  function drawTitle(t) {
    if (t < TITLE_IN) return;
    var title = "ORACLE";
    var shown = Math.min(title.length, Math.floor((t - TITLE_IN) / 70));
    var size = Math.max(18, Math.min(w / 9, unit * 4));
    var subSize = Math.max(11, size * 0.42);
    var handBottom = centerY + (HAND_H * unit) / 2;
    var bottomLimit = h - 46 - subSize * 1.6 - size / 2;
    var y = Math.min(handBottom + size * 0.85, bottomLimit);

    ctx.textAlign = "center";
    ctx.textBaseline = "middle";
    ctx.font = "bold " + size + "px " + MONO;
    var text = title.slice(0, shown) + (Math.floor(t / 250) % 2 ? "\u2588" : "");
    ctx.fillStyle = "#aa0000";
    ctx.fillText(text, centerX + 3, y + 3);
    ctx.fillStyle = "#ffff55";
    ctx.fillText(text, centerX, y);

    if (shown >= title.length) {
      var subAlpha = Math.min(1, (t - TITLE_IN - title.length * 70) / 400);
      ctx.globalAlpha = subAlpha;
      ctx.font = subSize + "px " + MONO;
      ctx.fillStyle = "#55ff55";
      ctx.fillText("GOD CAN PUPPET YOU", centerX, y + size * 0.62 + subSize);
      ctx.globalAlpha = 1;
    }
  }

  function drawFlash(t) {
    var k = t - IMPACT;
    if (k < 0 || k > 420) return;
    ctx.globalAlpha = Math.pow(1 - k / 420, 2);
    ctx.fillStyle = "#ffffff";
    ctx.fillRect(0, 0, w, h);
    ctx.globalAlpha = 1;
  }

  function drawDissolve(t) {
    if (t < FADE_IN) return;
    var p = Math.min(1, (t - FADE_IN) / (END - FADE_IN));
    var count = Math.floor(dissolve.length * p);
    ctx.fillStyle = "#000000";
    for (var i = 0; i < count; i++) {
      ctx.fillRect(dissolve[i].x, dissolve[i].y, dissolveStep, dissolveStep);
    }
  }

  function frame(now) {
    if (finished) return;
    if (!start) { start = now; last = now; }
    var t = now - start;
    var dt = Math.min(0.05, (now - last) / 1000);
    last = now;

    ctx.fillStyle = "#000000";
    ctx.fillRect(0, 0, w, h);

    drawEmbers(dt);

    var rainAlpha = Math.min(1, Math.max(0, (t - RAIN_IN) / 500));
    if (t > IMPACT) rainAlpha *= 0.5;
    drawRain(dt, rainAlpha);

    drawKamea(t, -1);
    drawKamea(t, 1);
    drawMagicCircle(t);
    drawTriangle(t);
    drawStars(t);

    drawShock(t);
    drawBeam(t);
    drawArcs(t);
    drawHands(t);

    if (!burst && t >= IMPACT) { burst = true; spawnBurst(); }
    drawParticles(dt);

    drawTitle(t);
    drawFlash(t);
    drawDissolve(t);

    if (t >= END) { finish(); return; }
    requestAnimationFrame(frame);
  }

  function skip(event) {
    if (event) event.preventDefault();
    finish();
  }

  window.addEventListener("resize", resize);
  overlay.addEventListener("pointerdown", skip);
  window.addEventListener("keydown", function (event) {
    if (!finished) skip(event);
  });

  resize();
  requestAnimationFrame(frame);
  window.setTimeout(function () {
    if (!finished && skipEl) skipEl.classList.add("show");
  }, 900);
})();
