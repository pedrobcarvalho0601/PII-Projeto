#include "web_server.h"
#include "ina226.h"
#include "segredos.h"
#include "comms.h"
#include <WiFi.h>
#include <WebServer.h>
#include <LittleFS.h>
#include <esp_wifi.h>

WebServer server(80);

const char* ssid = WIFI_SSID;
const char* password = WIFI_PASS;

// Função de segurança para evitar erro NaN no JSON
String formataValor(float valor) {
  if (isnan(valor) || isinf(valor)) {
    return "0.00";
  }
  return String(valor);
}

// Função para iniciar o servidor web e configurar as rotas
void iniciarWebServer() { 
  if (!LittleFS.begin(true)) {
    Serial.println("ERRO: Falha ao montar o LittleFS!");
    return;
  }

  WiFi.mode(WIFI_AP_STA); 
  esp_wifi_set_ps(WIFI_PS_NONE);
  
  if (String(password).length() > 0) {
    WiFi.softAP(ssid, password, 1); 
  } else {
    WiFi.softAP(ssid, NULL, 1); 
  }

  IPAddress IP = WiFi.softAPIP();
  Serial.println("------------------------------------");
  Serial.print("Rede criada: "); Serial.println(ssid);
  Serial.print("Endereco do Painel: http://"); Serial.println(IP);
  Serial.println("------------------------------------");
  Serial.print("Endereco MAC do ESP32: "); Serial.println(WiFi.macAddress());
  Serial.print("Endereco MAC do Access Point: "); Serial.println(WiFi.softAPmacAddress());

  // Rotas específicas de API (dados JSON)
  server.on("/dados", []() {
    DadosEnergia dados_master = lerDadosINA226();
    struct_message mini1 = getDadosMini1();
    struct_message mini2 = getDadosMini2();
    extern String getEstadoSistema();
    extern int getDutyDissipacao(); // Importado do main.cpp
    
    String json = "{";
    json += "\"v_master\":" + formataValor(dados_master.tensao_V) + ",";
    json += "\"c_master\":" + formataValor(dados_master.corrente_mA) + ",";
    json += "\"p_master\":" + formataValor(dados_master.potencia_mW) + ",";
    json += "\"v_mini1\":" + formataValor(mini1.tensao_V) + ",";
    json += "\"c_mini1\":" + formataValor(mini1.corrente_mA) + ",";
    json += "\"p_mini1\":" + formataValor(mini1.potencia_mW) + ",";
    json += "\"v_mini2\":" + formataValor(mini2.tensao_V) + ",";
    json += "\"c_mini2\":" + formataValor(mini2.corrente_mA) + ",";
    json += "\"p_mini2\":" + formataValor(mini2.potencia_mW) + ",";
    json += "\"dissipacao\":" + String(getDutyDissipacao()) + ",";
    json += "\"estado\":\"" + getEstadoSistema() + "\"";
    json += "}";
    
    server.send(200, "application/json", json);
  });

  // Rota explícita para o index.html
  server.on("/", HTTP_GET, []() {
    File file = LittleFS.open("/index.html", "r");
    if (!file) {
      server.send(404, "text/plain", "ERRO: index.html inacessivel!");
      return;
    }
    server.streamFile(file, "text/html");
    file.close();
  });
  
  // Rota genérica para servir CSS, JS e imagens
  server.serveStatic("/", LittleFS, "/");

  // Servidor não encontrado (Erro 404)
  server.onNotFound([]() {
    server.send(404, "text/plain", "ERRO: Ficheiro ou rota nao encontrada!");
  });

  server.begin();
}

// Função para processar as requisições do servidor web (chamada no loop principal)
void processarWebServer() {
  server.handleClient(); 
}