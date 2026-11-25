(function () {
  "use strict";

  const SLOT_COLORS = ["#38bdf8", "#10b981", "#a78bfa", "#f59e0b", "#ec4899", "#14b8a6", "#f97316", "#84cc16"];
  const PRESETS = {
    case1: { slots: 2, jobs: "6 3 5\n3 4 3\n6 5 2\n2 9 6\n8 5 2" },
    case2: { slots: 1, jobs: "2 5 1\n1 2 4\n2 4 2" },
    burst: { slots: 2, jobs: "1 4 5\n1 3 5\n1 2 3\n1 3 2\n1 4 6" },
    heavy: { slots: 3, jobs: "0 5 2\n0 4 3\n0 6 2\n1 3 2\n1 5 4\n2 4 1\n3 3 3\n4 4 2\n5 2 3" }
  };

  let animTimer = null;

  function stopAnimation() {
    if (animTimer !== null) {
      clearInterval(animTimer);
      animTimer = null;
    }
  }

  function simulateFCFS(jobs, numSlots, thresholdPct, style) {
    const sorted = jobs
      .map((j, idx) => ({ arrival: j.arrival, service: j.service, patience: j.patience, id: idx + 1 }))
      .sort((a, b) => a.arrival - b.arrival || a.id - b.id);

    const events = [];
    const outcomes = [];
    const freeSlots = Array.from({ length: numSlots }, (_, i) => i + 1);
    const waitQueue = [];
    const running = [];

    const maxTick = sorted.reduce((m, j) => Math.max(m, j.arrival + j.patience + j.service), 1) + 20;
    let nextIdx = 0;

    for (let t = 0; t <= maxTick; t++) {
      running.sort((a, b) => a.slotId - b.slotId);
      for (let i = running.length - 1; i >= 0; i--) {
        if (running[i].finishTick === t) {
          const done = running.splice(i, 1)[0];
          freeSlots.push(done.slotId);
          freeSlots.sort((a, b) => a - b);
          events.push({
            tick: t,
            type: "completed",
            text: style === "classic"
              ? `Student ${done.job.id} leaves after washing`
              : `[t=${String(t).padStart(3, " ")}] Job ${done.job.id} released slot #${done.slotId} (completed)`
          });
        }
      }

      while (nextIdx < sorted.length && sorted[nextIdx].arrival === t) {
        const job = sorted[nextIdx++];
        events.push({
          tick: t,
          type: "arrived",
          text: style === "classic"
            ? `Student ${job.id} arrives`
            : `[t=${String(t).padStart(3, " ")}] Job ${job.id} arrived`
        });
        waitQueue.push(job);
      }

      while (freeSlots.length > 0 && waitQueue.length > 0) {
        const job = waitQueue.shift();
        const slotId = freeSlots.shift();
        const finishTick = t + job.service;
        events.push({
          tick: t,
          type: "acquired",
          text: style === "classic"
            ? `Student ${job.id} starts washing`
            : `[t=${String(t).padStart(3, " ")}] Job ${job.id} acquired slot #${slotId}`
        });
        outcomes.push({
          jobId: job.id,
          arrival: job.arrival,
          start: t,
          finish: finishTick,
          slotId: slotId,
          served: true
        });
        if (job.service === 0) {
          freeSlots.push(slotId);
          freeSlots.sort((a, b) => a - b);
          events.push({
            tick: t,
            type: "completed",
            text: style === "classic"
              ? `Student ${job.id} leaves after washing`
              : `[t=${String(t).padStart(3, " ")}] Job ${job.id} released slot #${slotId} (completed)`
          });
        } else {
          running.push({ job: job, slotId: slotId, finishTick: finishTick });
        }
      }

      for (let i = 0; i < waitQueue.length; ) {
        if (waitQueue[i].arrival + waitQueue[i].patience === t) {
          const dropped = waitQueue.splice(i, 1)[0];
          events.push({
            tick: t,
            type: "timeout",
            text: style === "classic"
              ? `Student ${dropped.id} leaves without washing`
              : `[t=${String(t).padStart(3, " ")}] Job ${dropped.id} timed out (patience exceeded)`
          });
          outcomes.push({
            jobId: dropped.id,
            arrival: dropped.arrival,
            start: -1,
            finish: t,
            slotId: -1,
            served: false
          });
        } else {
          i++;
        }
      }

      if (nextIdx === sorted.length && waitQueue.length === 0 && running.length === 0) {
        break;
      }
    }

    outcomes.sort((a, b) => a.jobId - b.jobId);
    const total = jobs.length;
    const dropped = outcomes.filter((o) => !o.served).length;
    const dropRate = total > 0 ? dropped / total : 0;
    const makespan = outcomes.reduce((m, o) => Math.max(m, o.finish), 1);
    const busyTicks = outcomes.filter((o) => o.served).reduce((s, o) => s + (o.finish - o.start), 0);
    const avgUtil = numSlots > 0 && makespan > 0 ? busyTicks / (makespan * numSlots) : 0;
    const scaling = total > 0 && dropRate * 100 >= thresholdPct - 1e-9;

    return { events, outcomes, total, dropped, dropRate, makespan, avgUtil, scaling };
  }

  function renderFrame(res, currentTick) {
    document.getElementById("tickCounter").textContent = `t = ${currentTick} / ${res.makespan}`;

    const gantt = document.getElementById("ganttContainer");
    gantt.textContent = "";
    const maxT = Math.max(res.makespan, 1);

    for (const o of res.outcomes) {
      const row = document.createElement("div");
      row.className = "gantt-row";

      const label = document.createElement("div");
      label.className = "gantt-label";
      label.textContent = `Job ${o.jobId} (T=${o.arrival})`;

      const track = document.createElement("div");
      track.className = "gantt-track";
      track.style.setProperty("--tick-w", `${100 / maxT}%`);

      const waitEnd = o.served ? o.start : o.finish;
      const visibleWaitEnd = Math.min(waitEnd, currentTick);
      const waitLeft = (o.arrival / maxT) * 100;
      const waitWidth = Math.max(0, ((visibleWaitEnd - o.arrival) / maxT) * 100);

      if (currentTick >= o.arrival && waitWidth > 0) {
        const waitBar = document.createElement("div");
        waitBar.className = "bar-wait";
        waitBar.style.left = `${waitLeft}%`;
        waitBar.style.width = `${waitWidth}%`;
        track.appendChild(waitBar);
      }

      if (o.served && currentTick >= o.start) {
        const visEnd = Math.min(o.finish, currentTick);
        const actBar = document.createElement("div");
        actBar.className = "bar-active";
        actBar.style.left = `${(o.start / maxT) * 100}%`;
        actBar.style.width = `${Math.max(0.8, ((visEnd - o.start) / maxT) * 100)}%`;
        actBar.style.background = SLOT_COLORS[(o.slotId - 1) % SLOT_COLORS.length];
        actBar.textContent = `S#${o.slotId}`;
        track.appendChild(actBar);
      } else if (!o.served && currentTick >= o.finish) {
        const dropDot = document.createElement("div");
        dropDot.className = "bar-drop";
        dropDot.style.left = `${(o.finish / maxT) * 100}%`;
        dropDot.title = `Timed out at t=${o.finish}`;
        dropDot.textContent = "X";
        track.appendChild(dropDot);
      }

      const cursor = document.createElement("div");
      cursor.className = "cursor-line";
      cursor.style.left = `${(Math.min(currentTick, maxT) / maxT) * 100}%`;
      track.appendChild(cursor);

      row.appendChild(label);
      row.appendChild(track);
      gantt.appendChild(row);
    }

    const axis = document.createElement("div");
    axis.className = "tick-axis";
    const step = Math.max(1, Math.ceil(maxT / 12));
    for (let t = 0; t <= maxT; t += step) {
      const span = document.createElement("span");
      span.textContent = String(t);
      axis.appendChild(span);
    }
    if (maxT % step !== 0) {
      const span = document.createElement("span");
      span.textContent = String(maxT);
      axis.appendChild(span);
    }
    gantt.appendChild(axis);

    const consoleEl = document.getElementById("consoleOut");
    consoleEl.textContent = "";
    const visibleEvents = res.events.filter((e) => e.tick <= currentTick);
    for (const ev of visibleEvents) {
      const line = document.createElement("div");
      line.className = `ev-${ev.type}`;
      line.textContent = ev.text;
      consoleEl.appendChild(line);
    }
    if (currentTick >= res.makespan) {
      const summary = document.createElement("div");
      summary.style.marginTop = "8px";
      summary.style.borderTop = "1px solid #334155";
      summary.style.paddingTop = "6px";
      summary.style.color = "#f8fafc";
      summary.style.fontWeight = "700";
      summary.textContent = `${res.dropped} dropped — Scaling Recommended: ${res.scaling ? "Yes" : "No"}`;
      consoleEl.appendChild(summary);
    }
    consoleEl.scrollTop = consoleEl.scrollHeight;
  }

  function runSimulation() {
    stopAnimation();
    const slots = Math.max(0, parseInt(document.getElementById("slotsInput").value, 10) || 0);
    const threshold = Math.max(0, parseFloat(document.getElementById("thresholdInput").value) || 25);
    const style = document.getElementById("styleSelect").value;
    const speed = parseInt(document.getElementById("speedSelect").value, 10);

    const lines = document.getElementById("jobsInput").value
      .split("\n")
      .map((l) => l.trim())
      .filter(Boolean);

    const jobs = [];
    for (const line of lines) {
      const parts = line.split(/\s+/).map(Number);
      if (parts.length >= 3 && parts.every((n) => !Number.isNaN(n) && n >= 0)) {
        jobs.push({ arrival: parts[0], service: parts[1], patience: parts[2] });
      }
    }

    const res = simulateFCFS(jobs, slots, threshold, style);

    document.getElementById("mDropped").textContent = `${res.dropped} / ${res.total}`;
    document.getElementById("mDropRate").textContent = `${(res.dropRate * 100).toFixed(1)}%`;
    const verdictEl = document.getElementById("mVerdict");
    verdictEl.textContent = res.scaling ? "Yes (Scale Up)" : "No (Sufficient)";
    verdictEl.className = "metric-val " + (res.scaling ? "verdict-yes" : "verdict-no");
    document.getElementById("mUtil").textContent = `${(res.avgUtil * 100).toFixed(1)}%`;

    if (speed === 0) {
      renderFrame(res, res.makespan);
    } else {
      let tick = 0;
      renderFrame(res, tick);
      animTimer = setInterval(() => {
        tick++;
        renderFrame(res, tick);
        if (tick >= res.makespan) {
          stopAnimation();
        }
      }, speed);
    }
  }

  document.querySelectorAll("[data-preset]").forEach((btn) => {
    btn.addEventListener("click", () => {
      const key = btn.getAttribute("data-preset");
      if (PRESETS[key]) {
        document.getElementById("slotsInput").value = PRESETS[key].slots;
        document.getElementById("jobsInput").value = PRESETS[key].jobs;
        runSimulation();
      }
    });
  });

  document.getElementById("runBtn").addEventListener("click", runSimulation);
  document.getElementById("stopBtn").addEventListener("click", stopAnimation);

  runSimulation();
})();
