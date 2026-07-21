document.addEventListener('DOMContentLoaded', () => {
    const stationsList = document.getElementById('stationsList');
    const saveBtn = document.getElementById('saveBtn');
    const statusMessage = document.getElementById('statusMessage');
    
    // Number of preset buttons on the RT200
    const MAX_STATIONS = 10;
    
    // Initialize empty form
    function initForm(data) {
        stationsList.innerHTML = '';
        const stations = data.stations || [];
        
        for (let i = 0; i < MAX_STATIONS; i++) {
            const station = stations[i] || { name: '', url: '' };
            
            const card = document.createElement('div');
            card.className = 'station-card';
            
            card.innerHTML = `
                <div class="station-num">${i}</div>
                <div class="input-group">
                    <label>Sendername</label>
                    <input type="text" id="name_${i}" value="${station.name}" placeholder="z.B. SWR3">
                </div>
                <div class="input-group">
                    <label>Stream-URL</label>
                    <input type="url" id="url_${i}" value="${station.url}" placeholder="http://...">
                </div>
            `;
            
            stationsList.appendChild(card);
        }
    }

    // Fetch existing stations
    function fetchStations() {
        fetch('/api/stations')
            .then(res => res.json())
            .then(data => initForm(data))
            .catch(err => {
                console.error('Fehler beim Laden:', err);
                initForm({ stations: [] }); // Fallback
            });
    }

    // Save configurations
    saveBtn.addEventListener('click', () => {
        saveBtn.style.transform = 'scale(0.95)';
        setTimeout(() => saveBtn.style.transform = 'none', 150);

        const stations = [];
        for (let i = 0; i < MAX_STATIONS; i++) {
            const name = document.getElementById(`name_${i}`).value.trim();
            const url = document.getElementById(`url_${i}`).value.trim();
            if (name || url) {
                stations.push({ name, url });
            } else {
                stations.push({ name: "", url: "" });
            }
        }

        const data = { stations };

        fetch('/api/stations', {
            method: 'POST',
            headers: {
                'Content-Type': 'application/json'
            },
            body: JSON.stringify(data)
        })
        .then(res => {
            if (res.ok) {
                showStatus('Konfiguration erfolgreich gespeichert!', '#00f2fe');
            } else {
                showStatus('Fehler beim Speichern!', '#ff4757');
            }
        })
        .catch(err => {
            console.error('Speicherfehler:', err);
            showStatus('Verbindungsfehler!', '#ff4757');
        });
    });

    function showStatus(msg, color) {
        statusMessage.textContent = msg;
        statusMessage.style.color = color;
        statusMessage.classList.add('show');
        
        setTimeout(() => {
            statusMessage.classList.remove('show');
        }, 3000);
    }

    // Initial load
    fetchStations();
});
