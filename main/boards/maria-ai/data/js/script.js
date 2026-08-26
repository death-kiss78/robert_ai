document.addEventListener("DOMContentLoaded", () => {

    const scanBtn = document.getElementById("scanBtn");
    const networksDiv = document.getElementById("networks");
    const connectSection = document.getElementById("connectSection");
    const selectedSSID = document.getElementById("selectedSSID");
    const wifiPass = document.getElementById("wifiPass");
    const connectBtn = document.getElementById("connectBtn");
    const statusText = document.getElementById("statusText");

    const clientsTable = document.getElementById("clientsTable");

    let chosenSSID = "";

    // -------------------------
    // Scanare rețele
    // -------------------------
    scanBtn.addEventListener("click", () => {
        networksDiv.innerHTML = "Scanez...";
        fetch("/scan")
            .then(r => r.json())
            .then(list => {
                networksDiv.innerHTML = "";
                list.forEach(ssid => {
                    const item = document.createElement("div");
                    item.className = "network-item";
                    item.textContent = ssid;

                    item.addEventListener("click", () => {
                        chosenSSID = ssid;
                        selectedSSID.textContent = "Rețea selectată: " + ssid;
                        connectSection.style.display = "block";
                    });

                    networksDiv.appendChild(item);
                });
            });
    });

    // -------------------------
    // Conectare la rețea
    // -------------------------
    connectBtn.addEventListener("click", () => {
        const pass = wifiPass.value;

        fetch(`/connect?ssid=${encodeURIComponent(chosenSSID)}&pass=${encodeURIComponent(pass)}`)
            .then(() => {
                statusText.textContent = "Conectare în curs...";
                setTimeout(updateStatus, 2000);
            });
    });

    // -------------------------
    // Status conexiune
    // -------------------------
    function updateStatus() {
        fetch("/status")
            .then(r => r.json())
            .then(data => {
                if (data.connected) {
                    statusText.textContent = "Conectat! IP: " + data.ip;
                } else {
                    statusText.textContent = "Neconectat.";
                }
            });
    }

    // actualizare periodică status
    setInterval(updateStatus, 5000);
    updateStatus();

    // -------------------------
    // Lista clienților conectați la AP
    // -------------------------
    window.loadClients = function () {
        fetch("/clients")
            .then(r => r.json())
            .then(data => {
                clientsTable.innerHTML = `
                    <tr>
                        <th>MAC</th>
                        <th>IP</th>
                    </tr>
                `;

                data.clients.forEach(c => {
                    const row = document.createElement("tr");
                    row.innerHTML = `
                        <td>${c.mac}</td>
                        <td>${c.ip}</td>
                    `;
                    clientsTable.appendChild(row);
                });
            });
    };

});

function resetAndReload() {
    fetch('/sd/reset')
      .then(r => r.text())
      .then(t => {
          alert(t);
          setTimeout(() => {
              location.reload();
          }, 1500);
      });
}

document.addEventListener('keydown', function(e) {
    if (e.code === 'Space') {
        e.preventDefault();
        resetAndReload();
    }
});

