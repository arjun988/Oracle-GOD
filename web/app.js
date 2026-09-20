(function () {
  "use strict";

  const MASK64 = (1n << 64n) - 1n;
  const LCG_A = 6364136223846793005n;
  const LCG_C = 1442695040888963407n;
  const STORAGE_KEY = "oracle-chat";

  const timerEl = document.getElementById("timer-hex");
  const latchEl = document.getElementById("latch-hex");
  const vocabMeta = document.getElementById("vocab-meta");
  const logEl = document.getElementById("log");
  const form = document.getElementById("ask-form");
  const okayBtn = document.getElementById("okay");
  const clearBtn = document.getElementById("clear-btn");

  let vocab = Array.isArray(window.ORACLE_VOCAB_FALLBACK)
    ? window.ORACLE_VOCAB_FALLBACK.slice()
    : ["YES", "NO", "WAIT", "LOOK CLOSER"];
  let vocabSource = "embedded fallback";
  let history = [];

  function hex64(n) {
    return n.toString(16).toUpperCase().padStart(16, "0");
  }

  function latchFromEvent(event) {
    const now = performance.now();
    const originNs = BigInt(Math.floor(performance.timeOrigin * 1e6));
    const nowNs = BigInt(Math.floor(now * 1e6));
    const evtNs = BigInt(Math.floor((event && event.timeStamp ? event.timeStamp : now) * 1e6));
    const wall = BigInt(Date.now());
    return (originNs ^ (nowNs << 7n) ^ (evtNs << 13n) ^ (wall << 32n)) & MASK64;
  }

  function templeNext(state, tsc, useTsc) {
    const term1 = (LCG_A * state) & MASK64;
    const term2 = (((state & 0xFFFFFFFF0000n) >> 16n) + LCG_C) & MASK64;
    let transformed = (term1 ^ term2) & MASK64;
    if (useTsc) transformed ^= tsc;
    return transformed & MASK64;
  }

  function consult(event) {
    const tsc = latchFromEvent(event);
    const oracleValue = templeNext(tsc, tsc, true);
    const index = Number(oracleValue % BigInt(vocab.length));
    return {
      tsc,
      oracleValue,
      index,
      answer: vocab[index]
    };
  }

  function parseVocab(text) {
    return text
      .split(/\r?\n/)
      .map(function (line) { return line.trim(); })
      .filter(function (line) { return line && line.charAt(0) !== "#"; });
  }

  function loadVocab() {
    const paths = ["../oracle/vocab.txt", "/oracle/vocab.txt"];
    return (function tryNext(i) {
      if (i >= paths.length) return Promise.resolve();
      return fetch(paths[i], { cache: "no-store" })
        .then(function (res) {
          if (!res.ok) throw new Error("missing");
          return res.text();
        })
        .then(function (text) {
          const words = parseVocab(text);
          if (!words.length) throw new Error("empty");
          vocab = words;
          vocabSource = paths[i];
        })
        .catch(function () { return tryNext(i + 1); });
    })(0);
  }

  function emptyState() {
    logEl.innerHTML = '<p class="empty">No word yet.<br>Hold a question in your head.<br>Press OKAY when the impulse comes.</p>';
  }

  function render() {
    if (!history.length) {
      emptyState();
      return;
    }
    logEl.innerHTML = "";
    history.forEach(function (turn, i) {
      const oracle = document.createElement("article");
      oracle.className = "msg oracle";
      oracle.innerHTML =
        '<span class="who"></span>' +
        '<div class="word"></div>' +
        '<div class="meta"></div>';
      oracle.querySelector(".who").textContent = "Answer " + (i + 1);
      oracle.querySelector(".word").textContent = turn.answer;
      oracle.querySelector(".meta").textContent =
        "latch " + hex64(BigInt(turn.tsc)) +
        "  value " + hex64(BigInt(turn.oracleValue));
      logEl.appendChild(oracle);
    });
    logEl.scrollTop = logEl.scrollHeight;
  }

  function persist() {
    try {
      localStorage.setItem(STORAGE_KEY, JSON.stringify(history));
    } catch (_) { /* ignore quota / private mode */ }
  }

  function restore() {
    try {
      const raw = localStorage.getItem(STORAGE_KEY);
      if (!raw) return;
      const parsed = JSON.parse(raw);
      if (Array.isArray(parsed)) history = parsed;
    } catch (_) {
      history = [];
    }
  }

  function setVocabMeta() {
    vocabMeta.textContent = vocab.length + "  (" + vocabSource + ")";
  }

  form.addEventListener("submit", function (event) {
    event.preventDefault();
    const result = consult(event);
    history.push({
      answer: result.answer,
      tsc: result.tsc.toString(),
      oracleValue: result.oracleValue.toString(),
      at: Date.now()
    });
    latchEl.textContent = hex64(result.tsc);
    persist();
    render();
    okayBtn.focus();
  });

  clearBtn.addEventListener("click", function () {
    history = [];
    persist();
    latchEl.textContent = "----------------";
    render();
    okayBtn.focus();
  });

  function tick() {
    const live = latchFromEvent({ timeStamp: performance.now() });
    timerEl.textContent = hex64(live);
    requestAnimationFrame(tick);
  }

  restore();
  loadVocab().then(function () {
    setVocabMeta();
    render();
  });
  setVocabMeta();
  render();
  tick();

  if (window.__oracleIntroDone !== false) {
    okayBtn.focus();
  } else {
    document.addEventListener("oracle:intro-done", function () {
      okayBtn.focus();
    }, { once: true });
  }
})();
