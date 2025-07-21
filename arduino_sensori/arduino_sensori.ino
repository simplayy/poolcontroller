// === ARDUINO SENSORI - Pool Monitor con Controllo Pompe ===
// ESP32 con sensori + interfaccia per controllo pompe pH

#include <OneWire.h>
#include <DallasTemperature.h>
#include <WiFi.h>
#include <WebServer.h>
#include <HTTPClient.h>
#include <EEPROM.h>
#include <math.h>

// === CONFIGURAZIONE WIFI ===
const char* ssid = "iPhone di Nico";
const char* password = "tipodeicodici";
//const char* ssid = "FRITZ!Box 7590 EE";
//const char* password = "73904964901912360779";


// === CONFIGURAZIONE TELEGRAM ===
// Per ottenere questi valori:
// 1. Cerca @BotFather su Telegram
// 2. Scrivi /newbot e segui le istruzioni
// 3. Copia il token qui sotto
// 4. Scrivi al tuo bot e poi vai su https://api.telegram.org/bot<TOKEN>/getUpdates
// 5. Trova il "chat":{"id": nella risposta e copialo qui sotto
const char* telegramToken = "8160576144:AAEl9xJTxfNnfzDio5O8sBBIdb1zOoF1hZk";  // Sostituisci con il tuo bot token
const char* telegramChatID = "161074294";   // Sostituisci con il tuo chat ID

// === IP CONFIGURATION ===

IPAddress sensori_ip(172, 20, 10, 5);  // IP questo ESP32 (sensori)
const char* pompe_ip = "172.20.10.6";   // IP ESP32 pompe

// === PIN DEFINITIONS ===
#define ONE_WIRE_BUS 33
#define TDS_PIN 32
#define LED_PIN 2

// === SENSOR CONSTANTS ===
#define TDS_MIN_VOLTAGE 0.01
#define TDS_MAX_VOLTAGE 2.5
#define TDS_MAX_PPM 1000
#define TEMPERATURE_COMPENSATION 25.0

// === PH CONTROL SETTINGS ===
#define PH_TARGET_MIN 7.0
#define PH_TARGET_MAX 7.4
#define PH_TOLERANCE 0.1

// === TELEGRAM SETTINGS ===
#define NOTIFICATION_INTERVAL 300000  // 5 minuti tra notifiche duplicate
#define SENSOR_TIMEOUT 30000          // 30 secondi prima di considerare sensore scollegato

// === AI pH MODEL ===
#define MAX_TRAINING_DATA 20
#define EEPROM_SIZE 512

struct TrainingData {
  float temperature;
  float tds;
  float ph;
  bool valid;
};

struct PHModel {
  float a;  // Coefficiente temperatura
  float b;  // Coefficiente TDS  
  float c;  // Intercetta
  int dataCount;
  bool trained;
  float poolSurface;  // Superficie piscina in m2
};

// Setup sensori
OneWire oneWire(ONE_WIRE_BUS);
DallasTemperature sensors(&oneWire);
WebServer server(80);
HTTPClient http;

// Variabili AI pH
TrainingData trainingData[MAX_TRAINING_DATA];
PHModel phModel = {0, 0, 7.0, 0, false, 25.0};

// Variabili sensori
float temperature = 0;
float tds = 0;
bool tdsValid = false;
float estimatedPH = 7.0;
unsigned long lastSensorRead = 0;

// === VARIABILI CORREZIONE INTELLIGENTE ===
float poolSurfaceM2 = 25.0;        // Superficie piscina in m2 (default 5x5m)
float poolDepthM = 1.5;            // Profondità media in metri
int lastCalculatedDuration = 0;    // Ultima durata calcolata per debug

// Variabili notifiche Telegram
bool previousTdsValid = true;  // Stato precedente del sensore TDS
unsigned long lastNotificationTime = 0;
bool notificationEnabled = true;

// Variabili media mobile TDS
float tdsReadings[5];
int readingIndex = 0;

// === VARIABILI PER STIMA MEDIANA ===
float estimatedPH_median = 7.0;

void setup() {
  Serial.begin(115200);
  Serial.println("=== ARDUINO SENSORI - Pool Monitor ===");
  
  // Inizializza EEPROM
  EEPROM.begin(EEPROM_SIZE);
  
  // Setup pin
  pinMode(LED_PIN, OUTPUT);
  pinMode(TDS_PIN, INPUT);
  pinMode(ONE_WIRE_BUS, INPUT_PULLUP);
  
  // Setup sensori
  sensors.begin();
  analogReadResolution(12);
  analogSetPinAttenuation(TDS_PIN, ADC_11db);
  
  // Inizializza array TDS
  for(int i = 0; i < 5; i++) {
    tdsReadings[i] = 0;
  }
  
  // Carica modello AI
  loadPHModel();
  
  // Setup WiFi
  setupWiFi();
  
  // Setup Web Server
  setupWebServer();
  
  // Test Telegram (opzionale)
  if (strlen(telegramToken) > 20 && strlen(telegramChatID) > 5) {
    sendTelegramMessage("Pool Monitor Online! Sistema di monitoraggio attivo.");
    Serial.println("📱 Telegram configurato e testato");
    
    // Controlla subito se il sensore è scollegato all'avvio
    delay(2000); // Aspetta un po' per la stabilizzazione
    readSensors(); // Prima lettura
    
    if (!tdsValid && notificationEnabled) {
      Serial.println("🚨 Sensore TDS già scollegato all'avvio - invio allarme");
      String alertMessage = "<b>ALLARME PISCINA!</b>\n\n";
      alertMessage += "<b>LIVELLO ACQUA BASSO</b>\n";
      alertMessage += "Sistema avviato con sensore TDS scollegato.\n";
      alertMessage += "Il livello dell'acqua e sotto la soglia minima.\n\n";
      alertMessage += "<b>Stato Sistema:</b>\n";
      alertMessage += "Temperatura: " + String(temperature, 1) + "C\n";
      alertMessage += "TDS: Sensore scollegato\n";
      alertMessage += "pH stimato: " + String(estimatedPH, 2);
      if (phModel.trained) {
        alertMessage += " (AI:" + String(phModel.dataCount) + " campioni)\n";
      } else {
        alertMessage += " (formula base)\n";
      }
      alertMessage += "\nControlla immediatamente il livello dell'acqua!";
      
             sendTelegramMessage(alertMessage);
       // NON aggiorno lastNotificationTime qui per permettere allarmi immediati
     }
  } else {
    Serial.println("⚠️ Telegram NON configurato - inserire token e chat ID");
  }
  
  Serial.println("Sistema pronto!");
  Serial.print("IP Sensori: ");
  Serial.println(WiFi.localIP());
  Serial.print("IP Pompe: ");
  Serial.println(pompe_ip);
}

void setupWiFi() {
  Serial.println("Configurazione WiFi...");
  
  WiFi.setSleep(false);
  IPAddress gateway(172, 20, 10, 1);
  IPAddress subnet(255, 255, 255, 240);
  IPAddress dns1(172, 20, 10, 1);
  IPAddress dns2(8, 8, 8, 8);
  
  if (!WiFi.config(sensori_ip, gateway, subnet, dns1, dns2)) {
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
  // Pagina principale
  server.on("/", HTTP_GET, [](){
    String html = generateWebPage();
    server.send(200, "text/html", html);
  });
  
  // Pagina gestione training
  server.on("/training", HTTP_GET, [](){
    String html = generateTrainingPage();
    server.send(200, "text/html", html);
  });
  
  // API sensori (per ESP32 pompe)
  server.on("/api/sensors", HTTP_GET, [](){
    String json = "{";
    json += "\"temperature\":" + String(temperature, 2) + ",";
    json += "\"tds\":" + (tdsValid ? String(tds, 1) : "null") + ",";
    json += "\"ph_estimated\":" + String(estimatedPH, 2) + ",";
    json += "\"ph_target_min\":" + String(PH_TARGET_MIN, 1) + ",";
    json += "\"ph_target_max\":" + String(PH_TARGET_MAX, 1) + ",";
    json += "\"tds_connected\":" + String(tdsValid ? "true" : "false") + ",";
    json += "\"ph_model_trained\":" + String(phModel.trained ? "true" : "false") + ",";
    json += "\"training_samples\":" + String(phModel.dataCount) + ",";
    json += "\"pool_surface\":" + String(phModel.poolSurface, 1);
    json += "}";
    
    server.sendHeader("Access-Control-Allow-Origin", "*");
    server.send(200, "application/json", json);
  });
  
  // === NUOVI ENDPOINT PER GESTIONE TRAINING ===
  
  // Visualizza tutti i dati di training
  server.on("/api/training/list", HTTP_GET, [](){
    String json = "{\"samples\":[";
    bool first = true;
    
    for(int i = 0; i < phModel.dataCount; i++) {
      if (trainingData[i].valid) {
        if (!first) json += ",";
        json += "{";
        json += "\"id\":" + String(i) + ",";
        json += "\"temperature\":" + String(trainingData[i].temperature, 2) + ",";
        json += "\"tds\":" + String(trainingData[i].tds, 1) + ",";
        json += "\"ph\":" + String(trainingData[i].ph, 2);
        json += "}";
        first = false;
      }
    }
    
    json += "],";
    json += "\"count\":" + String(phModel.dataCount) + ",";
    json += "\"model_trained\":" + String(phModel.trained ? "true" : "false") + ",";
    json += "\"formula\":{";
    json += "\"a\":" + String(phModel.a, 6) + ",";
    json += "\"b\":" + String(phModel.b, 6) + ",";
    json += "\"c\":" + String(phModel.c, 3);
    json += "}}";
    
    server.sendHeader("Access-Control-Allow-Origin", "*");
    server.send(200, "application/json", json);
  });
  
  // Aggiungi dato di training
  server.on("/api/training/add", HTTP_POST, [](){
    if (server.hasArg("ph") && server.hasArg("temp") && server.hasArg("tds")) {
      float ph = server.arg("ph").toFloat();
      float temp = server.arg("temp").toFloat();
      float tds = server.arg("tds").toFloat();
      
      // Nessuna validazione dati
      addTrainingData(temp, tds, ph);
      server.send(200, "application/json", "{\"success\":true,\"message\":\"Dati aggiunti con successo\"}");
    } else {
      server.send(400, "application/json", "{\"success\":false,\"message\":\"Parametri mancanti (ph, temp, tds)\"}");
    }
  });
  
  // Cancella singolo campione
  server.on("/api/training/delete", HTTP_POST, [](){
    if (server.hasArg("id")) {
      int id = server.arg("id").toInt();
      if (deleteTrainingData(id)) {
        server.send(200, "application/json", "{\"success\":true,\"message\":\"Campione eliminato\"}");
      } else {
        server.send(400, "application/json", "{\"success\":false,\"message\":\"ID non valido\"}");
      }
    } else {
      server.send(400, "application/json", "{\"success\":false,\"message\":\"ID mancante\"}");
    }
  });
  
  // Esporta dati di training
  server.on("/api/training/export", HTTP_GET, [](){
    String csv = "temperature,tds,ph\n";
    for(int i = 0; i < phModel.dataCount; i++) {
      if (trainingData[i].valid) {
        csv += String(trainingData[i].temperature, 2) + ",";
        csv += String(trainingData[i].tds, 1) + ",";
        csv += String(trainingData[i].ph, 2) + "\n";
      }
    }
    
    server.sendHeader("Content-Disposition", "attachment; filename=training_data.csv");
    server.send(200, "text/csv", csv);
  });
  
  // Importa dati di training (formato: temp,tds,ph per riga)
  server.on("/api/training/import", HTTP_POST, [](){
    if (server.hasArg("data")) {
      String data = server.arg("data");
      int imported = importTrainingData(data);
      
      if (imported > 0) {
        server.send(200, "application/json", "{\"success\":true,\"message\":\"Importati " + String(imported) + " campioni\"}");
      } else {
        server.send(400, "application/json", "{\"success\":false,\"message\":\"Formato dati non valido\"}");
      }
    } else {
      server.send(400, "application/json", "{\"success\":false,\"message\":\"Dati mancanti\"}");
    }
  });
  
  // Endpoint per training AI veloce (usa sensori attuali)
  server.on("/api/train", HTTP_POST, [](){
    if (server.hasArg("ph") && tdsValid && temperature > -100) {
      float realPH = server.arg("ph").toFloat();
      // Nessuna validazione dati
      addTrainingData(temperature, tds, realPH);
      server.send(200, "text/plain", "Dati training aggiunti!");
    } else {
      server.send(400, "text/plain", "Sensori non pronti o pH mancante");
    }
  });
  
  // Reset modello AI
  server.on("/api/reset_ai", HTTP_POST, [](){
    initializePHModel();
    server.send(200, "text/plain", "Modello AI resettato");
  });
  
  // Controllo pompe
  server.on("/api/pump_control", HTTP_POST, [](){
    if (server.hasArg("action")) {
      String action = server.arg("action");
      String response = controlPump(action);
      server.send(200, "text/plain", response);
    } else {
      server.send(400, "text/plain", "Azione mancante");
    }
  });
  
  // === NUOVI ENDPOINT CONFIGURAZIONE PISCINA ===
  
  // Imposta superficie piscina
  server.on("/api/pool/surface", HTTP_POST, [](){
    if (server.hasArg("surface")) {
      float newSurface = server.arg("surface").toFloat();
      if (newSurface >= 5.0 && newSurface <= 500.0) {
        phModel.poolSurface = newSurface;
        poolSurfaceM2 = newSurface;
        savePHModel();
        server.send(200, "application/json", "{\"success\":true,\"message\":\"Superficie aggiornata: " + String(newSurface, 1) + "m²\"}");
      } else {
        server.send(400, "application/json", "{\"success\":false,\"message\":\"Superficie deve essere tra 5 e 500 m²\"}");
      }
    } else {
      server.send(400, "application/json", "{\"success\":false,\"message\":\"Parametro surface mancante\"}");
    }
  });
  
  // Calcolo durata erogazione intelligente
  server.on("/api/pump/calculate", HTTP_POST, [](){
    if (server.hasArg("target_ph")) {
      float targetPH = server.arg("target_ph").toFloat();
      if (targetPH >= 6.5 && targetPH <= 8.0) {
        // Usa SEMPRE la stima mediana locale come la correzione reale
        float phStart = estimatedPH_median;
        int duration = calculateSmartPumpDuration(phStart, targetPH);
        String json = "{";
        json += "\"current_ph\":" + String(phStart, 2) + ",";
        json += "\"target_ph\":" + String(targetPH, 2) + ",";
        json += "\"ph_difference\":" + String(abs(phStart - targetPH), 2) + ",";
        json += "\"pool_surface\":" + String(phModel.poolSurface, 1) + ",";
        json += "\"calculated_duration\":" + String(duration) + ",";
        json += "\"pump_type\":\"" + String(targetPH > phStart ? "ph_plus" : "ph_minus") + "\"";
        json += "}";
        server.send(200, "application/json", json);
      } else {
        server.send(400, "application/json", "{\"error\":\"Target pH deve essere tra 6.5 e 8.0\"}");
      }
    } else {
      server.send(400, "application/json", "{\"error\":\"Parametro target_ph mancante\"}");
    }
  });
  
  // === ENDPOINT TELEGRAM ===
  
  // Test notifica Telegram
  server.on("/api/telegram/test", HTTP_POST, [](){
    if (strlen(telegramToken) > 20 && strlen(telegramChatID) > 5) {
      String testMessage = "<b>Test Notifica Pool Monitor</b>\n";
      testMessage += "<b>Stato Sensori:</b>\n";
      testMessage += "Temperatura: " + String(temperature, 1) + "C\n";
      testMessage += "TDS: " + (tdsValid ? String(tds, 1) + " ppm" : "Sensore scollegato") + "\n";
      testMessage += "pH stimato: " + String(estimatedPH, 2);
      if (phModel.trained) {
        testMessage += " (AI:" + String(phModel.dataCount) + " campioni)\n";
      } else {
        testMessage += " (formula base)\n";
      }
      testMessage += "Orario: " + String(millis()/1000) + "s da avvio";
      
      sendTelegramMessage(testMessage);
      server.send(200, "text/plain", "Notifica di test inviata!");
    } else {
      server.send(400, "text/plain", "Telegram non configurato - inserire token e chat ID");
    }
  });
  
  // Abilita/Disabilita notifiche
  server.on("/api/telegram/toggle", HTTP_POST, [](){
    notificationEnabled = !notificationEnabled;
    String status = notificationEnabled ? "abilitate" : "disabilitate";
    server.send(200, "text/plain", "Notifiche " + status);
  });
  
  // Stato notifiche
  server.on("/api/telegram/status", HTTP_GET, [](){
    String json = "{";
    json += "\"enabled\":" + String(notificationEnabled ? "true" : "false") + ",";
    json += "\"configured\":" + String((strlen(telegramToken) > 20 && strlen(telegramChatID) > 5) ? "true" : "false") + ",";
    json += "\"last_notification\":" + String(lastNotificationTime) + ",";
    json += "\"sensor_status\":\"" + String(tdsValid ? "connesso" : "scollegato") + "\"";
    json += "}";
    
    server.sendHeader("Access-Control-Allow-Origin", "*");
    server.send(200, "application/json", json);
  });
  
  server.begin();
  Serial.println("Web server avviato");
}

String generateWebPage() {
  String html = "<!DOCTYPE html><html><head>";
  html += "<title>🏊 Pool Monitor & Control</title>";
  html += "<meta charset='UTF-8'>";
  html += "<meta name='viewport' content='width=device-width, initial-scale=1.0'>";
  html += "<style>";
  html += "body { font-family: Arial, sans-serif; margin: 0; background: linear-gradient(135deg, #667eea 0%, #764ba2 100%); min-height: 100vh; }";
  html += ".container { max-width: 900px; margin: 0 auto; padding: 20px; }";
  html += ".card { background: white; border-radius: 15px; padding: 20px; margin: 15px 0; box-shadow: 0 4px 15px rgba(0,0,0,0.1); }";
  html += ".sensor-value { font-size: 32px; font-weight: bold; color: #2196F3; margin: 10px 0; }";
  html += ".status { font-size: 14px; color: #666; margin: 5px 0; }";
  html += ".ph-section { background: linear-gradient(135deg, #e8f5e8 0%, #f0f8ff 100%); border-left: 5px solid #4CAF50; }";
  html += ".control-section { background: linear-gradient(135deg, #fff3e0 0%, #fce4ec 100%); border-left: 5px solid #ff9800; }";
  html += ".button { background: linear-gradient(135deg, #4CAF50 0%, #45a049 100%); color: white; padding: 15px 25px; border: none; border-radius: 8px; cursor: pointer; margin: 10px 10px; font-size: 16px; font-weight: bold; }";
  html += ".button:hover { transform: translateY(-2px); box-shadow: 0 4px 12px rgba(0,0,0,0.2); }";
  html += ".button.danger { background: linear-gradient(135deg, #f44336 0%, #d32f2f 100%); }";
  html += ".button.auto { background: linear-gradient(135deg, #2196F3 0%, #1976D2 100%); }";
  html += ".grid { display: grid; grid-template-columns: repeat(auto-fit, minmax(300px, 1fr)); gap: 20px; }";
  html += ".error { color: #f44336; }";
  html += ".success { color: #4CAF50; }";
  html += "</style>";
  html += "<script>";
  html += "function controlPump(action) {";
  html += "  fetch('/api/pump_control', { method: 'POST', headers: {'Content-Type': 'application/x-www-form-urlencoded'}, body: 'action=' + action })";
  html += "  .then(response => response.text())";
  html += "  .then(data => { alert(data); setTimeout(() => location.reload(), 1000); });";
  html += "}";
  html += "function trainAI() {";
  html += "  var ph = document.getElementById('phInput').value;";
  html += "  if (ph) {";
  html += "    fetch('/api/train', { method: 'POST', headers: {'Content-Type': 'application/x-www-form-urlencoded'}, body: 'ph=' + ph })";
  html += "    .then(response => response.text())";
  html += "    .then(data => { alert(data); location.reload(); });";
  html += "  } else { alert('Inserisci un valore di pH'); }";
  html += "}";
  html += "function resetAI() {";
  html += "  if (confirm('Resettare il modello AI?')) {";
  html += "    fetch('/api/reset_ai', { method: 'POST' })";
  html += "    .then(response => response.text())";
  html += "    .then(data => { alert(data); location.reload(); });";
  html += "  }";
  html += "}";
  html += "function testTelegram() {";
  html += "  fetch('/api/telegram/test', { method: 'POST' })";
  html += "  .then(response => response.text())";
  html += "  .then(data => { alert(data); });";
  html += "}";
  html += "function toggleNotifications() {";
  html += "  fetch('/api/telegram/toggle', { method: 'POST' })";
  html += "  .then(response => response.text())";
  html += "  .then(data => { alert(data); setTimeout(() => location.reload(), 1000); });";
  html += "}";
  html += "setInterval(() => location.reload(), 15000);";  // Auto refresh ogni 15 secondi
  html += "function updatePoolSurface() {";
  html += "  var surface = document.getElementById('poolSurface').value;";
  html += "  if (surface >= 5 && surface <= 500) {";
  html += "    fetch('/api/pool/surface', { method: 'POST', headers: {'Content-Type': 'application/x-www-form-urlencoded'}, body: 'surface=' + surface })";
  html += "    .then(response => response.json())";
  html += "    .then(data => { alert(data.message); if(data.success) location.reload(); });";
  html += "  } else { alert('Superficie deve essere tra 5 e 500 m²'); }";
  html += "}";
  html += "function calculateCorrection() {";
  html += "  var targetPH = document.getElementById('targetPH').value;";
  html += "  if (targetPH >= 6.5 && targetPH <= 8.0) {";
  html += "    fetch('/api/pump/calculate', { method: 'POST', headers: {'Content-Type': 'application/x-www-form-urlencoded'}, body: 'target_ph=' + targetPH })";
  html += "    .then(response => response.json())";
  html += "    .then(data => {";
  html += "      if (data.error) {";
  html += "        document.getElementById('correctionResult').innerHTML = '<span style=\"color: red;\">❌ ' + data.error + '</span>';";
  html += "      } else {";
  html += "        var result = '<span style=\"color: green;\">✅ Calcolo completato:</span><br>';";
  html += "        result += '🎯 Da pH ' + data.current_ph + ' a pH ' + data.target_ph + ' (diff: ' + data.ph_difference + ')<br>';";
  html += "        result += '🏊 Piscina: ' + data.pool_surface + ' m²<br>';";
  html += "        result += '⏱️ Durata: ' + (data.calculated_duration/1000).toFixed(1) + ' secondi<br>';";
  html += "        result += '💊 Prodotto: ' + (data.pump_type === 'ph_plus' ? 'pH+ (bicarbonato)' : 'pH- (acido muriatico)');";
  html += "        document.getElementById('correctionResult').innerHTML = result;";
  html += "      }";
  html += "    });";
  html += "  } else { alert('pH target deve essere tra 6.5 e 8.0'); }";
  html += "}";
  html += "</script>";
  html += "</head><body>";
  
  html += "<div class='container'>";
  html += "<h1 style='color: white; text-align: center; margin-bottom: 30px;'>🏊 Pool Monitor & Control</h1>";
  
  html += "<div class='grid'>";
  
  // Sensori
  html += "<div class='card'>";
  html += "<h3>🌡️ Temperatura</h3>";
  if (temperature > -100) {
    html += "<div class='sensor-value'>" + String(temperature, 1) + " °C</div>";
    html += "<div class='status'>DS18B20 attivo</div>";
  } else {
    html += "<div class='sensor-value error'>ERRORE</div>";
    html += "<div class='status error'>Sensore scollegato</div>";
  }
  html += "</div>";
  
  html += "<div class='card'>";
  html += "<h3>💧 TDS</h3>";
  if (tdsValid) {
    html += "<div class='sensor-value'>" + String(tds, 1) + " ppm</div>";
    String quality = tds < 150 ? "✅ Ottima" : tds < 300 ? "⚠️ Buona" : "❌ Mediocre";
    html += "<div class='status'>" + quality + "</div>";
  } else {
    html += "<div class='sensor-value error'>N/A</div>";
    html += "<div class='status error'>Sensore non connesso</div>";
  }
  html += "</div>";
  
  html += "</div>";
  
  // pH e Controllo
  html += "<div class='card ph-section'>";
  html += "<h2>🧪 Controllo pH Intelligente</h2>";
  
  // === SOLO MODELLO AI ===
  html += "<div style='background: #e8f5e8; padding: 20px; border-radius: 10px; margin: 15px 0; border-left: 5px solid #28a745;'>";
  html += "<h3>🧪 pH stimato (mediana locale)</h3>";
  html += "<div class='sensor-value' style='font-size: 44px; margin: 10px 0; color: #388e3c;'>" + String(estimatedPH_median, 2) + "</div>";
  html += "<div class='status'><b>Stima mediana locale:</b> " + String(estimatedPH_median, 2) + " (mediana dei 3 campioni più vicini T/TDS)</div>";
  html += "<div class='status' style='font-size:13px;color:#888;margin-top:10px;'>Stima AI (regressione): <span style='font-family:monospace'>" + String(estimatedPH, 2) + "</span> &nbsp; | &nbsp; Formula: <span style='font-family:monospace'>pH = " + String(phModel.a, 4) + "×T + " + String(phModel.b, 6) + "×TDS + " + String(phModel.c, 2) + "</span></div>";
  html += "<div class='status' style='font-size:12px;color:#aaa;'>La regressione AI è mostrata solo come riferimento</div>";
  html += "</div>";
  
  // Analisi e raccomandazioni
  html += "<div style='background: #f5f5f5; padding: 15px; border-radius: 8px; margin: 15px 0;'>";
  html += "<h4>📊 Analisi pH</h4>";
  
  String phStatus = "";
  String recommendation = "";
  
  if (estimatedPH_median < PH_TARGET_MIN - PH_TOLERANCE) {
    phStatus = "🔴 TROPPO ACIDO";
    recommendation = "Consiglio: Aggiungi pH+";
  } else if (estimatedPH_median > PH_TARGET_MAX + PH_TOLERANCE) {
    phStatus = "🔵 TROPPO BASICO";
    recommendation = "Consiglio: Aggiungi pH-";
  } else {
    phStatus = "✅ PERFETTO";
    recommendation = "Nessuna correzione necessaria";
  }
  
  html += "<div class='status'><strong>Stato: " + phStatus + "</strong></div>";
  html += "<div class='status'>Range target: " + String(PH_TARGET_MIN, 1) + " - " + String(PH_TARGET_MAX, 1) + "</div>";
  html += "<div class='status'><strong>" + recommendation + "</strong></div>";
  html += "</div>";
  
  // === CONFIGURAZIONE PISCINA ===
  html += "<div style='background: #e7f3ff; padding: 15px; border-radius: 8px; margin: 15px 0; border-left: 4px solid #007bff;'>";
  html += "<h4>🏊 Configurazione Piscina</h4>";
  html += "<div style='margin: 10px 0;'>";
  html += "<label>Superficie (m²):</label>";
  html += "<input type='number' id='poolSurface' value='" + String(phModel.poolSurface, 1) + "' step='0.5' min='5' max='500' style='padding: 5px; margin: 0 10px; width: 80px; border: 1px solid #ccc; border-radius: 3px;'>";
  html += "<button onclick='updatePoolSurface()' style='background: #007bff; color: white; padding: 5px 10px; border: none; border-radius: 3px;'>Aggiorna</button>";
  html += "</div>";
  
  float poolVolume = phModel.poolSurface * poolDepthM;
  html += "<div class='status'>Volume stimato: " + String(poolVolume, 1) + " m³ (superficie × " + String(poolDepthM, 1) + "m profondità)</div>";
  html += "</div>";
  
  // === CALCOLO CORREZIONE INTELLIGENTE ===
  if (phModel.trained) {
    html += "<div style='background: #fff8e1; padding: 15px; border-radius: 8px; margin: 15px 0; border-left: 4px solid #ff9800;'>";
    html += "<h4>⚙️ Calcolo Correzione Intelligente</h4>";
    html += "<div style='margin: 10px 0;'>";
    html += "<label>pH target:</label>";
    html += "<input type='number' id='targetPH' value='" + String((PH_TARGET_MIN + PH_TARGET_MAX) / 2.0, 1) + "' step='0.1' min='6.5' max='8.0' style='padding: 5px; margin: 0 10px; width: 60px; border: 1px solid #ccc; border-radius: 3px;'>";
    html += "<button onclick='calculateCorrection()' style='background: #ff9800; color: white; padding: 5px 15px; border: none; border-radius: 3px;'>🧮 Calcola</button>";
    html += "</div>";
    html += "<div id='correctionResult' style='margin: 10px 0; font-weight: bold;'></div>";
    html += "</div>";
  }
  
  // === TRAINING AI VELOCE ===
  if (tdsValid && temperature > -100) {
    html += "<div style='background: #f0f8ff; padding: 15px; border-radius: 8px; margin: 15px 0; border-left: 4px solid #4CAF50;'>";
    html += "<h4>📚 Training AI Veloce</h4>";
    html += "<p>Misura il pH reale con strumento di precisione:</p>";
    html += "<div style='display: flex; align-items: center; gap: 10px;'>";
    html += "<input type='number' id='phInput' placeholder='7.2' step='0.1' style='padding: 8px; border: 2px solid #ddd; border-radius: 5px; width: 80px;'>";
    html += "<button onclick='trainAI()' style='background: #4CAF50; color: white; padding: 10px 15px; border: none; border-radius: 5px;'>➕ Aggiungi</button>";
    html += "<button onclick='resetAI()' style='background: #f44336; color: white; padding: 10px 15px; border: none; border-radius: 5px;'>🔄 Reset</button>";
    html += "</div>";
    html += "<div class='status'>T=" + String(temperature, 1) + "°C, TDS=" + String(tds, 1) + "ppm (valori attuali)</div>";
    html += "</div>";
  }
  
  html += "</div>";
  
  // Controlli pompe
  html += "<div class='card control-section'>";
  html += "<h2>🔧 Controllo Pompe Intelligente</h2>";
  
  if (phModel.trained) {
    html += "<p>✅ Sistema AI pronto - correzione automatica basata su superficie piscina</p>";
    html += "<button class='button auto' onclick='controlPump(\"auto\")'>🤖 Correzione Automatica Intelligente</button>";
  } else {
    html += "<p>⚠️ Sistema AI non allenato - solo controlli manuali disponibili</p>";
    html += "<div style='background: #fff3cd; padding: 10px; border-radius: 5px; margin: 10px 0;'>";
    html += "Per attivare la correzione intelligente, aggiungi almeno 3 misurazioni pH precise.";
    html += "</div>";
  }
  
  html += "<div style='margin: 15px 0;'>";
  html += "<h4>Controlli Manuali (Test):</h4>";
  html += "<button class='button' onclick='controlPump(\"ph_plus\")'>➕ pH+ (3 sec test)</button>";
  html += "<button class='button danger' onclick='controlPump(\"ph_minus\")'>➖ pH- (3 sec test)</button>";
  html += "<button class='button' onclick='controlPump(\"status\")'>📊 Status Pompe</button>";
  html += "</div>";
  
  html += "</div>";
  
  // Link alla pagina training
  html += "<div class='card'>";
  html += "<h3>🧠 Gestione Avanzata AI</h3>";
  html += "<p>Gestione completa dei dati di training per la regressione AI del pH</p>";
  html += "<a href='/training' style='text-decoration: none;'>";
  html += "<button class='button auto'>📊 Gestione Avanzata Dati</button>";
  html += "</a>";
  html += "</div>";
  
  // Controlli Telegram
  html += "<div class='card'>";
  html += "<h3>📱 Notifiche Telegram</h3>";
  
  bool telegramConfigured = (strlen(telegramToken) > 20 && strlen(telegramChatID) > 5);
  
  if (telegramConfigured) {
    html += "<div class='status success'>✅ Telegram configurato</div>";
    html += "<div class='status'>Notifiche: " + String(notificationEnabled ? "🔔 Abilitate" : "🔕 Disabilitate") + "</div>";
    html += "<div class='status'>Ultimo invio: " + String(lastNotificationTime > 0 ? String((millis() - lastNotificationTime)/1000) + "s fa" : "Mai") + "</div>";
    
    html += "<button class='button' onclick='testTelegram()'>📱 Test Notifica</button>";
    html += "<button class='button auto' onclick='toggleNotifications()'>" + String(notificationEnabled ? "🔕 Disabilita" : "🔔 Abilita") + "</button>";
  } else {
    html += "<div class='status error'>❌ Telegram NON configurato</div>";
    html += "<div class='status'>Modifica il codice e inserisci:</div>";
    html += "<div class='status'>• Bot Token (da @BotFather)</div>";
    html += "<div class='status'>• Chat ID (dal tuo bot)</div>";
    html += "<div class='status'>Riavvia ESP32 dopo la configurazione</div>";
  }
  
  html += "</div>";
  
  // Info sistema
  html += "<div class='card'>";
  html += "<h3>📶 Sistema</h3>";
  html += "<div class='status'>IP Sensori: " + WiFi.localIP().toString() + "</div>";
  html += "<div class='status'>IP Pompe: " + String(pompe_ip) + "</div>";
  html += "<div class='status'>WiFi: " + String(WiFi.RSSI()) + " dBm</div>";
  html += "<div class='status'>Uptime: " + String(millis()/1000) + " secondi</div>";
  html += "</div>";
  
  html += "</div></body></html>";
  
  return html;
}

// === NUOVA PAGINA PER GESTIONE TRAINING ===
String generateTrainingPage() {
  String html = "<!DOCTYPE html><html><head>";
  html += "<title>🤖 AI Training Manager</title>";
  html += "<meta charset='UTF-8'>";
  html += "<meta name='viewport' content='width=device-width, initial-scale=1.0'>";
  html += "<style>";
  html += "body { font-family: Arial, sans-serif; margin: 0; background: linear-gradient(135deg, #667eea 0%, #764ba2 100%); min-height: 100vh; }";
  html += ".container { max-width: 1200px; margin: 0 auto; padding: 20px; }";
  html += ".card { background: white; border-radius: 15px; padding: 20px; margin: 15px 0; box-shadow: 0 4px 15px rgba(0,0,0,0.1); }";
  html += ".button { background: linear-gradient(135deg, #4CAF50 0%, #45a049 100%); color: white; padding: 10px 15px; border: none; border-radius: 5px; cursor: pointer; margin: 5px; font-size: 14px; }";
  html += ".button:hover { transform: translateY(-1px); box-shadow: 0 2px 8px rgba(0,0,0,0.2); }";
  html += ".button.danger { background: linear-gradient(135deg, #f44336 0%, #d32f2f 100%); }";
  html += ".button.primary { background: linear-gradient(135deg, #2196F3 0%, #1976D2 100%); }";
  html += ".input { padding: 8px; border: 2px solid #ddd; border-radius: 5px; margin: 5px; }";
  html += ".table { width: 100%; border-collapse: collapse; margin: 10px 0; }";
  html += ".table th, .table td { padding: 8px; text-align: left; border-bottom: 1px solid #ddd; }";
  html += ".table th { background-color: #f2f2f2; }";
  html += ".grid { display: grid; grid-template-columns: 1fr 1fr; gap: 20px; }";
  html += ".status { color: #666; margin: 5px 0; }";
  html += ".success { color: #4CAF50; }";
  html += ".error { color: #f44336; }";
  html += "</style>";
  html += "<script>";
  
  // Funzioni JavaScript per gestione training
  html += "function loadTrainingData() {";
  html += "  fetch('/api/training/list')";
  html += "  .then(response => response.json())";
  html += "  .then(data => {";
  html += "    updateTrainingTable(data.samples);";
  html += "    updateModelInfo(data);";
  html += "  });";
  html += "}";
  
  html += "function updateTrainingTable(samples) {";
  html += "  var tbody = document.getElementById('trainingTableBody');";
  html += "  tbody.innerHTML = '';";
  html += "  samples.forEach(function(sample) {";
  html += "    var row = tbody.insertRow();";
  html += "    row.innerHTML = '<td>' + sample.id + '</td>' +";
  html += "                   '<td>' + sample.temperature + '°C</td>' +";
  html += "                   '<td>' + sample.tds + ' ppm</td>' +";
  html += "                   '<td>' + sample.ph + '</td>' +";
  html += "                   '<td><button class=\"button danger\" onclick=\"deleteSample(' + sample.id + ')\">🗑️</button></td>';";
  html += "  });";
  html += "}";
  
  html += "function updateModelInfo(data) {";
  html += "  document.getElementById('sampleCount').textContent = data.count + '/20';";
  html += "  document.getElementById('modelStatus').textContent = data.model_trained ? '✅ Allenato' : '❌ Non allenato';";
  html += "  if (data.model_trained) {";
  html += "    document.getElementById('formula').innerHTML = 'Equazione: pH = ' + data.formula.a.toFixed(4) + '×T + ' + data.formula.b.toFixed(6) + '×TDS + ' + data.formula.c.toFixed(2);";
  html += "  } else {";
  html += "    document.getElementById('formula').innerHTML = 'Aggiungi almeno 3 misurazioni per attivare la regressione';";
  html += "  }";
  html += "}";
  
  html += "function addTrainingData() {";
  html += "  var temp = document.getElementById('tempInput').value;";
  html += "  var tds = document.getElementById('tdsInput').value;";
  html += "  var ph = document.getElementById('phInput').value;";
  html += "  ";
  html += "  if (!temp || !tds || !ph) {";
  html += "    alert('Compila tutti i campi');";
  html += "    return;";
  html += "  }";
  html += "  ";
  html += "  fetch('/api/training/add', {";
  html += "    method: 'POST',";
  html += "    headers: {'Content-Type': 'application/x-www-form-urlencoded'},";
  html += "    body: 'temp=' + temp + '&tds=' + tds + '&ph=' + ph";
  html += "  })";
  html += "  .then(response => response.json())";
  html += "  .then(data => {";
  html += "    alert(data.message);";
  html += "    if (data.success) {";
  html += "      document.getElementById('tempInput').value = '';";
  html += "      document.getElementById('tdsInput').value = '';";
  html += "      document.getElementById('phInput').value = '';";
  html += "      loadTrainingData();";
  html += "    }";
  html += "  });";
  html += "}";
  
  html += "function deleteSample(id) {";
  html += "  if (confirm('Eliminare questa misurazione?')) {";
  html += "    fetch('/api/training/delete', {";
  html += "      method: 'POST',";
  html += "      headers: {'Content-Type': 'application/x-www-form-urlencoded'},";
  html += "      body: 'id=' + id";
  html += "    })";
  html += "    .then(response => response.json())";
  html += "    .then(data => {";
  html += "      alert(data.message);";
  html += "      if (data.success) loadTrainingData();";
  html += "    });";
  html += "  }";
  html += "}";
  
  html += "function exportData() {";
  html += "  window.open('/api/training/export', '_blank');";
  html += "}";
  
  html += "function importData() {";
  html += "  var data = document.getElementById('importData').value;";
  html += "  if (!data.trim()) {";
  html += "    alert('Inserisci i dati da importare');";
  html += "    return;";
  html += "  }";
  html += "  ";
  html += "  fetch('/api/training/import', {";
  html += "    method: 'POST',";
  html += "    headers: {'Content-Type': 'application/x-www-form-urlencoded'},";
  html += "    body: 'data=' + encodeURIComponent(data)";
  html += "  })";
  html += "  .then(response => response.json())";
  html += "  .then(data => {";
  html += "    alert(data.message);";
  html += "    if (data.success) {";
  html += "      document.getElementById('importData').value = '';";
  html += "      loadTrainingData();";
  html += "    }";
  html += "  });";
  html += "}";
  
  html += "function resetModel() {";
  html += "  if (confirm('Cancellare tutte le misurazioni pH?')) {";
  html += "    fetch('/api/reset_ai', { method: 'POST' })";
  html += "    .then(response => response.text())";
  html += "    .then(data => {";
  html += "      alert(data);";
  html += "      loadTrainingData();";
  html += "    });";
  html += "  }";
  html += "}";
  
  html += "function useCurrent() {";
  html += "  fetch('/api/sensors')";
  html += "  .then(response => response.json())";
  html += "  .then(data => {";
  html += "    if (data.temperature && data.tds) {";
  html += "      document.getElementById('tempInput').value = data.temperature;";
  html += "      document.getElementById('tdsInput').value = data.tds;";
  html += "    } else {";
  html += "      alert('Sensori non disponibili');";
  html += "    }";
  html += "  });";
  html += "}";
  
  html += "window.onload = function() { loadTrainingData(); };";
  html += "</script>";
  html += "</head><body>";
  
  html += "<div class='container'>";
  html += "<h1 style='color: white; text-align: center;'>📊 Gestione Dati pH</h1>";
  html += "<div style='text-align: center; margin: 20px 0;'>";
  html += "<a href='/' style='color: white; text-decoration: none; background: rgba(255,255,255,0.2); padding: 10px 20px; border-radius: 5px;'>← Torna al Monitor</a>";
  html += "</div>";
  
  html += "<div class='grid'>";
  
  // Pannello aggiunta dati
  html += "<div class='card'>";
  html += "<h3>➕ Aggiungi Misurazione pH</h3>";
  html += "<div style='margin: 10px 0;'>";
  html += "<label>Temperatura (°C):</label><br>";
  html += "<input type='number' id='tempInput' class='input' placeholder='25.0' step='0.1' style='width: 100px;'>";
  html += "<button class='button primary' onclick='useCurrent()'>📊 Usa Attuali</button>";
  html += "</div>";
  html += "<div style='margin: 10px 0;'>";
  html += "<label>TDS (ppm):</label><br>";
  html += "<input type='number' id='tdsInput' class='input' placeholder='150' step='1' style='width: 100px;'>";
  html += "</div>";
  html += "<div style='margin: 10px 0;'>";
  html += "<label>pH Reale (misurato):</label><br>";
  html += "<input type='number' id='phInput' class='input' placeholder='7.2' step='0.1' style='width: 100px;'>";
  html += "</div>";
  html += "<button class='button' onclick='addTrainingData()'>✅ Aggiungi Misurazione</button>";
  html += "</div>";
  
  // Pannello info modello
  html += "<div class='card'>";
  html += "<h3>📊 Stato pH</h3>";
  html += "<div class='status'>Misurazioni: <span id='sampleCount'>0/20</span></div>";
  html += "<div class='status'>Status: <span id='modelStatus'>❌ Non allenato</span></div>";
  html += "<div class='status'>Equazione: <span id='formula'>Aggiungi almeno 3 misurazioni per attivare la regressione</span></div>";
  html += "<div style='margin: 15px 0;'>";
  html += "<button class='button danger' onclick='resetModel()'>🔄 Reset Dati</button>";
  html += "</div>";
  html += "</div>";
  
  html += "</div>";
  
  // Tabella dati training
  html += "<div class='card'>";
  html += "<h3>📋 Misurazioni pH</h3>";
  html += "<table class='table'>";
  html += "<thead>";
  html += "<tr><th>ID</th><th>Temperatura</th><th>TDS</th><th>pH</th><th>Azioni</th></tr>";
  html += "</thead>";
  html += "<tbody id='trainingTableBody'>";
  html += "<tr><td colspan='5'>Caricamento...</td></tr>";
  html += "</tbody>";
  html += "</table>";
  html += "</div>";
  
  // Pannello import/export
  html += "<div class='card'>";
  html += "<h3>💾 Import/Export Dati</h3>";
  html += "<div style='margin: 10px 0;'>";
  html += "<button class='button primary' onclick='exportData()'>📤 Esporta CSV</button>";
  html += "<span style='margin-left: 20px; color: #666;'>Scarica tutti i dati in formato CSV</span>";
  html += "</div>";
  html += "<div style='margin: 10px 0;'>";
  html += "<label>Importa dati (formato: temp,tds,ph per riga):</label><br>";
  html += "<textarea id='importData' class='input' placeholder='25.0,150,7.2\\n26.5,160,7.1' style='width: 100%; height: 80px; resize: vertical;'></textarea>";
  html += "<br><button class='button' onclick='importData()'>📥 Importa Dati</button>";
  html += "</div>";
  html += "</div>";
  
  html += "</div></body></html>";
  
  return html;
}

String controlPump(String action) {
  if (WiFi.status() != WL_CONNECTED) {
    return "Errore: WiFi non connesso";
  }
  
  http.begin("http://" + String(pompe_ip) + "/api/control");
  http.addHeader("Content-Type", "application/x-www-form-urlencoded");
  
  String postData = "";
  
  if (action == "auto") {
    // Calcola azione automatica intelligente
    if (!phModel.trained) {
      return "Errore: Modello AI non allenato - aggiungi almeno 3 misurazioni pH";
    }
    
    // Usa SEMPRE la stima mediana locale
    float phStart = estimatedPH_median;
    float targetPH = (PH_TARGET_MIN + PH_TARGET_MAX) / 2.0; // pH target ideale
    
    if (abs(phStart - targetPH) < PH_TOLERANCE) {
      return "pH già nel range ottimale (" + String(phStart, 2) + ")";
    }
    
    int duration = calculateSmartPumpDuration(phStart, targetPH);
    
    if (duration == 0) {
      return "Correzione troppo piccola - pH OK (" + String(phStart, 2) + ")";
    }
    
    if (phStart < targetPH) {
      postData = "pump=ph_plus&duration=" + String(duration);
    } else {
      postData = "pump=ph_minus&duration=" + String(duration);
    }
    // Messaggio di debug
    Serial.println("[AUTO] Correzione intelligente (mediana): da pH " + String(phStart,2) + " a " + String(targetPH,2) + ", durata " + String(duration) + " ms");
  } else if (action == "ph_plus") {
    postData = "pump=ph_plus&duration=3000";  // 3 secondi fissi per test
  } else if (action == "ph_minus") {
    postData = "pump=ph_minus&duration=3000"; // 3 secondi fissi per test
  } else if (action == "status") {
    postData = "pump=status&duration=0";
  } else {
    return "Azione non valida";
  }
  
  int httpResponseCode = http.POST(postData);
  String response = "";
  
  if (httpResponseCode > 0) {
    response = http.getString();
  } else {
    response = "Errore comunicazione con pompe: " + String(httpResponseCode);
  }
  
  http.end();
  return response;
}

int calculateSmartPumpDuration(float currentPH, float targetPH) {
  float phDifference = abs(targetPH - currentPH);
  
  if (phDifference < 0.05) {
    return 0; // Nessuna correzione necessaria
  }
  
  // === CALCOLO INTELLIGENTE ===
  
  // Volume stimato piscina (superficie × profondità media)
  float poolVolume = phModel.poolSurface * poolDepthM; // m³
  
  // Fattori di correzione basati su esperienza piscine
  // pH+ è meno potente del pH-, quindi serve più tempo
  float correctionFactor;
  if (targetPH > currentPH) {
    // Serve pH+ (bicarbonato di sodio - meno reattivo)
    correctionFactor = 800.0; // ms per m³ per 0.1 pH
  } else {
    // Serve pH- (acido muriatico - più reattivo)
    correctionFactor = 500.0; // ms per m³ per 0.1 pH
  }
  
  // Formula: durata = differenza_pH × volume × fattore / 0.1
  int duration = (int)((phDifference * poolVolume * correctionFactor) / 0.1);
  
  // Limiti di sicurezza
  duration = constrain(duration, 1000, 30000); // Da 1 a 30 secondi max
  
  // Salva per debug
  lastCalculatedDuration = duration;
  
  Serial.println("=== CALCOLO EROGAZIONE INTELLIGENTE ===");
  Serial.println("pH attuale: " + String(currentPH, 2));
  Serial.println("pH target: " + String(targetPH, 2));
  Serial.println("Differenza: " + String(phDifference, 2));
  Serial.println("Superficie: " + String(phModel.poolSurface, 1) + " m²");
  Serial.println("Volume stimato: " + String(poolVolume, 1) + " m³");
  Serial.println("Fattore correzione: " + String(correctionFactor, 0));
  Serial.println("Durata calcolata: " + String(duration) + " ms (" + String(duration/1000.0, 1) + "s)");
  Serial.println("Prodotto: " + String(targetPH > currentPH ? "pH+" : "pH-"));
  
  return duration;
}

void readSensors() {
  // Lettura temperatura
  sensors.requestTemperatures();
  temperature = sensors.getTempCByIndex(0);
  
  // Lettura TDS con media di 3 campioni
  int totalRaw = 0;
  for(int i = 0; i < 3; i++) {
    totalRaw += analogRead(TDS_PIN);
    delay(10);
  }
  int rawValue = totalRaw / 3;
  
  float voltage = rawValue * (3.3 / 4095.0);
  
  if (voltage >= TDS_MIN_VOLTAGE && voltage <= TDS_MAX_VOLTAGE && rawValue > 10) {
    float tempCoeff = 1.0 + 0.02 * (temperature - TEMPERATURE_COMPENSATION);
    float rawTDS = (voltage / TDS_MAX_VOLTAGE) * TDS_MAX_PPM;
    float newTDS = rawTDS / tempCoeff;
    
    // Media mobile semplice
    tdsReadings[readingIndex] = newTDS;
    readingIndex = (readingIndex + 1) % 5;
    
    float sum = 0;
    for(int i = 0; i < 5; i++) {
      sum += tdsReadings[i];
    }
    tds = sum / 5.0;
    tdsValid = true;
  } else {
    tdsValid = false;
  }
  
  // === RILEVAMENTO CAMBIO STATO SENSORE TDS ===
  checkWaterLevelNotification(previousTdsValid);
  
  // Aggiorna lo stato precedente per la prossima lettura
  previousTdsValid = tdsValid;
  
  // Stima pH con AI o formula base
  if (temperature > -100 && tdsValid) {
    estimatePH(temperature, tds);
  } else {
    estimatedPH = 7.0;
  }
}

void loop() {
  unsigned long currentTime = millis();
  
  digitalWrite(LED_PIN, HIGH);
  
  // Leggi sensori ogni 3 secondi
  if (currentTime - lastSensorRead >= 3000) {
    readSensors();
    lastSensorRead = currentTime;
    
    // Output seriale
    Serial.print("T: " + String(temperature, 1) + "°C");
    Serial.print(" | TDS: " + (tdsValid ? String(tds, 1) + "ppm" : "N/A"));
    Serial.print(" | pH: " + String(estimatedPH, 2));
    if (phModel.trained) {
      Serial.print(" (AI:" + String(phModel.dataCount) + " campioni)");
    } else {
      Serial.print(" (formula base)");
    }
    Serial.println(" | WiFi: " + String(WiFi.RSSI()) + "dBm");
  }
  
  // Gestione web server
  if (WiFi.status() == WL_CONNECTED) {
    server.handleClient();
  }
  
  digitalWrite(LED_PIN, LOW);
  delay(100);
}

// === FUNZIONI AI pH MODEL ===

void loadPHModel() {
  Serial.println("🤖 Caricamento modello AI pH...");
  
  EEPROM.get(0, phModel);
  EEPROM.get(sizeof(PHModel), trainingData);
  
  // Controlli di validità
  bool modelValid = true;
  
  if (phModel.dataCount < 0 || phModel.dataCount > MAX_TRAINING_DATA) {
    Serial.println("⚠️ Contatore dati corrotto");
    modelValid = false;
  }
  
  if (phModel.poolSurface < 5.0 || phModel.poolSurface > 500.0) {
    Serial.println("⚠️ Superficie piscina non valida, uso default 25m²");
    phModel.poolSurface = 25.0;
  }
  
  if (phModel.trained && (abs(phModel.a) > 0.2 || abs(phModel.b) > 0.01 || phModel.c < 5.0 || phModel.c > 9.0)) {
    Serial.println("⚠️ Coefficienti del modello fuori range");
    Serial.println("  a=" + String(phModel.a, 6) + " b=" + String(phModel.b, 6) + " c=" + String(phModel.c, 2));
    modelValid = false;
  }
  
  // Verifica pH nei dati di training
  for(int i = 0; i < phModel.dataCount; i++) {
    if (trainingData[i].valid && (trainingData[i].ph < 5.0 || trainingData[i].ph > 9.0)) {
      Serial.println("⚠️ Dati pH fuori range nel campione " + String(i));
      modelValid = false;
      break;
    }
  }
  
  if (!modelValid) {
    Serial.println("🔄 Reset modello per dati non validi");
    initializePHModel();
  } else {
    // Aggiorna variabile globale superficie
    poolSurfaceM2 = phModel.poolSurface;
    
    Serial.print("✅ Modello caricato: ");
    Serial.print(phModel.dataCount);
    Serial.print(" campioni, ");
    Serial.print(phModel.trained ? "allenato" : "non allenato");
    Serial.println(", superficie: " + String(phModel.poolSurface, 1) + "m²");
    
    if (phModel.trained) {
      Serial.println("📈 Equazione: pH = " + String(phModel.a, 4) + "×T + " + String(phModel.b, 6) + "×TDS + " + String(phModel.c, 2));
    }
  }
}

void savePHModel() {
  EEPROM.put(0, phModel);
  EEPROM.put(sizeof(PHModel), trainingData);
  EEPROM.commit();
  Serial.println("💾 Modello salvato");
}

void initializePHModel() {
  phModel.a = 0;        // Nessuna correzione temperatura iniziale
  phModel.b = 0;        // Nessuna correzione TDS iniziale  
  phModel.c = 7.2;      // pH base neutro
  phModel.dataCount = 0;
  phModel.trained = false;
  
  for(int i = 0; i < MAX_TRAINING_DATA; i++) {
    trainingData[i].valid = false;
  }
  
  savePHModel();
  Serial.println("🔄 Modello AI inizializzato - pH base: 7.2");
}

void addTrainingData(float temp, float tdsVal, float realPH) {
  if (phModel.dataCount >= MAX_TRAINING_DATA) {
    // Sposta dati indietro
    for(int i = 0; i < MAX_TRAINING_DATA - 1; i++) {
      trainingData[i] = trainingData[i + 1];
    }
    phModel.dataCount = MAX_TRAINING_DATA - 1;
  }
  
  int index = phModel.dataCount;
  trainingData[index].temperature = temp;
  trainingData[index].tds = tdsVal;
  trainingData[index].ph = realPH;
  trainingData[index].valid = true;
  
  phModel.dataCount++;
  
  Serial.print("📚 Training: T=");
  Serial.print(temp, 1);
  Serial.print("°C, TDS=");
  Serial.print(tdsVal, 1);
  Serial.print("ppm, pH=");
  Serial.println(realPH, 2);
  
  if (phModel.dataCount >= 3) {
    trainPHModel();
  }
  
  savePHModel();
}

void trainPHModel() {
  if (phModel.dataCount < 3) return;
  
  Serial.println("🧠 Training modello pH...");
  
  // Raccogli dati validi
  float temps[MAX_TRAINING_DATA];
  float tdss[MAX_TRAINING_DATA]; 
  float phs[MAX_TRAINING_DATA];
  int n = 0;
  
  for(int i = 0; i < phModel.dataCount; i++) {
    if (trainingData[i].valid) {
      temps[n] = trainingData[i].temperature;
      tdss[n] = trainingData[i].tds;
      phs[n] = trainingData[i].ph;
      n++;
    }
  }
  
  if (n < 3) return;
  
  // Calcola medie
  float meanT = 0, meanTDS = 0, meanPH = 0;
  for(int i = 0; i < n; i++) {
    meanT += temps[i];
    meanTDS += tdss[i];
    meanPH += phs[i];
  }
  meanT /= n;
  meanTDS /= n;
  meanPH /= n;
  
  // Calcola varianze per info
  float varT = 0, varTDS = 0;
  for(int i = 0; i < n; i++) {
    varT += (temps[i] - meanT) * (temps[i] - meanT);
    varTDS += (tdss[i] - meanTDS) * (tdss[i] - meanTDS);
  }
  varT /= n;
  varTDS /= n;
  
  Serial.println("📊 Analisi dati:");
  Serial.println("  Temperatura: " + String(meanT, 1) + "°C ± " + String(sqrt(varT), 1));
  Serial.println("  TDS: " + String(meanTDS, 0) + " ppm ± " + String(sqrt(varTDS), 0));
  Serial.println("  pH: " + String(meanPH, 2) + " ± variabile");
  
  // === SEMPRE REGRESSIONE LINEARE (rimossa logica di decisione) ===
  
  // Regressione lineare: pH = a*T + b*TDS + c
  float sumXT = 0, sumXTDS = 0, sumXY_T = 0, sumXY_TDS = 0;
  float sumX2T = 0, sumX2TDS = 0, sumXTXTDS = 0;
  
  for(int i = 0; i < n; i++) {
    float dT = temps[i] - meanT;
    float dTDS = tdss[i] - meanTDS;
    float dPH = phs[i] - meanPH;
    
    sumXT += dT;
    sumXTDS += dTDS;
    sumXY_T += dT * dPH;
    sumXY_TDS += dTDS * dPH;
    sumX2T += dT * dT;
    sumX2TDS += dTDS * dTDS;
    sumXTXTDS += dT * dTDS;
  }
  
  // Risolvi sistema 2x2 per regressione multipla
  float det = sumX2T * sumX2TDS - sumXTXTDS * sumXTXTDS;
  
  if (abs(det) > 0.001) {
    // Regressione multipla completa
    phModel.a = (sumXY_T * sumX2TDS - sumXY_TDS * sumXTXTDS) / det;
    phModel.b = (sumXY_TDS * sumX2T - sumXY_T * sumXTXTDS) / det;
  } else {
    // Sistema quasi singolare - usa regressione semplice sulla variabile dominante
    if (sumX2T > sumX2TDS) {
      // Più variazione in temperatura
      phModel.a = (sumX2T > 0.001) ? sumXY_T / sumX2T : 0;
      phModel.b = 0;
    } else {
      // Più variazione in TDS
      phModel.a = 0;
      phModel.b = (sumX2TDS > 0.001) ? sumXY_TDS / sumX2TDS : 0;
    }
  }
  
  phModel.c = meanPH; // Intercetta
  
  // Limiti ragionevoli per evitare valori assurdi
  phModel.a = constrain(phModel.a, -0.2, 0.2);
  phModel.b = constrain(phModel.b, -0.01, 0.01);
  
  // Test accuratezza
  float rmse = 0;
  Serial.println("🧪 Test predizioni:");
  for(int i = 0; i < n; i++) {
    float predicted = phModel.a * temps[i] + phModel.b * tdss[i] + phModel.c;
    float error = predicted - phs[i];
    rmse += error * error;
    
    Serial.println("  T=" + String(temps[i], 1) + " TDS=" + String(tdss[i], 0) + 
                   " → pH reale=" + String(phs[i], 2) + 
                   " predetto=" + String(predicted, 2) + 
                   " errore=" + String(error, 2));
  }
  rmse = sqrt(rmse / n);
  
  phModel.trained = true;
  
  Serial.println("✅ Modello allenato con REGRESSIONE FORZATA!");
  Serial.println("📈 Equazione: pH = " + String(phModel.a, 4) + "×T + " + 
                 String(phModel.b, 6) + "×TDS + " + String(phModel.c, 2));
  Serial.println("📊 RMSE: " + String(rmse, 3));
  Serial.println("💡 Coefficienti piccoli sono normali con dati simili");
}

void estimatePH(float temp, float tdsVal) {
  // === SOLO MODELLO AI (rimossa formula base) ===
  if (phModel.trained && phModel.dataCount >= 3) {
    // Usa regressione AI
    estimatedPH = phModel.a * temp + phModel.b * tdsVal + phModel.c;
    estimatedPH = constrain(estimatedPH, 5.0, 9.0);
  } else {
    // Fallback se AI non allenata - pH neutro
    estimatedPH = 7.0;
  }
  // Calcola anche la stima tramite mediana locale
  estimatedPH_median = estimatePHMedian(temp, tdsVal);
}

// === STIMA PH MEDIANA LOCALE ===
float estimatePHMedian(float temp, float tdsVal) {
  // Trova i 3 dati di training più vicini (distanza euclidea su T/TDS)
  struct Neighbor {
    float dist;
    float ph;
    bool valid;
  } neighbors[MAX_TRAINING_DATA];
  int n = 0;
  for (int i = 0; i < phModel.dataCount; i++) {
    if (trainingData[i].valid) {
      float dT = temp - trainingData[i].temperature;
      float dTDS = tdsVal - trainingData[i].tds;
      float dist = sqrt(dT * dT + dTDS * dTDS);
      neighbors[n++] = {dist, trainingData[i].ph, true};
    }
  }
  if (n == 0) return 7.0;
  // Ordina per distanza crescente
  for (int i = 0; i < n-1; i++) {
    for (int j = i+1; j < n; j++) {
      if (neighbors[j].dist < neighbors[i].dist) {
        Neighbor tmp = neighbors[i];
        neighbors[i] = neighbors[j];
        neighbors[j] = tmp;
      }
    }
  }
  // Prendi i primi 3 (o meno se non disponibili)
  float phVals[3];
  int count = 0;
  for (int i = 0; i < n && count < 3; i++) {
    phVals[count++] = neighbors[i].ph;
  }
  // Calcola la mediana
  if (count == 1) return phVals[0];
  if (count == 2) return (phVals[0] + phVals[1]) / 2.0;
  // Ordina i 3 valori
  if (phVals[0] > phVals[1]) { float t = phVals[0]; phVals[0] = phVals[1]; phVals[1] = t; }
  if (phVals[1] > phVals[2]) { float t = phVals[1]; phVals[1] = phVals[2]; phVals[2] = t; }
  if (phVals[0] > phVals[1]) { float t = phVals[0]; phVals[0] = phVals[1]; phVals[1] = t; }
  return phVals[1];
}

// === NUOVE FUNZIONI PER GESTIONE TRAINING ===

bool deleteTrainingData(int id) {
  if (id < 0 || id >= phModel.dataCount || !trainingData[id].valid) {
    return false;
  }
  
  // Marca il campione come non valido
  trainingData[id].valid = false;
  
  // Ricompatta l'array rimuovendo gli elementi non validi
  int writeIndex = 0;
  for (int readIndex = 0; readIndex < phModel.dataCount; readIndex++) {
    if (trainingData[readIndex].valid) {
      if (writeIndex != readIndex) {
        trainingData[writeIndex] = trainingData[readIndex];
      }
      writeIndex++;
    }
  }
  
  // Aggiorna il contatore
  phModel.dataCount = writeIndex;
  
  // Marca il resto dell'array come non valido
  for (int i = writeIndex; i < MAX_TRAINING_DATA; i++) {
    trainingData[i].valid = false;
  }
  
  // Riallena il modello se ci sono ancora dati
  if (phModel.dataCount >= 3) {
    trainPHModel();
  } else {
    phModel.trained = false;
  }
  
  // Salva i cambiamenti
  savePHModel();
  
  Serial.println("🗑️ Campione eliminato - Dati rimanenti: " + String(phModel.dataCount));
  
  return true;
}

int importTrainingData(String data) {
  int imported = 0;
  int startIndex = 0;
  
  Serial.println("📥 Importazione dati training...");
  
  while (startIndex < data.length() && imported < (MAX_TRAINING_DATA - phModel.dataCount)) {
    int endIndex = data.indexOf('\n', startIndex);
    if (endIndex == -1) {
      endIndex = data.length();
    }
    
    String line = data.substring(startIndex, endIndex);
    line.trim();
    
    // Salta righe vuote o header
    if (line.length() > 0 && line.indexOf("temperature") == -1 && line.indexOf("temp") == -1) {
      // Parsing CSV: temp,tds,ph
      int comma1 = line.indexOf(',');
      int comma2 = line.indexOf(',', comma1 + 1);
      
      if (comma1 > 0 && comma2 > comma1) {
        String tempStr = line.substring(0, comma1);
        String tdsStr = line.substring(comma1 + 1, comma2);
        String phStr = line.substring(comma2 + 1);
        
        tempStr.trim();
        tdsStr.trim();
        phStr.trim();
        
        float temp = tempStr.toFloat();
        float tds = tdsStr.toFloat();
        float ph = phStr.toFloat();
        
        // Nessuna validazione dati
        addTrainingData(temp, tds, ph);
        imported++;
        
        Serial.print("✅ Importato: T=");
        Serial.print(temp, 1);
        Serial.print("°C, TDS=");
        Serial.print(tds, 1);
        Serial.print("ppm, pH=");
        Serial.println(ph, 2);
      } else {
        Serial.println("⚠️ Riga saltata (formato non valido): " + line);
      }
    }
    
    startIndex = endIndex + 1;
  }
  
  Serial.println("📥 Importazione completata: " + String(imported) + " campioni");
  return imported;
} 

// === FUNZIONI TELEGRAM ===

void checkWaterLevelNotification(bool previousState) {
  unsigned long currentTime = millis();
  
  // Debug del cambio di stato
  if (previousState != tdsValid) {
    Serial.println("🔄 Rilevato cambio stato TDS:");
    Serial.println("  Precedente: " + String(previousState ? "CONNESSO" : "SCOLLEGATO"));
    Serial.println("  Attuale: " + String(tdsValid ? "CONNESSO" : "SCOLLEGATO"));
    Serial.println("  Notifiche: " + String(notificationEnabled ? "ABILITATE" : "DISABILITATE"));
  }
  
  // Rileva quando il sensore passa da connesso a scollegato
  if (previousState && !tdsValid && notificationEnabled) {
    // Invia sempre per i cambi di stato reali (come le altre notifiche)
    String alertMessage = "<b>ALLARME PISCINA!</b>\n\n";
    alertMessage += "<b>LIVELLO ACQUA BASSO</b>\n";
    alertMessage += "Il sensore TDS si e scollegato, probabilmente perche il livello dell'acqua e sceso sotto la soglia minima.\n\n";
    
    alertMessage += "<b>Stato Sistema:</b>\n";
    alertMessage += "Temperatura: " + String(temperature > -100 ? String(temperature, 1) + "C" : "N/A") + "\n";
    alertMessage += "TDS: Sensore scollegato\n";
    alertMessage += "pH stimato: " + String(estimatedPH, 2);
    if (phModel.trained) {
      alertMessage += " (AI:" + String(phModel.dataCount) + " campioni)\n";
    } else {
      alertMessage += " (formula base)\n";
    }
    alertMessage += "WiFi: " + String(WiFi.RSSI()) + " dBm\n";
    alertMessage += "Orario: " + String(currentTime/1000) + "s da avvio\n\n";
    
    alertMessage += "<b>Azioni Consigliate:</b>\n";
    alertMessage += "1. Controlla il livello dell'acqua nella piscina\n";
    alertMessage += "2. Aggiungi acqua se necessario\n";
    alertMessage += "3. Verifica che il sensore TDS sia immerso\n";
    alertMessage += "4. Controlla eventuali perdite\n\n";
    
    alertMessage += "Il sistema continuera a monitorare la situazione.";
    
    sendTelegramMessage(alertMessage);
    lastNotificationTime = currentTime;
    
    Serial.println("🚨 ALLARME: Sensore TDS scollegato - Notifica Telegram inviata!");
  }
  
  // Rileva quando il sensore si riconnette
  if (!previousState && tdsValid && notificationEnabled) {
    // Invia notifica di riconnessione (senza timeout per buone notizie!)
    String recoveryMessage = "<b>Sensore TDS Riconnesso!</b>\n\n";
    recoveryMessage += "Il livello dell'acqua e tornato normale.\n";
    recoveryMessage += "Letture TDS ripristinate: " + String(tds, 1) + " ppm\n\n";
    
    recoveryMessage += "<b>Stato Attuale:</b>\n";
    recoveryMessage += "Temperatura: " + String(temperature, 1) + "C\n";
    recoveryMessage += "TDS: " + String(tds, 1) + " ppm\n";
    recoveryMessage += "pH stimato: " + String(estimatedPH, 2);
    if (phModel.trained) {
      recoveryMessage += " (AI:" + String(phModel.dataCount) + " campioni)\n";
    } else {
      recoveryMessage += " (formula base)\n";
    }
    recoveryMessage += "\nSistema completamente operativo!";
    
    sendTelegramMessage(recoveryMessage);
    
    Serial.println("✅ Sensore TDS riconnesso - Notifica Telegram inviata!");
  }
}

String urlEncode(String str) {
  String encoded = "";
  char c;
  char code0;
  char code1;
  for (int i = 0; i < str.length(); i++) {
    c = str.charAt(i);
    if (c == ' ') {
      encoded += "%20";
    } else if (isalnum(c)) {
      encoded += c;
    } else {
      code1 = (c & 0xf) + '0';
      if ((c & 0xf) > 9) {
        code1 = (c & 0xf) - 10 + 'A';
      }
      c = (c >> 4) & 0xf;
      code0 = c + '0';
      if (c > 9) {
        code0 = c - 10 + 'A';
      }
      encoded += '%';
      encoded += code0;
      encoded += code1;
    }
  }
  return encoded;
}

void sendTelegramMessage(String message) {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("❌ WiFi non connesso per Telegram");
    return;
  }

  Serial.println("📱 Invio notifica Telegram...");
  Serial.println("🔗 Connessione a api.telegram.org...");

  // Usa HTTPClient invece di WiFiClientSecure per maggiore stabilità
  HTTPClient https;
  https.begin("https://api.telegram.org/bot" + String(telegramToken) + "/sendMessage");
  https.addHeader("Content-Type", "application/x-www-form-urlencoded");

  // Prepara il messaggio codificato
  String postData = "chat_id=" + String(telegramChatID);
  postData += "&text=" + urlEncode(message);
  postData += "&parse_mode=HTML";

  Serial.println("📤 Invio dati...");
  int httpResponseCode = https.POST(postData);
  
  if (httpResponseCode > 0) {
    String response = https.getString();
    Serial.println("📬 Risposta HTTP: " + String(httpResponseCode));
    
    if (response.indexOf("\"ok\":true") > 0) {
      Serial.println("✅ Notifica Telegram inviata con successo!");
    } else {
      Serial.println("❌ Errore API Telegram:");
      Serial.println(response);
    }
  } else {
    Serial.println("❌ Errore connessione HTTP: " + String(httpResponseCode));
  }

  https.end();
} 