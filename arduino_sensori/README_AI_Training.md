# 🤖 Sistema AI Training - Pool Monitor

## Panoramica

Il sistema AI del pool monitor utilizza **machine learning semplice** per stimare il pH basandosi su temperatura e TDS. Più dati fornisci, più preciso diventa il modello.

## Come Accedere

1. **Pagina principale**: `http://192.168.178.115/`
2. **Training Manager**: `http://192.168.178.115/training`

## 📚 Aggiungere Dati di Training

### Metodo 1: Training Veloce (dalla homepage)
- Misura il pH reale con un tester
- Inserisci il valore nel campo "pH Reale"
- Clicca "Aggiungi" (usa automaticamente temperatura e TDS attuali)

### Metodo 2: Training Completo (da Training Manager)
- Vai su `/training`
- Compila manualmente:
  - **Temperatura**: 5-45°C
  - **TDS**: 0-2000 ppm  
  - **pH Reale**: 6.0-8.5 (misurato con tester)
- Clicca "Aggiungi Campione"
- Oppure usa "📊 Usa Attuali" per prendere temperatura e TDS dal sistema

## 🧠 Come Funziona l'AI

- **Campioni minimi**: 3 (per iniziare il training)
- **Campioni massimi**: 20 (poi sostituisce i più vecchi)
- **Formula**: `pH = a×Temperatura + b×TDS + c`
- **Coefficienti**: Calcolati automaticamente con regressione lineare

### Stato del Modello
- **❌ Base**: Usa formula fissa (non allenato)
- **🧠 Allenato**: Usa modello personalizzato basato sui tuoi dati

## 🛠️ Gestione Dati

### Visualizzazione
- Lista completa di tutti i campioni
- ID, Temperatura, TDS, pH per ogni campione
- Formula del modello attuale

### Cancellazione
- Clicca 🗑️ accanto al campione da eliminare
- Il modello viene riallenato automaticamente

### Import/Export

#### Esportazione
- Clicca "📤 Esporta CSV"
- Scarica file `training_data.csv`
- Formato: `temperature,tds,ph`

#### Importazione
- Formato supportato: una riga per campione
- Esempio:
  ```
  25.0,150,7.2
  26.5,160,7.1
  24.8,140,7.3
  ```
- Incolla nell'area di testo e clicca "📥 Importa Dati"

## 📊 API Endpoints

### Per sviluppatori o automazione

```bash
# Lista dati training
GET /api/training/list

# Aggiungi campione
POST /api/training/add
# Parametri: temp, tds, ph

# Elimina campione  
POST /api/training/delete
# Parametri: id

# Export CSV
GET /api/training/export

# Import dati
POST /api/training/import  
# Parametri: data (formato CSV)

# Reset completo
POST /api/reset_ai
```

## ⚡ Suggerimenti per un Buon Training

1. **Misure frequenti**: Aggiungi dati in condizioni diverse
2. **Range completo**: Testa con pH diversi (6.8-7.6)
3. **Stagioni diverse**: Temperatura influisce sul pH
4. **Dopo trattamenti**: Misura dopo aver aggiunto chimici
5. **Qualità dell'acqua**: Diversi livelli di TDS

## 🔧 Risoluzione Problemi

### Il modello non si allena
- Servono almeno 3 campioni validi
- Controlla che i dati siano nel range consentito

### Precisione bassa
- Aggiungi più campioni in condizioni diverse  
- Verifica calibrazione del tester pH
- Elimina campioni con errori di misura

### Reset necessario
- Clicca "🔄 Reset Modello" per ricominciare
- Elimina tutti i dati e torna al modello base

---

## 🏊 Integrazione con Sistema Pool

Il modello AI viene usato automaticamente per:
- **Stima pH**: Visualizzazione in tempo reale
- **Correzione automatica**: Calcolo dosaggio pompe
- **Monitoraggio**: Allarmi e notifiche

Il sistema è progettato per **apprendere le caratteristiche specifiche della tua piscina** e diventare sempre più preciso nel tempo! 