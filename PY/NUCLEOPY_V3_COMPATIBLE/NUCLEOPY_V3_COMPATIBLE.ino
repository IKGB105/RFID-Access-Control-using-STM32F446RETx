/*
 * NodeMCU RFID RC522 Web Server - V3 COMPATIBLE
 * Conecta el STM32 con RC522 a una interfaz web
 * Adaptado para ser compatible con main.c
 * 
 * Conexiones:
 * STM32 PA9 (USART1 TX) -> NodeMCU GPIO12 (D6)
 * STM32 PA10 (USART1 RX) -> NodeMCU GPIO14 (D5)
 * GND -> GND
 * 
 * Protocolo STM32 -> NodeMCU:
 * - UID:AABBCCDD        (tarjeta detectada)
 * - DATA:AABBCCDD...    (16 bytes de datos del bloque)
 * - WRITE:OK / WRITE:FAIL
 * - READ:OK / READ:FAIL
 * - AUTH:FAIL
 * - CARD:REMOVED
 * 
 * Protocolo NodeMCU -> STM32:
 * - CMD_WRITE:AABBCCDD:LEVEL:NAME (comando para escribir en tarjeta)
 * - W + 16 bytes (datos a escribir)
 * - R (comando para leer)
 */

#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <SoftwareSerial.h>

// ===== CONFIGURACIÓN WiFi =====
const char* ssid = "SM-G985F6339";
const char* password = "bobo4507";

// UART al STM32
SoftwareSerial stm32Serial(12, 14); // RX=GPIO12(D6), TX=GPIO14(D5)

// Servidor web en puerto 80
ESP8266WebServer server(80);

// Variables globales
String lastCardId = "";
String lastAccessStatus = "";
String lastAccessLevel = "";
String lastAccessName = "";
String lastBlockData = "";
unsigned long lastCardTime = 0;

// Estado de operación
enum OperationState {
    STATE_IDLE,
    STATE_READING,
    STATE_WRITING,
    STATE_WAITING_WRITE_CONFIRM
};
OperationState currentState = STATE_IDLE;

// Historial de accesos (últimos 10)
struct AccessLog {
    String cardId;
    String status;
    String level;
    String name;
    String timestamp;
};
AccessLog accessHistory[10];
int historyIndex = 0;
int historyCount = 0;

void setup() {
    Serial.begin(115200);
    Serial.println("\n\n=================================");
    Serial.println("  NodeMCU RC522 RFID Web Server");
    Serial.println("  V3 Compatible with STM32");
    Serial.println("=================================");
    
    stm32Serial.begin(9600);
    
    // Conectar a WiFi
    WiFi.begin(ssid, password);
    Serial.print("Conectando a WiFi");
    
    int attempts = 0;
    while (WiFi.status() != WL_CONNECTED && attempts < 30) {
        delay(500);
        Serial.print(".");
        attempts++;
    }
    
    Serial.println();
    
    if(WiFi.status() == WL_CONNECTED) {
        Serial.println("✓ WiFi conectado!");
        Serial.print("✓ IP Address: http://");
        Serial.println(WiFi.localIP());
    } else {
        Serial.println("✗ No se pudo conectar al WiFi");
        Serial.println("✗ El sistema continuará sin WiFi");
    }
    
    // Configurar rutas del servidor web
    server.on("/", handleRoot);
    server.on("/status", handleStatus);
    server.on("/history", handleHistory);
    server.on("/write", handleWrite);
    
    server.begin();
    Serial.println("✓ Servidor web iniciado");
    Serial.println("=================================\n");
    
    delay(1000);
}

void loop() {
    server.handleClient();
    
    // Recibir datos del STM32
    if (stm32Serial.available()) {
        String data = stm32Serial.readStringUntil('\n');
        data.trim();
        
        if(data.length() > 0) {
            Serial.println("← STM32: " + data);
            
            // Procesar según el prefijo del mensaje
            if (data.startsWith("UID:")) {
                handleUID(data);
            } 
            else if (data.startsWith("DATA:")) {
                handleBlockData(data);
            }
            else if (data.startsWith("WRITE:")) {
                handleWriteResult(data);
            }
            else if (data.startsWith("READ:")) {
                handleReadResult(data);
            }
            else if (data.startsWith("AUTH:")) {
                handleAuthResult(data);
            }
            else if (data.startsWith("CARD:REMOVED")) {
                handleCardRemoved();
            }
            else {
                Serial.println("→ Mensaje no reconocido: " + data);
            }
        }
    }
}

// ========== Manejadores del STM32 ==========

void handleUID(String data) {
    // Format: UID:AABBCCDD
    lastCardId = data.substring(4);  // Extraer AABBCCDD
    lastCardTime = millis();
    lastAccessStatus = "";
    lastBlockData = "";
    
    Serial.println("→ Tarjeta detectada: " + lastCardId);
    addToHistory(lastCardId, "DETECTED", "", "");
}

void handleBlockData(String data) {
    // Format: DATA:AABBCCDDEE...FF (16 bytes = 32 caracteres hex)
    lastBlockData = data.substring(5);  // Extraer datos después de "DATA:"
    
    Serial.println("→ Datos del bloque recibidos (" + String(lastBlockData.length() / 2) + " bytes)");
}

void handleWriteResult(String data) {
    // Format: WRITE:OK o WRITE:FAIL
    if (data.indexOf("OK") > 0) {
        lastAccessStatus = "WRITTEN";
        Serial.println("✓ Escritura exitosa");
        addToHistory(lastCardId, "WRITTEN", "", "");
    } else {
        lastAccessStatus = "WRITE_FAILED";
        Serial.println("✗ Escritura fallida");
        addToHistory(lastCardId, "WRITE_FAILED", "", "");
    }
    currentState = STATE_IDLE;
}

void handleReadResult(String data) {
    // Format: READ:OK o READ:FAIL
    if (data.indexOf("OK") > 0) {
        Serial.println("✓ Lectura exitosa");
        lastAccessStatus = "READ_OK";
    } else {
        Serial.println("✗ Lectura fallida");
        lastAccessStatus = "READ_FAILED";
    }
    currentState = STATE_IDLE;
}

void handleAuthResult(String data) {
    // Format: AUTH:FAIL
    Serial.println("✗ Autenticación fallida");
    lastAccessStatus = "AUTH_FAILED";
    addToHistory(lastCardId, "AUTH_FAILED", "", "");
    currentState = STATE_IDLE;
}

void handleCardRemoved() {
    Serial.println("→ Tarjeta removida");
    lastCardId = "";
    lastAccessStatus = "";
    lastBlockData = "";
    currentState = STATE_IDLE;
}

void addToHistory(String cardId, String status, String level, String name) {
    if (cardId.length() == 0) return;
    
    accessHistory[historyIndex].cardId = cardId;
    accessHistory[historyIndex].status = status;
    accessHistory[historyIndex].level = level;
    accessHistory[historyIndex].name = name;
    
    unsigned long seconds = millis() / 1000;
    accessHistory[historyIndex].timestamp = String(seconds) + "s";
    
    historyIndex = (historyIndex + 1) % 10;
    if(historyCount < 10) historyCount++;
}

// ========== Manejadores Web ==========

void handleRoot() {
    String html = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
  <meta charset="UTF-8">
  <meta name="viewport" content="width=device-width, initial-scale=1.0">
  <title>Sistema RFID RC522</title>
  <style>
    * { margin: 0; padding: 0; box-sizing: border-box; }
    body {
      font-family: 'Segoe UI', Tahoma, Geneva, Verdana, sans-serif;
      background: linear-gradient(135deg, #667eea 0%, #764ba2 100%);
      min-height: 100vh;
      padding: 20px;
    }
    .container {
      max-width: 900px;
      margin: 0 auto;
    }
    .card {
      background: white;
      border-radius: 15px;
      padding: 30px;
      margin-bottom: 20px;
      box-shadow: 0 10px 30px rgba(0,0,0,0.2);
    }
    h1, h2 {
      color: #667eea;
      margin-bottom: 15px;
    }
    .subtitle {
      color: #666;
      margin-bottom: 20px;
    }
    .status-box {
      background: #f8f9fa;
      border-radius: 10px;
      padding: 20px;
      margin-bottom: 15px;
      border-left: 5px solid #667eea;
    }
    .status-label {
      font-weight: bold;
      color: #333;
      margin-bottom: 5px;
    }
    .status-value {
      font-size: 1.2em;
      color: #667eea;
      word-break: break-all;
    }
    .detected {
      border-left-color: #17a2b8;
    }
    .detected .status-value {
      color: #17a2b8;
    }
    .written {
      border-left-color: #28a745;
    }
    .written .status-value {
      color: #28a745;
    }
    .failed {
      border-left-color: #dc3545;
    }
    .failed .status-value {
      color: #dc3545;
    }
    .btn {
      background: #667eea;
      color: white;
      border: none;
      padding: 12px 30px;
      border-radius: 8px;
      font-size: 1em;
      cursor: pointer;
      margin: 5px;
      transition: all 0.3s;
    }
    .btn:hover {
      background: #5568d3;
      transform: translateY(-2px);
      box-shadow: 0 5px 15px rgba(102, 126, 234, 0.4);
    }
    .btn:disabled {
      background: #999;
      cursor: not-allowed;
      transform: none;
    }
    .input-group {
      margin-bottom: 15px;
    }
    .input-group label {
      display: block;
      margin-bottom: 5px;
      font-weight: bold;
      color: #333;
    }
    .input-group input, .input-group select {
      width: 100%;
      padding: 10px;
      border: 2px solid #ddd;
      border-radius: 8px;
      font-size: 1em;
    }
    .input-group input:focus, .input-group select:focus {
      outline: none;
      border-color: #667eea;
    }
    #message {
      padding: 15px;
      border-radius: 8px;
      margin-top: 15px;
      display: none;
    }
    .success {
      background: #d4edda;
      color: #155724;
      border: 1px solid #c3e6cb;
    }
    .error {
      background: #f8d7da;
      color: #721c24;
      border: 1px solid #f5c6cb;
    }
    .history-table {
      width: 100%;
      border-collapse: collapse;
      margin-top: 15px;
    }
    .history-table th {
      background: #667eea;
      color: white;
      padding: 10px;
      text-align: left;
    }
    .history-table td {
      padding: 10px;
      border-bottom: 1px solid #ddd;
    }
    .history-table tr:hover {
      background: #f8f9fa;
    }
    .data-box {
      background: #f0f0f0;
      padding: 15px;
      border-radius: 8px;
      font-family: monospace;
      font-size: 0.9em;
      word-break: break-all;
    }
  </style>
</head>
<body>
  <div class="container">
    <div class="card">
      <h1>🔐 Sistema RFID RC522</h1>
      <p class="subtitle">Control de acceso con STM32F446 y RC522</p>
      
      <div id="statusContainer">
        <div class="status-box">
          <div class="status-label">Estado del sistema</div>
          <div class="status-value">⏳ Esperando tarjeta...</div>
        </div>
      </div>
      
      <button class="btn" onclick="refreshStatus()">🔄 Actualizar Estado</button>
    </div>
    
    <div class="card">
      <h2>✍️ Registrar Nueva Tarjeta</h2>
      <p class="subtitle">Configura nivel de acceso y nombre de usuario</p>
      
      <div class="input-group">
        <label>Card ID (8 caracteres hex)</label>
        <input type="text" id="cardId" maxlength="8" placeholder="AABBCCDD" 
               style="text-transform: uppercase;" readonly>
        <small style="color: #666;">Se completará automáticamente al detectar una tarjeta</small>
      </div>
      <div class="input-group">
        <label>Nivel de acceso</label>
        <select id="level">
          <option value="ADMIN">ADMIN</option>
          <option value="PROFESOR">PROFESOR</option>
          <option value="ESTUDIANTE" selected>ESTUDIANTE</option>
          <option value="VISITANTE">VISITANTE</option>
        </select>
      </div>
      <button class="btn" onclick="writeCard()">💾 Escribir en Tarjeta</button>
      <div id="message"></div>
    </div>
    
    <div class="card">
      <h2>📊 Historial de Operaciones</h2>
      <div id="historyContainer">
        <p style="color: #666;">Cargando historial...</p>
      </div>
    </div>
  </div>

  <script>
    setInterval(refreshStatus, 2000);
    setInterval(refreshHistory, 3000);
    
    function refreshStatus() {
      fetch('/status')
        .then(response => response.json())
        .then(data => {
          let html = '';
          
          if (data.lastCard) {
            document.getElementById('cardId').value = data.lastCard;
            
            let statusClass = '';
            let statusIcon = '';
            
            if(data.status === 'DETECTED') {
              statusClass = 'detected';
              statusIcon = '📇';
            } else if(data.status === 'WRITTEN') {
              statusClass = 'written';
              statusIcon = '✓';
            } else if(data.status.includes('FAILED')) {
              statusClass = 'failed';
              statusIcon = '✗';
            }
            
            html = `
              <div class="status-box ${statusClass}">
                <div class="status-label">Última tarjeta detectada</div>
                <div class="status-value">${statusIcon} ${data.lastCard}</div>
              </div>
            `;
            
            if (data.status && data.status !== 'DETECTED') {
              html += `
                <div class="status-box">
                  <div class="status-label">Estado</div>
                  <div class="status-value">${data.status}</div>
                </div>
              `;
            }
            
            if (data.blockData) {
              // Convert HEX to ASCII text
              let hexStr = data.blockData;
              let textContent = '';
              for (let i = 0; i < hexStr.length; i += 2) {
                let hex = hexStr.substr(i, 2);
                let charCode = parseInt(hex, 16);
                if (charCode >= 32 && charCode <= 126) {
                  textContent += String.fromCharCode(charCode);
                } else {
                  textContent += '.';
                }
              }
              
              html += `
                <div class="status-box">
                  <div class="status-label">Datos del bloque (Hex)</div>
                  <div class="data-box">${data.blockData}</div>
                </div>
                <div class="status-box">
                  <div class="status-label">Contenido de la tarjeta (Texto)</div>
                  <div class="data-box" style="font-family: Arial, sans-serif;">${textContent}</div>
                </div>
              `;
            }
            
            html += `
              <div class="status-box">
                <div class="status-label">Tiempo transcurrido</div>
                <div class="status-value">${data.timeSince}</div>
              </div>
            `;
          } else {
            html = `
              <div class="status-box">
                <div class="status-label">Estado del sistema</div>
                <div class="status-value">⏳ Esperando tarjeta...</div>
              </div>
            `;
          }
          
          document.getElementById('statusContainer').innerHTML = html;
        });
    }
    
    function refreshHistory() {
      fetch('/history')
        .then(response => response.json())
        .then(data => {
          if(data.history && data.history.length > 0) {
            let html = '<table class="history-table"><tr><th>Tarjeta</th><th>Estado</th><th>Nivel</th><th>Nombre</th><th>Tiempo</th></tr>';
            
            data.history.forEach(item => {
              let statusIcon = item.status === 'WRITTEN' ? '✓' : 
                              item.status.includes('FAILED') ? '✗' : '📝';
              html += `<tr>
                <td>${item.cardId}</td>
                <td>${statusIcon} ${item.status}</td>
                <td>${item.level || '-'}</td>
                <td>${item.name || '-'}</td>
                <td>${item.timestamp}</td>
              </tr>`;
            });
            
            html += '</table>';
            document.getElementById('historyContainer').innerHTML = html;
          }
        });
    }
    
    function writeCard() {
      const cardId = document.getElementById('cardId').value.toUpperCase();
      const level = document.getElementById('level').value;
      
      if (cardId.length !== 8) {
        showMessage('Primero detecta una tarjeta (Card ID debe tener 8 caracteres)', 'error');
        return;
      }
      
      fetch(`/write?cardId=${cardId}&level=${encodeURIComponent(level)}`)
        .then(response => response.text())
        .then(data => {
          showMessage(data, 'success');
        })
        .catch(err => showMessage('Error: ' + err, 'error'));
    }
    
    function showMessage(msg, type) {
      const msgBox = document.getElementById('message');
      msgBox.textContent = msg;
      msgBox.className = type;
      msgBox.style.display = 'block';
      setTimeout(() => msgBox.style.display = 'none', 5000);
    }
    
    refreshStatus();
    refreshHistory();
  </script>
</body>
</html>
)rawliteral";
  
    server.send(200, "text/html", html);
}

void handleStatus() {
    String json = "{";
    
    if (lastCardId.length() > 0) {
        unsigned long timeSince = (millis() - lastCardTime) / 1000;
        
        json += "\"lastCard\":\"" + lastCardId + "\",";
        json += "\"status\":\"" + lastAccessStatus + "\",";
        json += "\"timeSince\":\"" + String(timeSince) + " segundos\"";
        
        if (lastBlockData.length() > 0) {
            json += ",\"blockData\":\"" + lastBlockData + "\"";
        }
    }
    
    json += "}";
    
    server.send(200, "application/json", json);
}

void handleHistory() {
    String json = "{\"history\":[";
    
    for(int i = 0; i < historyCount; i++) {
        int idx = (historyIndex - historyCount + i + 10) % 10;
        
        if(i > 0) json += ",";
        
        json += "{";
        json += "\"cardId\":\"" + accessHistory[idx].cardId + "\",";
        json += "\"status\":\"" + accessHistory[idx].status + "\",";
        json += "\"level\":\"" + accessHistory[idx].level + "\",";
        json += "\"name\":\"" + accessHistory[idx].name + "\",";
        json += "\"timestamp\":\"" + accessHistory[idx].timestamp + "\"";
        json += "}";
    }
    
    json += "]}";
    
    server.send(200, "application/json", json);
}

void handleWrite() {
    if (server.hasArg("cardId") && server.hasArg("level")) {
        String cardId = server.arg("cardId");
        String level = server.arg("level");
        
        cardId.toUpperCase();
        
        // Convert level to single character code
        char levelCode;
        if (level == "ADMIN") {
            levelCode = '0';
        } else if (level == "PROFESOR") {
            levelCode = '1';
        } else if (level == "ESTUDIANTE" || level == "VISITANTE") {
            levelCode = '2';
        } else {
            levelCode = '2';  // Default to VISITOR/ESTUDIANTE
        }
        
        // Send only single character: '0'=ADMIN, '1'=STUDENT, '2'=VISITOR
        stm32Serial.write(levelCode);
        
        Serial.println("→ STM32: '" + String(levelCode) + "' (level: " + level + ")");
        
        currentState = STATE_WRITING;
        server.send(200, "text/plain", "✓ Comando enviado: escribiendo nivel " + level + " en tarjeta " + cardId + ". Por favor, acerca la tarjeta al lector.");
    } else {
        server.send(400, "text/plain", "✗ Faltan parámetros");
    }
}
