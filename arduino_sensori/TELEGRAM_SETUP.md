# 📱 Configurazione Notifiche Telegram - Pool Monitor

## 🎯 Panoramica
Ricevi notifiche istantanee su Telegram quando il livello dell'acqua nella piscina scende troppo (sensore TDS scollegato).

## 🚀 Setup Completo (5 minuti)

### Passo 1: Crea il Bot Telegram
1. Apri Telegram e cerca **@BotFather**
2. Scrivi `/start` e poi `/newbot`
3. Scegli un nome per il bot (es. "Pool Monitor Bot")
4. Scegli un username (es. "mio_pool_monitor_bot")
5. **Copia il TOKEN** che ricevi (formato: `123456789:AAAA...`)

### Passo 2: Ottieni il Chat ID
1. Scrivi un messaggio al tuo bot (es. "Ciao")
2. Apri questo link nel browser (sostituisci `<TOKEN>` con il tuo token):
   ```
   https://api.telegram.org/bot<TOKEN>/getUpdates
   ```
3. Cerca `"chat":{"id":` e **copia il numero** (es. `123456789`)

### Passo 3: Configura il Codice
1. Apri il file `arduino_sensori.ino`
2. Trova queste righe (circa linea 15):
   ```cpp
   const char* telegramToken = "YOUR_BOT_TOKEN_HERE";
   const char* telegramChatID = "YOUR_CHAT_ID_HERE";
   ```
3. Sostituisci con i tuoi valori:
   ```cpp
   const char* telegramToken = "123456789:AAAA...";  // Il tuo token
   const char* telegramChatID = "123456789";         // Il tuo chat ID
   ```

### Passo 4: Carica e Testa
1. **Carica il codice** sull'ESP32
2. Vai su `http://192.168.178.115/`
3. Nella sezione "📱 Notifiche Telegram" clicca **"📱 Test Notifica"**
4. Dovresti ricevere un messaggio di test!

## 💧 Come Funziona

### Rilevamento Livello Acqua
- **Sensore connesso**: TDS legge valori normali
- **Sensore scollegato**: Acqua scesa sotto il sensore → ALLARME!

### Notifiche Automatiche
- **🚨 Allarme**: Quando il sensore si scollega
- **✅ Recupero**: Quando il sensore si riconnette
- **⏱️ Anti-spam**: Massimo 1 allarme ogni 5 minuti

### Esempio Notifica di Allarme
```
🚨 ALLARME PISCINA!

💧 LIVELLO ACQUA BASSO
Il sensore TDS si è scollegato, probabilmente perché 
il livello dell'acqua è sceso sotto la soglia minima.

📊 Stato Sistema:
🌡️ Temperatura: 25.3°C
💧 TDS: Sensore scollegato
🧪 pH stimato: 7.20 (basato su formula base)
📡 WiFi: -45 dBm
⏰ Orario: 12345s da avvio

🔧 Azioni Consigliate:
1. Controlla il livello dell'acqua nella piscina
2. Aggiungi acqua se necessario  
3. Verifica che il sensore TDS sia immerso
4. Controlla eventuali perdite

🏊 Il sistema continuerà a monitorare la situazione.
```

## ⚙️ Gestione dalla Web Interface

### Controlli Disponibili
- **📱 Test Notifica**: Invia messaggio di prova
- **🔔/🔕 Abilita/Disabilita**: Attiva/disattiva notifiche
- **Stato**: Visualizza configurazione e ultimo invio

### API Endpoints
```bash
# Test notifica
POST /api/telegram/test

# Abilita/disabilita notifiche  
POST /api/telegram/toggle

# Stato notifiche
GET /api/telegram/status
```

## 🔧 Configurazioni Avanzate

### Personalizza Intervalli (nel codice)
```cpp
#define NOTIFICATION_INTERVAL 300000  // 5 minuti tra notifiche duplicate
#define SENSOR_TIMEOUT 30000          // 30 secondi prima di considerare sensore scollegato
```

### Disabilita Temporaneamente
- Dalla web interface: clicca "🔕 Disabilita"
- Nel codice: `bool notificationEnabled = false;`

## 🛠️ Risoluzione Problemi

### "❌ Telegram NON configurato"
- Verifica che token e chat ID siano inseriti correttamente
- Controlla che non ci siano spazi extra
- Riavvia ESP32 dopo modifiche

### "Errore nell'invio della notifica"
- Controlla connessione WiFi
- Verifica che il bot token sia valido
- Assicurati che il chat ID sia corretto

### "Test notifica non arriva"
- Controlla che hai scritto al bot almeno una volta
- Verifica il chat ID con `/getUpdates`
- Controlla il log seriale per errori

### Debug via Serial Monitor
```
📱 Telegram configurato e testato      // OK
⚠️ Telegram NON configurato           // Mancano token/chat ID
📱 Notifica Telegram inviata!         // Invio riuscito
❌ Errore nell'invio della notifica    // Problema di comunicazione
```

## 🎉 Vantaggi

- **✅ Immediato**: Notifica in tempo reale
- **🔄 Automatico**: Nessun intervento manuale
- **📊 Dettagliato**: Stato completo del sistema
- **🛡️ Affidabile**: Anti-spam integrato
- **📱 Ovunque**: Ricevi notifiche ovunque tu sia

---

## 🆘 Supporto

Se hai problemi:
1. Controlla il Serial Monitor per errori
2. Verifica la connessione WiFi
3. Testa con `/api/telegram/test`
4. Ricrea il bot se necessario

**La configurazione richiede solo 5 minuti e ti darà tranquillità totale! 🏊‍♂️** 