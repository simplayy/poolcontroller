// === ARDUINO POMPE - Controllo pH per Piscina ===
// ESP32 dedicato al controllo delle pompe pH+ e pH-

#include <WiFi.h>
#include <WebServer.h>

// === CONFIGURAZIONE WIFI ===
const char* ssid = "iPhone di Nico";
const char* password = "tipodeicodici";

// === IP CONFIGURATION ===
IPAddress pompe_ip(172, 20, 10, 6);    // IP questo ESP32 (pompe)
const char* sensori_ip = "172.20.10.5"; // IP ESP32 sensori

// === PIN DEFINITIONS ===
#define PUMP_PH_PLUS_PIN 25    // GPIO25 per pompa pH+
#define PUMP_PH_MINUS_PIN 27   // GPIO27 per pompa pH-
#define LED_PIN 2              // LED interno

// === SAFETY SETTINGS ===
#define MAX_PUMP_DURATION 30000        // Max 30 secondi (per correzioni intelligenti)
#define SAFETY_TIMEOUT 35000           // Timeout sicurezza 35 secondi

// Variabili di stato
WebServer server(80);
bool pumpPhPlusActive = false;
bool pumpPhMinusActive = false;
unsigned long pumpStartTime = 0;
unsigned long pumpDuration = 0;      // Durata specifica del comando
String lastAction = "Nessuna";
String lastError = "";

void setup() {
  Serial.begin(115200);
  Serial.println("=== ARDUINO POMPE - pH Controller ===");
  
  // Setup pin pompe
  pinMode(PUMP_PH_PLUS_PIN, OUTPUT);
  pinMode(PUMP_PH_MINUS_PIN, OUTPUT);
  pinMode(LED_PIN, OUTPUT);
  
  // Assicurati che le pompe siano spente
  digitalWrite(PUMP_PH_PLUS_PIN, LOW);
  digitalWrite(PUMP_PH_MINUS_PIN, LOW);
  
  // Setup WiFi
  setupWiFi();
  
  // Setup Web Server
  setupWebServer();
  
  Serial.println("Sistema pompe pronto!");
  Serial.print("IP Pompe: ");
  Serial.println(WiFi.localIP());
  Serial.print("IP Sensori: ");
  Serial.println(sensori_ip);
  
  // Test iniziale pompe
  testPumps();
}

void setupWiFi() {
  Serial.println("Configurazione WiFi...");
  
  WiFi.setSleep(false);
  
  IPAddress gateway(172, 20, 10, 1);
  IPAddress subnet(255, 255, 255, 240);
  IPAddress dns1(172, 20, 10, 1);
  IPAddress dns2(8, 8, 8, 8);
  
  if (!WiFi.config(pompe_ip, gateway, subnet, dns1, dns2)) {
    Serial.println("Errore configurazione IP statico!");
  }
  
  WiFi.begin(ssid, password);
  
  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 20) {
    delay(500);
    Serial.print(".");
    attempts++;
  }
  
  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\nWiFi connesso!");
    Serial.print("IP: ");
    Serial.println(WiFi.localIP());
  } else {
    Serial.println("\nErrore connessione WiFi!");
  }
}

void setupWebServer() {
  // Pagina status pompe
  server.on("/", HTTP_GET, [](){
    String html = generateStatusPage();
    server.send(200, "text/html", html);
  });
  
  // API principale per controllo pompe
  server.on("/api/control", HTTP_POST, [](){
    String response = handlePumpControl();
    server.send(200, "text/plain", response);
  });
  
  // API status
  server.on("/api/status", HTTP_GET, [](){
    String json = "{";
    json += "\"ph_plus_active\":" + String(pumpPhPlusActive ? "true" : "false") + ",";
    json += "\"ph_minus_active\":" + String(pumpPhMinusActive ? "true" : "false") + ",";
    json += "\"last_action\":\"" + lastAction + "\",";
    json += "\"last_error\":\"" + lastError + "\",";
    json += "\"uptime\":" + String(millis()) + ",";
    json += "\"wifi_rssi\":" + String(WiFi.RSSI());
    json += "}";
    
    server.sendHeader("Access-Control-Allow-Origin", "*");
    server.send(200, "application/json", json);
  });
  
  server.begin();
  Serial.println("Web server pompe avviato");
}

String generateStatusPage() {
  String html = "<!DOCTYPE html><html><head>";
  html += "<title>🔧 Controllo Pompe pH</title>";
  html += "<meta charset='UTF-8'>";
  html += "<meta name='viewport' content='width=device-width, initial-scale=1.0'>";
  html += "<style>";
  html += "body { font-family: Arial, sans-serif; margin: 0; background: linear-gradient(135deg, #ff9800 0%, #f57c00 100%); min-height: 100vh; }";
  html += ".container { max-width: 600px; margin: 0 auto; padding: 20px; }";
  html += ".card { background: white; border-radius: 15px; padding: 20px; margin: 15px 0; box-shadow: 0 4px 15px rgba(0,0,0,0.1); }";
  html += ".status-value { font-size: 24px; font-weight: bold; margin: 10px 0; }";
  html += ".active { color: #4CAF50; }";
  html += ".inactive { color: #666; }";
  html += ".error { color: #f44336; }";
  html += ".info { font-size: 14px; color: #666; margin: 5px 0; }";
  html += "</style>";
  html += "<script>";
  html += "setInterval(() => location.reload(), 5000);";  // Auto refresh ogni 5 secondi
  html += "</script>";
  html += "</head><body>";
  
  html += "<div class='container'>";
  html += "<h1 style='color: white; text-align: center;'>🔧 Controllo Pompe pH</h1>";
  
  // Status pompe
  html += "<div class='card'>";
  html += "<h3>📊 Status Pompe</h3>";
  
  html += "<div class='status-value " + String(pumpPhPlusActive ? "active" : "inactive") + "'>";
  html += "Pompa pH+: " + String(pumpPhPlusActive ? "🟢 ATTIVA" : "⚫ SPENTA");
  html += "</div>";
  
  html += "<div class='status-value " + String(pumpPhMinusActive ? "active" : "inactive") + "'>";
  html += "Pompa pH-: " + String(pumpPhMinusActive ? "🟢 ATTIVA" : "⚫ SPENTA");
  html += "</div>";
  
  html += "<div class='info'>Ultima azione: " + lastAction + "</div>";
  if (lastError != "") {
    html += "<div class='info error'>Ultimo errore: " + lastError + "</div>";
  }
  html += "</div>";
  
  // Info sistema
  html += "<div class='card'>";
  html += "<h3>📶 Sistema</h3>";
  html += "<div class='info'>IP Pompe: " + WiFi.localIP().toString() + "</div>";
  html += "<div class='info'>IP Sensori: " + String(sensori_ip) + "</div>";
  html += "<div class='info'>WiFi: " + String(WiFi.RSSI()) + " dBm</div>";
  html += "<div class='info'>Uptime: " + String(millis()/1000) + " secondi</div>";
  html += "</div>";
  
  html += "</div></body></html>";
  
  return html;
}

String handlePumpControl() {
  if (!server.hasArg("pump")) {
    lastError = "Parametro pump mancante";
    return "Errore: parametro pump mancante";
  }
  
  String pump = server.arg("pump");
  int duration = server.hasArg("duration") ? server.arg("duration").toInt() : 3000;
  
  // Validazione sicurezza
  if (duration > MAX_PUMP_DURATION) {
    duration = MAX_PUMP_DURATION;
  }
  
  if (duration < 500 && pump != "status") {
    lastError = "Durata troppo breve";
    return "Errore: durata minima 500ms";
  }
  
  // Gestione comandi
  if (pump == "status") {
    return getDetailedStatus();
  } else if (pump == "ph_plus") {
    return activatePumpPhPlus(duration);
  } else if (pump == "ph_minus") {
    return activatePumpPhMinus(duration);
  } else {
    lastError = "Comando non valido: " + pump;
    return "Errore: comando non valido";
  }
}

String activatePumpPhPlus(int duration) {
  if (pumpPhPlusActive || pumpPhMinusActive) {
    lastError = "Pompa già attiva";
    return "Errore: pompa già in funzione";
  }
  
  Serial.println("Attivazione pompa pH+ per " + String(duration) + "ms");
  
  pumpPhPlusActive = true;
  pumpStartTime = millis();
  pumpDuration = duration; // Salva la durata
  lastAction = "pH+ per " + String(duration) + "ms";
  lastError = "";
  
  digitalWrite(PUMP_PH_PLUS_PIN, HIGH);
  
  return "Pompa pH+ attivata per " + String(duration) + "ms";
}

String activatePumpPhMinus(int duration) {
  if (pumpPhPlusActive || pumpPhMinusActive) {
    lastError = "Pompa già attiva";
    return "Errore: pompa già in funzione";
  }
  
  Serial.println("Attivazione pompa pH- per " + String(duration) + "ms");
  
  pumpPhMinusActive = true;
  pumpStartTime = millis();
  pumpDuration = duration; // Salva la durata
  lastAction = "pH- per " + String(duration) + "ms";
  lastError = "";
  
  digitalWrite(PUMP_PH_MINUS_PIN, HIGH);
  
  return "Pompa pH- attivata per " + String(duration) + "ms";
}

String getDetailedStatus() {
  String status = "=== STATUS POMPE ===\n";
  status += "pH+: " + String(pumpPhPlusActive ? "ATTIVA" : "SPENTA") + "\n";
  status += "pH-: " + String(pumpPhMinusActive ? "ATTIVA" : "SPENTA") + "\n";
  status += "Ultima azione: " + lastAction + "\n";
  if (lastError != "") {
    status += "Ultimo errore: " + lastError + "\n";
  }
  status += "Uptime: " + String(millis()/1000) + "s\n";
  status += "WiFi: " + String(WiFi.RSSI()) + " dBm";
  
  return status;
}

void checkPumpSafety() {
  unsigned long currentTime = millis();
  
  // Controllo durata specifica e timeout pompa pH+
  if (pumpPhPlusActive) {
    // Prima controlla se ha raggiunto la durata specifica
    if (currentTime - pumpStartTime >= pumpDuration) {
      digitalWrite(PUMP_PH_PLUS_PIN, LOW);
      pumpPhPlusActive = false;
      Serial.println("Pompa pH+ spenta (durata completata: " + String(pumpDuration) + "ms)");
      lastAction = "pH+ completata (" + String(pumpDuration) + "ms)";
    }
    // Controllo timeout di sicurezza come backup
    else if (currentTime - pumpStartTime >= SAFETY_TIMEOUT) {
      digitalWrite(PUMP_PH_PLUS_PIN, LOW);
      pumpPhPlusActive = false;
      Serial.println("Pompa pH+ spenta (timeout sicurezza)");
      
      lastError = "Timeout sicurezza pH+";
    }
  }
  
  // Controllo durata specifica e timeout pompa pH-
  if (pumpPhMinusActive) {
    // Prima controlla se ha raggiunto la durata specifica
    if (currentTime - pumpStartTime >= pumpDuration) {
      digitalWrite(PUMP_PH_MINUS_PIN, LOW);
      pumpPhMinusActive = false;
      Serial.println("Pompa pH- spenta (durata completata: " + String(pumpDuration) + "ms)");
      lastAction = "pH- completata (" + String(pumpDuration) + "ms)";
    }
    // Controllo timeout di sicurezza come backup
    else if (currentTime - pumpStartTime >= SAFETY_TIMEOUT) {
      digitalWrite(PUMP_PH_MINUS_PIN, LOW);
      pumpPhMinusActive = false;
      Serial.println("Pompa pH- spenta (timeout sicurezza)");
      
      lastError = "Timeout sicurezza pH-";
    }
  }
}

void testPumps() {
  Serial.println("Test rapido pompe...");
  
  // Test pompa pH+
  Serial.println("Test pH+ (1 secondo)");
  digitalWrite(PUMP_PH_PLUS_PIN, HIGH);
  delay(1000);
  digitalWrite(PUMP_PH_PLUS_PIN, LOW);
  
  delay(2000);
  
  // Test pompa pH-
  Serial.println("Test pH- (1 secondo)");
  digitalWrite(PUMP_PH_MINUS_PIN, HIGH);
  delay(1000);
  digitalWrite(PUMP_PH_MINUS_PIN, LOW);
  
  Serial.println("Test completato!");
  lastAction = "Test iniziale completato";
}

void loop() {
  unsigned long currentTime = millis();
  
  // LED di stato
  digitalWrite(LED_PIN, (pumpPhPlusActive || pumpPhMinusActive) ? HIGH : LOW);
  
  // Controllo sicurezza pompe
  checkPumpSafety();
  
  // Gestione web server
  if (WiFi.status() == WL_CONNECTED) {
    server.handleClient();
  }
  
  // Output seriale periodico
  static unsigned long lastSerialOutput = 0;
  if (currentTime - lastSerialOutput >= 10000) {  // Ogni 10 secondi
    Serial.print("Status: ");
    Serial.print("pH+ " + String(pumpPhPlusActive ? "ON" : "OFF"));
    Serial.print(" | pH- " + String(pumpPhMinusActive ? "ON" : "OFF"));
    Serial.print(" | WiFi: " + String(WiFi.RSSI()) + "dBm");
    Serial.println(" | Last: " + lastAction);
    
    lastSerialOutput = currentTime;
  }
  
  delay(50);  // Piccolo delay per responsività
} 