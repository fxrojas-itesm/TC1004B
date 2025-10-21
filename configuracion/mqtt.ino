#include <WiFi.h>
#include <PubSubClient.h>

#define MQTT_TRANSMIT_RATE_MS 2000 // Se define el periodo de publicación de mensajes de MQTT

// **********************************************
// Modifica los siguientes rubros con los valores adecuados
// para establecer la comunicación con el servidor MQTT.
// **********************************************
const char* SSID     = "MG50";     // Se define el nombre de la red a la cual se conectará el módulo ESP32
const char* PASSWORD = "MG50@EIOu"; // Se define la contraseña de la red

const char* MQTT_SERVER    = "192.168.195.153"; // Es la dirección IP del equipo donde corre el servidor MQTT.
const char* CLIENT_ID      = "ESP32-A";         // Es un identificador único del cliente conectado.
const int MQTT_SERVER_PORT = 1883;              // Es el puerto de comunicación (puerto de red) asignado al servidor MQTT. 

const char* topic = "switch0"; // Es el nombre del tópico al cual se publicarán los mensajes.

// **********************************************

WiFiClient   espClient;             // Permite crear una conexión a un servidor con base en su dirección IP y puerto de comunicación.
PubSubClient mqttClient(espClient); // Permite configurar un cliente para envío y recepción de mensajes MQTT.

float msg = 0.0; // Variable a publicar

void setup() 
{
  delay(5000);
  Serial.begin(9600);
  setup_wifi();                                        // Función usada para configurar la conexión WiFi
  mqttClient.setServer(MQTT_SERVER, MQTT_SERVER_PORT); // Función usada para configurar la conexión al servidor MQTT.
}

void loop() 
{
  // Si el cliente MQTT pierde la conexión con el servidor, se intenta reconectar.
  if (!mqttClient.connected()) 
  {
    reconnect(); // Función usada para reconectar el cliente MQTT.
  }
  
  // La función loop() se llama con regularidad para permitir la llegada de mensajes y mantener la conexión con el servidor MQTT.
  // Si se ha perdido la conexión con el servidor, se intenta establecer nuevamente la misma.
  if(!mqttClient.loop())
  {
    mqttClient.connect(CLIENT_ID);
  }

  // **********************************************
  // El siguiente código, permite modificar el valor del mensaje a publicar.
  // El mismo, será enviado con la función publish() la cual recibe como entradas:
  //  a) El tópico al cual se publicará el mensaje.
  //  b) El payload (mensaje a enviar) como una cadena de carácteres.
  // NOTA: La función sprintf() es usada para obtener el payload (mensaje a enviar) en el formato correcto.
  // **********************************************
  msg = msg + 0.5;
  if (msg > 20) msg = 0.0;
  
  char payload[10];
  sprintf(payload, "%.2f", msg);
  
  mqttClient.publish(topic, payload);
  
  Serial.print("Payload: ");
  Serial.print(payload);
  Serial.print(", published to: ");
  Serial.print(topic);
  Serial.println(" MQTT topic.");
  // **********************************************
  
  delay(MQTT_TRANSMIT_RATE_MS); // Se detiene el código por un lapso de tiempo definido (MQTT_TRANSMIT_RATE_MS)
}

// Función usada para configurar la conexión WiFi
// 1. Se inicia la conexión con base en el SSID y Password declarados.
// 2. Mientras la conexión no se haya establecido, se imprime un mensaje de espera ".".
// 3. Una vez establecida la conexión, se imprime en el monitor Serial la dirección IP asiganada al módulo NodeMCU.
void setup_wifi() 
{
  delay(10);
  
  Serial.println();
  Serial.print("Connecting to ");
  Serial.println(SSID);
  
  WiFi.begin(SSID, PASSWORD);
  while (WiFi.status() != WL_CONNECTED) 
  {
    delay(500);
    Serial.print(".");
  }
  
  Serial.println("");
  Serial.print("WiFi connected - ESP IP address: ");
  Serial.println(WiFi.localIP());
}

// Función usada para reconectar el cliente MQTT.
// 1. Mientras la conexión no se haya establecido, se vuelve a intentar la conexión hasta tener exito.
// 2. Si la conexión es exitosa, se imprime un mensaje de confirmación en el monitor Serial "connected".
// 3. Si la conexión falla, se vuelte a intentar la reconexión pasados 5 segundos.
void reconnect()
{
  while (!mqttClient.connected())
  {
    Serial.print("Attempting MQTT connection...");
    if (mqttClient.connect(CLIENT_ID))
    {
      Serial.println("connected");
    }
    else
    {
      Serial.print("MQTT connection failed, rc=");
      Serial.print(mqttClient.state());
      Serial.println(" try again in 5 seconds");
      delay(5000);
    }
  }
}
