#include <WiFi.h>                       // Wi-Fi
#include <PubSubClient.h>               // MQTT
#include <LiquidCrystal_I2C.h>          // LCD I2C
#include "DHT.h"                        // Sensor DHT

LiquidCrystal_I2C lcd(0x27, 16, 2);    // Inicializa LCD

void LCDset(String Str, int posX, int posY) { // Configura texto no LCD
    lcd.setCursor(posX, posY);
    lcd.print(Str);
}

// Tópicos MQTT
const char* TOPICPREFIX           = "lampbit"; 
const char* TOPICO_SUBSCRIBE      = "/TEF/lampbit/cmd"; 
const char* TOPICO_PUBLISH_1      = "/TEF/lampbit/attrs"; 
const char* PUBLISH_ATTACK        = "/TEF/lampbit/attrs/a"; 
const char* PUBLISH_EFFICIENCY    = "/TEF/lampbit/attrs/e"; 

// Configurações
const bool  Simulation  = true;      
const char* SSID        = Simulation ? "Wokwi-GUEST" : "FIAP-IBM"; 
const char* PASSWORD    = Simulation ? ""            : "Challenge@24!"; 
const char* BROKER_MQTT = "18.191.164.252"; 
const int   BROKER_PORT = 1883; 
const char* ID_MQTT     = "fiware_bit"; 
const int   LED_PIN     = 2; 
const int   BUTTON_PIN   = 18; 

DHT dht(32, Simulation ? DHT22 : DHT11); // Inicializa DHT
WiFiClient espClient;  
PubSubClient MQTT(espClient); // Cliente MQTT

char estadoSaida = '0'; // Estado do LED
int atkRTC  = 0; // Temporizador
String displayCycles[] = {"bateria", "attack_mode"}; // Modos
int displayNow = 0; // Ciclo atual

void setup() {
    Serial.begin(115200); // Serial
    pinMode(LED_PIN, OUTPUT); // LED como saída
    pinMode(BUTTON_PIN, INPUT); // Botão como entrada
    digitalWrite(LED_PIN, LOW); // Desliga LED

    // Conexão Wi-Fi
    WiFi.begin(SSID, PASSWORD); 
    dht.begin(); // Inicializa DHT
    lcd.init(); // Inicializa LCD
    lcd.backlight(); // Luz de fundo

    // Mensagens LCD
    LCDset("| WiFi |", 4, 0);
    LCDset("Aguardando", 3, 1);
    while (WiFi.status() != WL_CONNECTED) { // Aguarda conexão
        delay(100);
        Serial.print("."); // Progresso
    }
    LCDset("Conectado ", 3, 1);
    delay(500);
    
    Serial.println("\nConectado à rede WiFi"); // Confirma conexão
    Serial.print("IP: "); 
    Serial.println(WiFi.localIP()); // IP

    // Configuração MQTT
    MQTT.setServer(BROKER_MQTT, BROKER_PORT); 
    MQTT.setCallback(mqtt_callback); // Callback

    // Mensagens LCD
    LCDset("| MQTT |", 4, 0);
    LCDset("Aguardando", 3, 1);
    while (!MQTT.connected()) { // Aguarda conexão
        if (MQTT.connect(ID_MQTT)) { 
            MQTT.subscribe(TOPICO_SUBSCRIBE); // Inscrição
            MQTT.publish(TOPICO_PUBLISH_1, "s|on"); // Publica
        } else {
            Serial.println("Falha ao conectar, tentando em 2s");
            delay(2000); // Espera
        }
    }
    LCDset("Conectado ", 3, 1);
    delay(500);
    lcd.clear(); // Limpa LCD
}

void loop() {
    if (!MQTT.connected()) { // Verifica conexão
        lcd.clear();
        LCDset("| MQTT |", 4, 0);
        LCDset("Aguardando", 3, 1);
        while (!MQTT.connected()) {
            if (MQTT.connect(ID_MQTT)) {
                MQTT.subscribe(TOPICO_SUBSCRIBE); // Inscreve
            } else {
                Serial.println("Falha, tentando em 2s");
                delay(2000); // Espera
            }
        }
    }

    if (!MQTT.connected()) { // Verifica Wi-Fi
        lcd.clear();
        LCDset("| WiFi |", 4, 0);
        LCDset("Aguardando", 3, 1);
        while (WiFi.status() != WL_CONNECTED) {
            Serial.println("Rede Desconectada... Aguardando 2s");
            delay(2000); // Espera
        }
        LCDset("Conectado ", 3, 1);
    }
    mainHandler(); // Chama a função principal
    delay(100); // Espera
}

float temp = 0; // Temperatura
const int deltaTemp = 5; // Delta
const int minTemp = 20; // Mínima
const int maxTemp = 25; // Máxima
int currCycle = 0; // Ciclo atual
int lastMQTTCall = 0; // Última chamada MQTT

void mainHandler() {
    String displayOld = displayCycles[currCycle]; // Ciclo anterior
    if (digitalRead(BUTTON_PIN) == HIGH) { // Botão
        currCycle++;
        if (currCycle >= sizeof(displayCycles) / sizeof(displayCycles[0])) {
            currCycle = 0; // Reinicia ciclo
        }
        delay(500); // Espera
    }

    String displayNow = displayCycles[currCycle]; // Ciclo atual
    if (displayOld != displayNow) {
        lcd.clear(); // Limpa se mudou
    }

    bool lower = false; // Flag para temperatura
    int tempStatus = 1; // Status
    int efficiency = 100; // Eficiência
    temp = dht.readTemperature(); // Lê temperatura

    // Verifica a temperatura
    if (temp <= (minTemp - deltaTemp) || temp >= (maxTemp + deltaTemp)) {
        lower = (temp <= minTemp - deltaTemp);
        tempStatus = 3; // Muito fora
    } else if (temp <= minTemp || temp >= maxTemp) {
        lower = (temp <= minTemp);
        tempStatus = 2; // Fora
    }

    // Atualiza LCD
    if (tempStatus == 1 && displayNow == "bateria") {
        LCDset("Temp: ADEQUADA", 0, 0);
    } else if (tempStatus == 2) {
        if (displayNow == "bateria") {
            LCDset(lower ? "Temp: BAIXA    " : "Temp: ALTA    ", 0, 0);
        }
        efficiency -= lower ? (minTemp - temp) : (temp - maxTemp); // Ajusta eficiência
    } else if (tempStatus == 3) {
        if (displayNow == "bateria") {
            LCDset(lower ? "Temp: MT BAIXA" : "Temp: MT ALTA", 0, 0);
        }
        efficiency -= lower ? (minTemp - temp) : (temp - maxTemp); // Ajusta eficiência
    }

    if (displayNow == "bateria") {
        LCDset("Eficiencia: " + String(efficiency) + "%  ", 0, 1); // Exibe eficiência
    }

    // Modo de ataque
    String atkStatus = ATKread();
    if (displayNow == "attack_mode") {
        if (atkRTC == 0) {
            LCDset("Attack Mode: OFF", 0, 0);
            if (atkStatus == "Esperando") {
                LCDset("Esperando sensor", 0, 1);
            } else if (atkStatus == "Ativo") {
                LCDset("Sensor Ativo    ", 0, 1);
            }
        } else {
            LCDset("Attack Mode: ON ", 0, 0);
            LCDset("Tempo: " + atkStatus + "       ", 0, 1);
        }
    }

    // Atualiza MQTT
    if (millis() - lastMQTTCall > 1000) {
        lastMQTTCall = millis();
        MQTT.loop(); // Processa mensagens
        MQTT.publish(PUBLISH_EFFICIENCY, String(efficiency).c_str()); // Publica eficiência
        MQTT.publish(PUBLISH_ATTACK, atkStatus.c_str()); // Publica status
        enviarEstadoOutputMQTT(); // Envia estado LED
        Serial.println("MQTT Atualizado!"); // Confirma
        Serial.println("Attack Mode: " + atkStatus);
        Serial.println("Efficiência: " + String(efficiency));
    }
}

String ATKread() {
    const float amb = map(analogRead(34), 0, 4095, 0, 100); // Lê ambiente
    static unsigned long lastPeak = millis(); // Último pico
    String Status;

    if (atkRTC == 0) {
        if (amb > 15) {
            lastPeak = millis();
            Status = "Esperando"; // Esperando sensor
        } else {
            Status = "Ativo"; // Sensor ativo
        }

        if (millis() - lastPeak > 5000) {
            atkRTC = millis() + 30000; // Define temporizador
        }
    } else {
        if (millis() >= atkRTC) {
            Status = "0:00"; // Tempo esgotado
            atkRTC = 0; // Reseta
        } else {
            unsigned long remainingSeconds = (atkRTC - millis()) / 1000; // Tempo restante
            int minutes = remainingSeconds / 60;
            int seconds = remainingSeconds % 60;
            Status = String(minutes) + ":" + (seconds < 10 ? "0" : "") + String(seconds); // Formata
        }
    }

    return Status; // Retorna status
}

void mqtt_callback(char* topic, byte* payload, unsigned int length) {
    String msg;
    for (unsigned int i = 0; i < length; i++) {
        msg += (char)payload[i]; // Constrói mensagem
    }

    // Comandos MQTT
    if (msg == String(TOPICPREFIX) + "@on|") {
        digitalWrite(LED_PIN, HIGH); // Liga LED
        estadoSaida = '1'; // Estado
    } else if (msg == String(TOPICPREFIX) + "@off|") {
        digitalWrite(LED_PIN, LOW); // Desliga LED
        estadoSaida = '0'; // Estado
    }
}

void enviarEstadoOutputMQTT() {
    String status = (estadoSaida == '1') ? "s|on" : "s|off"; // Estado LED
    MQTT.publish(TOPICO_PUBLISH_1, status.c_str()); // Publica estado
}
