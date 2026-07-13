const API_URL = `http://${window.location.hostname}:9090/api`;
const POLL_INTERVAL = 2000;
const MAX_HISTORY = 30; // 60s sliding window
const CIRCUMFERENCE = 150.8; // 2 * Math.PI * 24
let currentCores = [];
// Connection title state flags
let isCpuTitleSet = false;
let isGpuTitleSet = false;

const CORE_METRIC_CONFIG = {
    t: { max: 100, unit: '°C' },
    u: { max: 100, unit: '%' },
    c: { max: 5000, unit: 'MHz' }
};

let currentView = 'simple';

const historyData = {
    gpu: [],
    cpu: []
};

const FALLBACK_SCALES = {
    temperature: 105,
    hotspot: 105,
    vram_temp: 105,
    power: 350,
    fan_speed: 5000,
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
        requestAnimationFrame(() => {
            requestAnimationFrame(renderCharts);
        });
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

    // const gradient = ctx.createLinearGradient(0, 0, 0, height);
    // gradient.addColorStop(0, 'rgba(200, 35, 35, 0.25)');
    // gradient.addColorStop(1, 'rgba(200, 35, 35, 0.00)');
    // ctx.fillStyle = gradient;
    // ctx.fill();

    // Dynamic Fill Area using the stroke color passed in
    const gradient = ctx.createLinearGradient(0, 0, 0, height);

    // Hex to RGBA conversion helper or parse raw stroke color
    ctx.save();
    ctx.fillStyle = strokeColor;
    const computedRGB = ctx.fillStyle; // Normalizes hex or CSS color names to standard rgb/rgba

    // Use rgba version for gradient fade
    if (computedRGB.startsWith('rgb')) {
        const rgbaStart = computedRGB.replace('rgb', 'rgba').replace(')', ', 0.25)');
        const rgbaEnd = computedRGB.replace('rgb', 'rgba').replace(')', ', 0.00)');
        gradient.addColorStop(0, rgbaStart);
        gradient.addColorStop(1, rgbaEnd);
    } else {
        gradient.addColorStop(0, strokeColor);
        gradient.addColorStop(1, 'rgba(0, 0, 0, 0)');
    }

    ctx.fillStyle = gradient;
    ctx.fill();
    ctx.restore();

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

    // Dynamically retrieve CSS custom variables from :root
    const rootStyles = getComputedStyle(document.documentElement);
    const gpuColor = rootStyles.getPropertyValue('--accent-red').trim() || 'rgb(200, 35, 35)';
    const cpuColor = rootStyles.getPropertyValue('--accent-cpu').trim() || '#fb923c';

    if (gpuInput) {
        drawCanvasChart('gpu-chart', historyData.gpu, gpuInput.value, gpuColor);
    }
    if (cpuInput) {
        drawCanvasChart('cpu-chart', historyData.cpu, cpuInput.value, cpuColor);
    }
}

const setVal = (id, val) => {
    const el = document.getElementById(id);
    if (el) el.textContent = (val !== null && val !== undefined) ? val : '--';
};

function updateRing(ringId, value, maxVal) {
    const ring = document.getElementById(ringId);
    if (!ring) return;

    // Radius detection based on current element geometry
    const r = ring.getAttribute('r') ? parseFloat(ring.getAttribute('r')) : 24;
    const circumference = 2 * Math.PI * r;

    // Sync dasharray dynamically to match geometry
    ring.style.strokeDasharray = circumference;

    if (value === null || value === undefined || !maxVal || maxVal <= 0) {
        ring.style.strokeDashoffset = circumference;
        return;
    }

    const percentage = Math.min(Math.max(value / maxVal, 0), 1);
    const offset = circumference * (1 - percentage);
    ring.style.strokeDashoffset = offset;
}

/**
 * Sets the GPU card title once per active session
 */
function setGpuTitleOnce(rawName) {
    if (isGpuTitleSet) return; // Skip DOM updates on subsequent ticks

    const titleEl = document.getElementById('gpu-title');
    if (!titleEl) return;

    if (rawName && typeof rawName === 'string' && rawName.trim() !== '') {
        const cleanName = rawName
            .replace(/\s+Graphics/gi, '') // Strips trailing "Graphics" (e.g. Radeon RX 7900 XT Graphics)
            .replace(/\s+Laptop\s+GPU/gi, ' Mobile') // Replaces "Laptop GPU" with "Mobile" if applicable
            .trim();

        titleEl.textContent = cleanName;
        titleEl.title = rawName.trim(); // Shows full raw SDK name on hover
    } else {
        titleEl.textContent = 'GPU Metrics';
        titleEl.removeAttribute('title');
    }

    isGpuTitleSet = true; // Lock state
}

/**
 * Resets GPU title state when connection drops
 */
function resetGpuTitleState() {
    if (!isGpuTitleSet) return;

    isGpuTitleSet = false;
    const titleEl = document.getElementById('gpu-title');
    if (titleEl) {
        titleEl.textContent = 'GPU Metrics';
        titleEl.removeAttribute('title');
    }
}

/**
 * Sets the CPU card title once per active session
 */
function setCpuTitleOnce(rawName, coreCount = 0) {
    if (isCpuTitleSet) return;

    const titleEl = document.getElementById('cpu-title');
    if (!titleEl) return;

    if (rawName && typeof rawName === 'string' && rawName.trim() !== '') {
        let detectedCores = coreCount;

        if (!detectedCores) {
            const match = rawName.match(/(\d+)-Core/i);
            if (match && match[1]) {
                detectedCores = parseInt(match[1], 10);
            }
        }

        const cleanName = rawName
            // 1. Strip iGPU suffixes: "w/ Radeon...", "with Radeon...", "w/ AMD Radeon..."
            .replace(/\s+(w\/|with)\s+.*Radeon.*/gi, '')
            .replace(/\s+Radeon\s+Graphics/gi, '')
            // 2. Strip standard core/processor tags
            .replace(/\s+\d+-Core\s+Processor/gi, '')
            .replace(/\s+\d+-Core/gi, '')
            .replace(/\s+Processor/gi, '')
            .trim();

        const suffix = detectedCores > 0 ? ` (${detectedCores}C)` : '';

        titleEl.textContent = `${cleanName}${suffix}`;
        titleEl.title = rawName.trim(); // Keeps full raw SDK name in tooltip on hover
    } else {
        titleEl.textContent = 'CPU Metrics';
        titleEl.removeAttribute('title');
    }

    isCpuTitleSet = true;
}
/**
 * Resets title state when connection drops
 */
function resetCpuTitleState() {
    if (!isCpuTitleSet) return; // Avoid unnecessary DOM updates if already offline

    isCpuTitleSet = false;
    const titleEl = document.getElementById('cpu-title');
    if (titleEl) {
        titleEl.textContent = 'CPU Metrics';
        titleEl.removeAttribute('title');
    }
}

/**
 * Computes average utilization across all cores in the CPU payload
 */
function calculateCpuAverageLoad(cores) {
    if (!Array.isArray(cores) || cores.length === 0) return null;

    const validCores = cores.filter(c => c && c.u !== undefined && c.u !== null);
    if (validCores.length === 0) return null;

    const totalUsage = validCores.reduce((sum, core) => sum + core.u, 0);
    const avg = totalUsage / validCores.length;

    return Math.round(avg * 10) / 10; // Round to 1 decimal place
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

/**
 * Evaluates standard thermal limits against specific degree targets
 */
function getThermalThreshold(value, warnTemp, critTemp) {
    if (value === null || value === undefined) return '';
    if (value >= critTemp) return 'critical';
    if (value >= warnTemp) return 'warning';
    return '';
}

/**
 * CPU Temperature Threshold Rule:
 * >= 95°C -> Critical (Flashy Red)
 * >= 85°C -> Warning (Amber / Orange)
 */
function getCpuTempThreshold(tempValue) {
    if (tempValue === null || tempValue === undefined) return '';
    if (tempValue >= 95) return 'critical';
    if (tempValue >= 85) return 'warning';
    return '';
}

/**
 * Hardcoded Universal GPU Thermal Rule:
 * >= 100°C -> Critical (Flashy Red)
 * >= 95°C  -> Warning (Orange)
 */
function getGpuTempThreshold(tempValue) {
    if (tempValue === null || tempValue === undefined) return '';
    if (tempValue >= 100) return 'critical';
    if (tempValue >= 95) return 'warning';
    return '';
}

/**
 * Fan Stalled/Zero RPM Threshold:
 * Triggers if Fan < 500 RPM while GPU thermals are high.
 */
function getGpuFanThreshold(fanRpm, gpuTemp, hotspotTemp, vramTemp) {
    if (fanRpm === null || fanRpm === undefined) return '';

    const isFanLowOrOff = fanRpm < 500;
    // Extract highest temperature reading currently reported
    const maxTemp = Math.max(gpuTemp || 0, hotspotTemp || 0, vramTemp || 0);

    if (isFanLowOrOff && maxTemp >= 90) return 'critical';
    if (isFanLowOrOff && maxTemp >= 80) return 'warning';

    return '';
}

/**
 * Helper to apply threshold classes to any box safely
 */
function setBoxThreshold(boxId, thresholdClass) {
    const box = document.getElementById(boxId);
    if (!box) return;

    box.classList.remove('warning', 'critical');
    if (thresholdClass) {
        box.classList.add(thresholdClass);
    }
}

/**
 * Returns 'critical', 'warning', or '' depending on value ratio
 */
function getThresholdClass(value, maxVal, warnPct = 0.85, critPct = 0.95) {
    if (value === null || value === undefined || !maxVal || maxVal <= 0) return '';
    const ratio = value / maxVal;
    if (ratio >= critPct) return 'critical';
    if (ratio >= warnPct) return 'warning';
    return '';
}

function handleMetricDisplay(boxId, valueId, ringId, metricData) {
    const box = document.getElementById(boxId);
    if (box) {
        if (metricData.supported) {
            box.classList.remove('hidden');
        } else {
            box.classList.add('hidden');
        }
        // Always strip threshold classes by default
        box.classList.remove('warning', 'critical');
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

/**
 * Calculates Hotspot Delta (+Δ°C) and applies color thresholds:
 * - Delta < 25°C : White (Default)
 * - 25°C <= Delta < 30°C : Orange / Warning
 * - Delta >= 30°C : Red / Critical
 */
function updateHotspotDelta(edgeTemp, hotspotTemp) {
    const deltaEl = document.getElementById('gpu-hotspot-delta');
    if (!deltaEl) return;

    if (edgeTemp === null || edgeTemp === undefined || hotspotTemp === null || hotspotTemp === undefined) {
        deltaEl.textContent = '';
        deltaEl.className = 'delta-tag';
        return;
    }

    const delta = Math.round((hotspotTemp - edgeTemp) * 10) / 10;

    if (delta > 0) {
        deltaEl.textContent = `(+${delta.toFixed(1)}°)`;

        // Apply Threshold Classes
        deltaEl.className = 'delta-tag';
        if (delta >= 30) {
            deltaEl.classList.add('delta-crit');
        } else if (delta >= 25) {
            deltaEl.classList.add('delta-warn');
        }
    } else {
        deltaEl.textContent = ''; // Displays nothing if zero or negative
        deltaEl.className = 'delta-tag';
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

/**
 * Renders or updates per-core metric DOM elements
 */
function updateCoreDisplays() {
    const container = document.getElementById('cpu-cores-container');
    if (!container) return;

    if (!currentCores || currentCores.length === 0) {
        container.innerHTML = '';
        return;
    }

    const selectedMetric = document.querySelector('input[name="core-metric"]:checked')?.value || 't';
    const config = CORE_METRIC_CONFIG[selectedMetric];

    // Build core nodes if count changes
    if (container.children.length !== currentCores.length) {
        container.innerHTML = currentCores.map((_, idx) => `
            <div class="core-item" id="core-item-${idx}">
                <span class="core-label">Core ${idx}</span>
                <span class="core-value"><span id="core-val-${idx}">--</span> <span class="core-unit">${config.unit}</span></span>
                <div class="core-bar" id="core-bar-${idx}" style="width: 0%;"></div>
            </div>
        `).join('');
    }

    // Update individual values and bar fills
    currentCores.forEach((core, idx) => {
        const valEl = document.getElementById(`core-val-${idx}`);
        const barEl = document.getElementById(`core-bar-${idx}`);
        const boxEl = document.getElementById(`core-item-${idx}`);
        const value = core[selectedMetric];

        if (valEl) valEl.textContent = (value !== undefined && value !== null) ? value.toFixed(1) : '--';
        if (barEl) {
            const pct = Math.min(Math.max((value / config.max) * 100, 0), 100);
            barEl.style.width = `${pct}%`;
        }

        // Apply thermal warning highlight to individual cores when "Temp" mode is selected
        if (boxEl) {
            boxEl.classList.remove('warning', 'critical');
            if (selectedMetric === 't') {
                const threshold = getCpuTempThreshold(value);
                if (threshold) boxEl.classList.add(threshold);
            }
        }

        const unitEl = valEl?.nextElementSibling;
        if (unitEl) unitEl.textContent = config.unit;
    });
}

/**
 * Utility to apply or strip warning/critical classes safely
 */
function applyCustomThreshold(boxId, thresholdClass) {
    const box = document.getElementById(boxId);
    if (!box) return;

    box.classList.remove('warning', 'critical');
    if (thresholdClass) {
        box.classList.add(thresholdClass);
    }
}

async function fetchMetrics() {
    try {
        const response = await fetch(API_URL);
        if (!response.ok) throw new Error(`HTTP ${response.status}`);

        const data = await response.json();
        setApiStatus(true, 'Live');

        // --- GPU METRICS PARSING ---
        if (data.gpu && data.gpu.valid) {
            updateCardState('gpu-card', 'gpu-tag', true);

            // Set GPU Title once per active session
            setGpuTitleOnce(data.gpu.name);

            const gpu = data.gpu;

            // Metric Parsing
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

            // Render Metrics (No automatic warnings)
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

            // Update Hotspot Delta
            updateHotspotDelta(temp.value, hotspot.value);

            // --- APPLY SPECIFIC THRESHOLDS ---

            // 1. Universal GPU Temp Rule (Edge, Hotspot, VRAM)
            setBoxThreshold('box-gpu-temp', getGpuTempThreshold(temp.value));
            setBoxThreshold('box-gpu-hotspot', getGpuTempThreshold(hotspot.value));
            setBoxThreshold('box-gpu-vram-temp', getGpuTempThreshold(vramTemp.value));

            // 2. Fan Speed Protection Rule (<500 RPM while Temps >= 80°C / 90°C)
            // Evaluate combined fan status against temperatures
            const fanThreshold = getGpuFanThreshold(fan.value, temp.value, hotspot.value, vramTemp.value);
            setBoxThreshold('box-gpu-fan', fanThreshold);
            setBoxThreshold('box-gpu-fan-duty', fanThreshold);

            // Push History
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

            resetGpuTitleState();
            // Clear delta when GPU goes offline
            updateHotspotDelta(null, null);
            historyData.gpu.push(null);
        }

        // --- CPU METRICS PARSING ---
        if (data.cpu && (data.cpu.temperature !== null || data.cpu.power !== null || Array.isArray(data.cpu.cores))) {
            updateCardState('cpu-card', 'cpu-tag', true);

            // 1. Update CPU Model Name Header
            currentCores = Array.isArray(data.cpu.cores) ? data.cpu.cores : [];
            setCpuTitleOnce(data.cpu.name, currentCores.length); // Runs once when connected, zero-overhead NOOP afterwards

            // 2. Core Data Parsing & Load Averaging
            const avgLoadValue = calculateCpuAverageLoad(currentCores);
            const cpuUsage = { supported: avgLoadValue !== null, value: avgLoadValue, max: 100 };
            const cpuTemp = { supported: data.cpu.temperature !== null, value: data.cpu.temperature, max: 100 };
            const cpuPower = { supported: data.cpu.power !== null, value: data.cpu.power, max: 150 };

            // 3. Render Displays & Rings
            handleMetricDisplay('box-cpu-usage', 'cpu-usage', 'cpu-usage-ring', cpuUsage);
            handleMetricDisplay('box-cpu-temp', 'cpu-temp', 'cpu-temp-ring', cpuTemp);
            handleMetricDisplay('box-cpu-power', 'cpu-power', 'cpu-power-ring', cpuPower);

            // 4. Thresholds & Core Displays
            setBoxThreshold('box-cpu-temp', getCpuTempThreshold(cpuTemp.value));
            updateCoreDisplays();

            // 5. Push History
            historyData.cpu.push({
                usage: cpuUsage,
                temperature: cpuTemp,
                power: cpuPower
            });
        } else {
            updateCardState('cpu-card', 'cpu-tag', false);

            ['cpu-usage', 'cpu-temp', 'cpu-power'].forEach(id => {
                setVal(id, null);
                updateRing(`${id}-ring`, null, 0);
            });
            resetCpuTitleState();
            setBoxThreshold('box-cpu-temp', '');
            currentCores = [];
            updateCoreDisplays();
            historyData.cpu.push(null);
        }

    } catch (err) {
        setApiStatus(false, 'Offline', 'var(--accent-red)');
        updateCardState('gpu-card', 'gpu-tag', false);
        updateCardState('cpu-card', 'cpu-tag', false);

        currentCores = [];
        updateCoreDisplays();
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