let stationData = [];

document.addEventListener("DOMContentLoaded", () => {
    fetchStations();
});

async function fetchStations() {
    try {
        const response = await fetch('/api/stations');
        if (!response.ok) throw new Error('Netzwerk-Fehler');
        const data = await response.json();
        stationData = data.stations || [];
        renderGrid();
    } catch (error) {
        console.error('Fehler beim Laden der Sender:', error);
        showStatus('Fehler beim Laden der Sender!', 'error');
    }
}

function renderGrid() {
    const grid = document.getElementById('stationGrid');
    grid.innerHTML = '';

    for (let i = 0; i < 10; i++) {
        // Fallback if less than 10 stations exist
        const station = stationData[i] || { name: "", url: "" };
        
        const card = document.createElement('div');
        card.className = 'station-card';
        card.innerHTML = `
            <div class="card-header">
                <h3>Station ${i + 1}</h3>
                <button class="btn-play" onclick="playStation(${i})" title="Sofort abspielen">▶</button>
            </div>
            <div class="input-group">
                <label>Sendername</label>
                <input type="text" id="name_${i}" value="${station.name}" placeholder="z.B. SWR3">
            </div>
            <div class="input-group">
                <label>Stream URL</label>
                <input type="text" id="url_${i}" value="${station.url}" placeholder="http://...">
            </div>
        `;
        grid.appendChild(card);
    }
}

async function saveStations() {
    const btn = document.getElementById('saveBtn');
    btn.innerHTML = '⏳ Speichern...';
    btn.disabled = true;

    const newStations = [];
    for (let i = 0; i < 10; i++) {
        newStations.push({
            name: document.getElementById(`name_${i}`).value.trim(),
            url: document.getElementById(`url_${i}`).value.trim()
        });
    }

    try {
        const response = await fetch('/api/stations', {
            method: 'POST',
            headers: { 'Content-Type': 'application/json' },
            body: JSON.stringify({ stations: newStations })
        });

        if (response.ok) {
            showStatus('Sender erfolgreich gespeichert!', 'success');
        } else {
            throw new Error('Server-Fehler');
        }
    } catch (error) {
        showStatus('Fehler beim Speichern!', 'error');
    } finally {
        btn.innerHTML = '💾 Sender speichern';
        btn.disabled = false;
    }
}

async function playStation(index) {
    try {
        await fetch('/api/play', {
            method: 'POST',
            headers: { 'Content-Type': 'application/json' },
            body: JSON.stringify({ index: index })
        });
        showStatus(`Station ${index+1} wird abgespielt...`, 'success');
    } catch (error) {
        showStatus('Fehler beim Abspielen!', 'error');
    }
}

async function stopRadio() {
    try {
        await fetch('/api/stop', { method: 'POST' });
        showStatus('Radio gestoppt', 'success');
    } catch (error) {
        showStatus('Fehler beim Stoppen!', 'error');
    }
}

function showStatus(text, type) {
    const msg = document.getElementById('statusMsg');
    msg.textContent = text;
    msg.className = 'status-message show status-' + type;
    setTimeout(() => {
        msg.className = 'status-message status-' + type;
    }, 3000);
}
