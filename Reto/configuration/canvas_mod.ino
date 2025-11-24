#include <WiFi.h>
#include <PubSubClient.h>

char ssid[] = "TC1004B-NET0";
char password[] = "TC1004B-NET0"; 
const char* mqtt_server = "192.168.137.1";

WiFiClient espClient;
PubSubClient client(espClient);

void setup_wifi() {
  delay(10);
  Serial.println();
  Serial.print("Conectando a ");
  Serial.println(ssid);
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\nWiFi conectado");
  Serial.println("IP asignada: ");
  Serial.println(WiFi.localIP());
}

void reconnect() {
  while (!client.connected()) {
    Serial.print("Conectando al broker MQTT...");
    if (client.connect("ESP32A")) {
      Serial.println("\nConectado");
    } else {
      Serial.print("Falla, rc=");
      Serial.print(client.state());
      Serial.println(" Reintentando en 5 segundos");
      delay(5000);
    }
  }
}

void setup() {
  Serial.begin(9600);
  setup_wifi();
  client.setServer(mqtt_server, 1883);
}

void loop() {
  if (!client.connected()) {
    reconnect();
  }
  client.loop();

  char sensor1[10] = "3.1416";
  char sensor2[10] = "2.71";
  
  client.publish("ESP32A/Sensor1", sensor1);
  client.publish("ESP32A/Sensor2", sensor2);

  Serial.println("Sensor 1: 3.1416");
  Serial.println("Sensor 2: 2.71");

  delay(5000);
}