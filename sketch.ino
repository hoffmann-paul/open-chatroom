#include <WiFi.h>
#include <WebServer.h>
#include <DNSServer.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>

LiquidCrystal_I2C lcd(0x26, 16, 2);

// WLAN
const char* ssid = "Open Chatroom";

// Server
WebServer server(80);
DNSServer dnsServer;

const byte DNS_PORT = 53;

// Nachrichten
struct Message {
  uint32_t id;
  String name;
  String text;
};

const int MAX_MESSAGES = 20;

Message messages[MAX_MESSAGES];

int messageCount = 0;
int messagePosition = 0;

uint32_t nextMessageId = 1;

// Webseite
const char webpage[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html lang="de">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>Open Chatroom</title>

<style>
* {
  box-sizing: border-box;
}

body {
  margin: 0;
  font-family: Arial, sans-serif;
  background: #f5f5f5;
  color: #222;
}

header {
  padding: 20px;
  background: white;
  border-bottom: 1px solid #ddd;
}

h2 {
  margin: 0;
  font-size: 20px;
}

#status {
  font-size: 12px;
  color: #777;
  margin-top: 6px;
}

main {
  max-width: 700px;
  margin: auto;
  padding: 15px;
}

#messages {
  height: 60vh;
  min-height: 250px;
  overflow-y: auto;
  background: white;
  border: 1px solid #ddd;
  padding: 12px;
}

.message {
  padding: 10px 0;
  border-bottom: 1px solid #eee;
  overflow-wrap: anywhere;
}

.name {
  font-weight: bold;
  font-size: 13px;
  margin-bottom: 5px;
}

.messageText {
  font-size: 15px;
}

form {
  display: flex;
  flex-direction: column;
  gap: 8px;
  margin-top: 12px;
}

input {
  width: 100%;
  padding: 13px;
  border: 1px solid #ccc;
  background: white;
  font-size: 15px;
  border-radius: 4px;
}

button {
  padding: 13px;
  border: none;
  background: #222;
  color: white;
  font-size: 15px;
  cursor: pointer;
  border-radius: 4px;
}

button:disabled {
  opacity: 0.5;
}

#hint {
  color: #777;
  font-size: 12px;
}
</style>
</head>

<body>

<header>
  <h2>Open Chatroom</h2>
  <div id="status">Verbinde mit dem Chatserver...</div>
</header>

<main>

  <div id="messages"></div>

  <form id="chatForm">

    <input
      id="username"
      maxlength="24"
      placeholder="Dein Name"
      required
    >

    <input
      id="message"
      maxlength="200"
      placeholder="Nachricht schreiben..."
      autocomplete="off"
      required
    >

    <button id="sendButton" type="submit">
      Nachricht senden
    </button>

  </form>

</main>

<script>

const messagesBox = document.getElementById("messages");
const usernameInput = document.getElementById("username");
const messageInput = document.getElementById("message");
const sendButton = document.getElementById("sendButton");
const statusText = document.getElementById("status");

let lastId = 0;
let sending = false;

usernameInput.value = localStorage.getItem("chatName") || "";

usernameInput.addEventListener("change", () => {
  localStorage.setItem("chatName", usernameInput.value);
});

function displayMessage(message) {

  const container = document.createElement("div");
  container.className = "message";

  const name = document.createElement("div");
  name.className = "name";
  name.textContent = message.name;

  const text = document.createElement("div");
  text.className = "messageText";
  text.textContent = message.text;

  container.appendChild(name);
  container.appendChild(text);

  messagesBox.appendChild(container);

  messagesBox.scrollTop = messagesBox.scrollHeight;

  lastId = Math.max(lastId, message.id);
}

async function updateMessages() {

  try {

    const response = await fetch(
      "/messages?since=" + lastId,
      { cache: "no-store" }
    );

    if (!response.ok) throw new Error();

    const data = await response.json();

    data.forEach(displayMessage);

    statusText.textContent = "Online";

  } catch (error) {

    statusText.textContent = "Verbindung unterbrochen";

  }
}

document.getElementById("chatForm")
.addEventListener("submit", async (event) => {

  event.preventDefault();

  if (sending) return;

  const name = usernameInput.value.trim();
  const message = messageInput.value.trim();

  if (!name || !message) return;

  sending = true;
  sendButton.disabled = true;

  try {

    const body = new URLSearchParams();

    body.append("name", name);
    body.append("message", message);

    const response = await fetch("/send", {
      method: "POST",
      headers: {
        "Content-Type": "application/x-www-form-urlencoded"
      },
      body: body.toString()
    });

    if (!response.ok) throw new Error();

    localStorage.setItem("chatName", name);

    messageInput.value = "";

    await updateMessages();

    messageInput.focus();

  } catch (error) {

    statusText.textContent = "Nachricht konnte nicht gesendet werden";

  }

  sending = false;
  sendButton.disabled = false;

});

updateMessages();
setInterval(updateMessages, 800);

</script>

</body>
</html>
)rawliteral";

// JSON-Escaping
String jsonEscape(const String &input) {

  String output;

  for (unsigned int i = 0; i < input.length(); i++) {

    char c = input[i];

    if (c == '"') output += "\\\"";
    else if (c == '\\') output += "\\\\";
    else if (c == '\n') output += "\\n";
    else if (c == '\r') output += "\\r";
    else if (c == '\t') output += "\\t";
    else if ((unsigned char)c < 32) continue;
    else output += c;

  }

  return output;
}

// Chat anzeigen
void handleRoot() {

  server.send(
    200,
    "text/html; charset=utf-8",
    webpage
  );

}

// Nachrichten senden
void handleSend() {

  String name = server.arg("name");
  String message = server.arg("message");

  name.trim();
  message.trim();

  if (name.length() == 0 || message.length() == 0) {
    server.send(400, "text/plain", "Leere Nachricht");
    return;
  }

  if (name.length() > 24 || message.length() > 200) {
    server.send(400, "text/plain", "Zu lang");
    return;
  }

  messages[messagePosition].id = nextMessageId++;
  messages[messagePosition].name = name;
  messages[messagePosition].text = message;

  messagePosition = (messagePosition + 1) % MAX_MESSAGES;

  if (messageCount < MAX_MESSAGES) {
    messageCount++;
  }

  digitalWrite(2, HIGH);
  lcd.clear();

  lcd.setCursor(0, 0);
  lcd.print(name + ":");

  lcd.setCursor(0, 1);
  lcd.print(message);

  delay(500);
  digitalWrite(2, LOW);

  Serial.println(name + ": " + message);

  server.send(200, "text/plain", "OK");

}

// Nachrichten abrufen
void handleMessages() {

  uint32_t since = server.arg("since").toInt();

  String json = "[";

  bool first = true;

  int start = (messagePosition - messageCount + MAX_MESSAGES)
              % MAX_MESSAGES;

  for (int i = 0; i < messageCount; i++) {

    int index = (start + i) % MAX_MESSAGES;

    if (messages[index].id <= since) continue;

    if (!first) json += ",";

    first = false;

    json += "{\"id\":";
    json += String(messages[index].id);

    json += ",\"name\":\"";
    json += jsonEscape(messages[index].name);

    json += "\",\"text\":\"";
    json += jsonEscape(messages[index].text);

    json += "\"}";

  }

  json += "]";

  server.send(200, "application/json; charset=utf-8", json);

}

// Captive Portal: unbekannte URLs auf den Chat umleiten
void handleNotFound() {

  server.sendHeader("Location", "http://192.168.4.1/", true);
  server.send(302, "text/plain", "");

}

void setup() {

  Serial.begin(115200);
  pinMode(2, OUTPUT);

  Wire.begin(21, 22);

	lcd.init();                      
  lcd.backlight();
  lcd.setCursor(0, 0); 

  Serial.println();

  WiFi.mode(WIFI_AP);

  // Offenes WLAN ohne Passwort
  WiFi.softAP(ssid);

  IPAddress ip = WiFi.softAPIP();

  Serial.println("WLAN gestartet!");
  Serial.print("WLAN: ");
  Serial.println(ssid);

  Serial.print("IP: ");
  Serial.println(ip);

  // DNS: Alle Namen auf den ESP32 umleiten
  dnsServer.start(
    DNS_PORT,
    "*",
    ip
  );

  // Webseiten-Routen
  server.on("/", HTTP_GET, handleRoot);
  server.on("/send", HTTP_POST, handleSend);
  server.on("/messages", HTTP_GET, handleMessages);

  // Apple / Android / Windows Captive-Portal-Prüfungen
  server.on("/generate_204", HTTP_GET, handleRoot);
  server.on("/gen_204", HTTP_GET, handleRoot);
  server.on("/hotspot-detect.html", HTTP_GET, handleRoot);
  server.on("/library/test/success.html", HTTP_GET, handleRoot);
  server.on("/connecttest.txt", HTTP_GET, handleRoot);
  server.on("/ncsi.txt", HTTP_GET, handleRoot);
  server.on("/success.txt", HTTP_GET, handleRoot);

  server.onNotFound(handleNotFound);

  server.begin();

  Serial.println("Chatserver bereit!");

}

void loop() {

  dnsServer.processNextRequest();
  server.handleClient();

}
