const API_URL = `http://${window.location.hostname}:9090/api`;
const POLL_INTERVAL = 2000;
const MAX_HISTORY = 30; // 60s sliding window
const CIRCUMFERENCE = 150.8; // 2 * Math.PI * 24

let currentView = 'simple';

const historyData = {
    gpu: [],
    cpu: []
};

const FALLBACK_SCALES = {
    temperature: 100,
    hotspot: 100,
    vram_temp: 100,
    power: 350,
    fan_speed: 6600,
    clock_speed: 4450,
    vram_clock_speed: 3000,
    usage: 100,
    voltage: 1200
};

function setView(mode) {
    currentView = mode;
    document.getElementById('btn-simple').classList.toggle('active', mode === 'simple');
    document.getElementById('btn-advanced').classList.toggle('active', mode === 'advanced');
    document.getElementById('btn-expert').classList.toggle('active', mode === 'expert');

    const isAdvancedOrExpert = mode === 'advanced' || mode === 'expert';
    const isExpert = mode === 'expert';

    document.querySelectorAll('.chart-container').forEach(el => {
        el.classList.toggle('visible', isAdvancedOrExpert);
    });

    document.querySelectorAll('.expert-section').forEach(el => {
        el.classList.toggle('visible', isExpert);
    });

    if (isAdvancedOrExpert) {
        // Delay render slightly so DOM transitions complete layout calculations
        requestAnimationFrame(renderCharts);
    }
}

function drawCanvasChart(canvasId, dataList, metricKey, strokeColor) {
    const canvas = document.getElementById(canvasId);
    if (!canvas) return;

    const rect = canvas.getBoundingClientRect();

    // Safety guard against rendering invisible canvas
    if (rect.width === 0 || rect.height === 0) return;

    const dpr = window.devicePixelRatio || 1;
    canvas.width = rect.width * dpr;
    canvas.height = rect.height * dpr;

    const ctx = canvas.getContext('2d');
    ctx.scale(dpr, dpr);

    const width = rect.width;
    const height = rect.height;

    ctx.clearRect(0, 0, width, height);

    // Grid lines
    ctx.strokeStyle = '#1e1e24';
    ctx.lineWidth = 1;
    ctx.beginPath();
    ctx.moveTo(0, height / 2); ctx.lineTo(width, height / 2);
    ctx.moveTo(0, height / 4); ctx.lineTo(width, height / 4);
    ctx.moveTo(0, (height * 3) / 4); ctx.lineTo(width, (height * 3) / 4);
    ctx.stroke();

    if (dataList.length < 2) return;

    const step = width / (MAX_HISTORY - 1);

    const points = dataList.map((entry, i) => {
        if (!entry || !entry[metricKey]) {
            return { x: i * step, y: height, valid: false };
        }

        const metric = entry[metricKey];
        const val = metric.value;
        const maxVal = metric.max || FALLBACK_SCALES[metricKey] || 100;

        const normalized = (val === null || val === undefined || maxVal <= 0)
            ? 0
            : Math.min(Math.max(val / maxVal, 0), 1);

        const x = i * step;
        const y = height - (normalized * (height - 12)) - 6;
        return { x, y, valid: val !== null && val !== undefined };
    });

    // Fill Area
    ctx.beginPath();
    ctx.moveTo(points[0].x, height);
    points.forEach(p => ctx.lineTo(p.x, p.valid ? p.y : height));
    ctx.lineTo(points[points.length - 1].x, height);
    ctx.closePath();

    const gradient = ctx.createLinearGradient(0, 0, 0, height);
    gradient.addColorStop(0, 'rgba(200, 35, 35, 0.25)');
    gradient.addColorStop(1, 'rgba(200, 35, 35, 0.00)');
    ctx.fillStyle = gradient;
    ctx.fill();

    // Line Stroke
    ctx.beginPath();
    points.forEach((p, i) => {
        if (i === 0) {
            ctx.moveTo(p.x, p.y);
        } else {
            ctx.lineTo(p.x, p.y);
        }
    });
    ctx.strokeStyle = strokeColor;
    ctx.lineWidth = 2;
    ctx.lineCap = 'round';
    ctx.lineJoin = 'round';
    ctx.stroke();
}

function renderCharts() {
    const gpuInput = document.querySelector('input[name="gpu-metric"]:checked');
    const cpuInput = document.querySelector('input[name="cpu-metric"]:checked');

    if (gpuInput) {
        drawCanvasChart('gpu-chart', historyData.gpu, gpuInput.value, 'rgb(200, 35, 35)');
    }
    if (cpuInput) {
        drawCanvasChart('cpu-chart', historyData.cpu, cpuInput.value, '#fca5a5');
    }
}

const setVal = (id, val) => {
    const el = document.getElementById(id);
    if (el) el.textContent = (val !== null && val !== undefined) ? val : '--';
};

function updateRing(ringId, value, maxVal) {
    const ring = document.getElementById(ringId);
    if (!ring) return;

    if (value === null || value === undefined || !maxVal || maxVal <= 0) {
        ring.style.strokeDashoffset = CIRCUMFERENCE;
        return;
    }

    const percentage = Math.min(Math.max(value / maxVal, 0), 1);
    const offset = CIRCUMFERENCE * (1 - percentage);
    ring.style.strokeDashoffset = offset;
}

function parseMetric(metricObj, scaleKey = null) {
    if (!metricObj || typeof metricObj !== 'object') {
        return { supported: false, value: null, min: 0, max: 0 };
    }

    let maxVal = metricObj.max;
    if (!maxVal || maxVal <= 0) {
        maxVal = scaleKey ? FALLBACK_SCALES[scaleKey] : 100;
    }

    return {
        supported: !!metricObj.supported,
        value: metricObj.supported ? metricObj.value : null,
        min: metricObj.min || 0,
        max: maxVal
    };
}

function handleMetricDisplay(boxId, valueId, ringId, metricData) {
    const box = document.getElementById(boxId);
    if (box) {
        if (metricData.supported) {
            box.classList.remove('hidden');
        } else {
            box.classList.add('hidden');
        }
    }

    setVal(valueId, metricData.supported ? metricData.value : null);
    updateRing(ringId, metricData.supported ? metricData.value : null, metricData.max);
}

function updateCardState(cardId, tagId, isOnline) {
    const card = document.getElementById(cardId);
    const tag = document.getElementById(tagId);
    if (card) card.classList.toggle('disabled', !isOnline);
    if (tag) {
        tag.textContent = isOnline ? 'Active' : 'Offline';
        tag.className = `tag ${isOnline ? 'active' : 'inactive'}`;
    }
}

function setApiStatus(online, message, colorOverride = null) {
    const dot = document.getElementById('api-dot');
    const status = document.getElementById('api-status');
    if (status) status.textContent = message;

    if (dot) {
        const color = colorOverride || (online ? 'var(--status-ok)' : 'var(--status-warn)');
        dot.style.backgroundColor = color;
        dot.style.boxShadow = `0 0 8px ${color}`;
    }
}

async function fetchMetrics() {
    try {
        const response = await fetch(API_URL);
        if (!response.ok) throw new Error(`HTTP ${response.status}`);

        const data = await response.json();
        setApiStatus(true, 'Live');

        // --- GPU METRICS ---
        if (data.gpu && data.gpu.valid) {
            updateCardState('gpu-card', 'gpu-tag', true);
            const gpu = data.gpu;

            const temp = parseMetric(gpu.temperature, 'temperature');
            const hotspot = parseMetric(gpu.hotspot, 'hotspot');
            const vramTemp = parseMetric(gpu.memory_temperature, 'vram_temp');

            let power = parseMetric(gpu.power, 'power');
            if (!power.supported || power.value === null) {
                power = parseMetric(gpu.total_board_power, 'power');
            }

            const fan = parseMetric(gpu.fan_speed, 'fan_speed');
            const clock = parseMetric(gpu.clock_speed, 'clock_speed');
            const vramClock = parseMetric(gpu.vram_clock_speed, 'vram_clock_speed');
            const tbp = parseMetric(gpu.total_board_power, 'power');
            const voltage = parseMetric(gpu.voltage, 'voltage');
            const fanDuty = parseMetric(gpu.fan_duty);
            const usage = parseMetric(gpu.usage, 'usage');
            const vram = parseMetric(gpu.vram);
            const sharedMem = parseMetric(gpu.shared_memory);

            // UI Displays
            handleMetricDisplay('box-gpu-temp', 'gpu-temp', 'gpu-temp-ring', temp);
            handleMetricDisplay('box-gpu-hotspot', 'gpu-hotspot', 'gpu-hotspot-ring', hotspot);
            handleMetricDisplay('box-gpu-vram-temp', 'gpu-vram-temp', 'gpu-vram-temp-ring', vramTemp);
            handleMetricDisplay('box-gpu-power', 'gpu-power', 'gpu-power-ring', power);
            handleMetricDisplay('box-gpu-fan', 'gpu-fan', 'gpu-fan-ring', fan);

            handleMetricDisplay('box-gpu-clock', 'gpu-clock', 'gpu-clock-ring', clock);
            handleMetricDisplay('box-gpu-vram-clock', 'gpu-vram-clock', 'gpu-vram-clock-ring', vramClock);
            handleMetricDisplay('box-gpu-tbp', 'gpu-tbp', 'gpu-tbp-ring', tbp);
            handleMetricDisplay('box-gpu-voltage', 'gpu-voltage', 'gpu-voltage-ring', voltage);
            handleMetricDisplay('box-gpu-fan-duty', 'gpu-fan-duty', 'gpu-fan-duty-ring', fanDuty);
            handleMetricDisplay('box-gpu-usage', 'gpu-usage', 'gpu-usage-ring', usage);
            handleMetricDisplay('box-gpu-vram-usage', 'gpu-vram-usage', 'gpu-vram-usage-ring', vram);
            handleMetricDisplay('box-gpu-shared-mem', 'gpu-shared-mem', 'gpu-shared-mem-ring', sharedMem);

            historyData.gpu.push({
                temperature: temp, hotspot: hotspot, vram_temp: vramTemp,
                power: power, fan_speed: fan, clock_speed: clock, usage: usage
            });
        } else {
            updateCardState('gpu-card', 'gpu-tag', false);
            ['gpu-temp', 'gpu-hotspot', 'gpu-vram-temp', 'gpu-power', 'gpu-fan',
                'gpu-clock', 'gpu-vram-clock', 'gpu-tbp', 'gpu-voltage', 'gpu-fan-duty',
                'gpu-usage', 'gpu-vram-usage', 'gpu-shared-mem'].forEach(id => {
                    setVal(id, null);
                    updateRing(`${id}-ring`, null, 0);
                });
            historyData.gpu.push(null);
        }

        // --- CPU METRICS ---
        if (data.cpu && (data.cpu.temperature !== null || data.cpu.power !== null)) {
            updateCardState('cpu-card', 'cpu-tag', true);

            const cpuTemp = { supported: data.cpu.temperature !== null, value: data.cpu.temperature, max: 100 };
            const cpuPower = { supported: data.cpu.power !== null, value: data.cpu.power, max: 150 };

            setVal('cpu-temp', cpuTemp.value);
            setVal('cpu-power', cpuPower.value);
            updateRing('cpu-temp-ring', cpuTemp.value, cpuTemp.max);
            updateRing('cpu-power-ring', cpuPower.value, cpuPower.max);

            historyData.cpu.push({
                temperature: cpuTemp,
                power: cpuPower
            });
        } else {
            updateCardState('cpu-card', 'cpu-tag', false);
            ['cpu-temp', 'cpu-power'].forEach(id => {
                setVal(id, null);
                updateRing(`${id}-ring`, null, 0);
            });
            historyData.cpu.push(null);
        }

    } catch (err) {
        setApiStatus(false, 'Offline', 'var(--accent-red)');
        updateCardState('gpu-card', 'gpu-tag', false);
        updateCardState('cpu-card', 'cpu-tag', false);

        historyData.gpu.push(null);
        historyData.cpu.push(null);
    }

    // Window History Cap
    if (historyData.gpu.length > MAX_HISTORY) historyData.gpu.shift();
    if (historyData.cpu.length > MAX_HISTORY) historyData.cpu.shift();

    if (currentView === 'advanced' || currentView === 'expert') {
        renderCharts();
    }
}

window.addEventListener('resize', () => {
    if (currentView === 'advanced' || currentView === 'expert') renderCharts();
});

fetchMetrics();
setInterval(fetchMetrics, POLL_INTERVAL);