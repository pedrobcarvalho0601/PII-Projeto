#include <Arduino.h>
#include "web_server.h"
#include "comms.h"
#include "config.h"
#include "telemetry.h"
#include "hardware.h"
#include "sg90.h"
#include "ibt2.h" 
#include "protocol.h" 

// ==============================================================================
// CONFIGURAÇÕES DA REDE ELÉTRICA
// ==============================================================================

const float PRODUCAO_FONTE_mW = 12000.0; // 12V * 1A = 12W

// LIMITES DE CONSUMO (MILIWATTS)
const float LIMITE_CARGA_mW = 1000.0; 
const float LIMITE_ESTAVEL_mW = 1200.0; 

enum EstadoRede { REDE_LEVE, REDE_ESTAVEL, REDE_SOBRECARGA };
EstadoRede estadoAtual = REDE_LEVE;
const char* nomesEstados[] = {"LEVE (<1000mW)", "ESTAVEL (1000-1200mW)", "PICO (>1200mW)"};

// Variáveis do Ciclo de Dissipação/Descida
unsigned long tempoInicioPico = 0;
bool emCicloDePico = false;

// Variáveis globais partilhadas com o WebServer
int dutyDissipacaoGlobal = 0;

int getDutyDissipacao() {
  return dutyDissipacaoGlobal;
}

// ==============================================================================
// VARIÁVEIS DA MECÂNICA DE TRAVA
// ==============================================================================
const int ANGULO_TRAVADO = 150; // NOVO: Base nos 30º para poder soltar para trás
const int ANGULO_SOLTO = 180;    // NOVO: Solta no sentido inverso (para os 0º)
const unsigned long TEMPO_ALIVIO_TRAVA_MS = 1500; // 1.5 segundos a subir para soltar a trava

bool quedaConcluida = false; // Proteção para não ficar a ler o ultrassom em loop
int ultimoAnguloServo = -1;  // Memória do servo (Anti-Spam)

String getEstadoSistema() {
  return String(nomesEstados[estadoAtual]);
}

void setup() {
  Serial.begin(SERIAL_BAUD_RATE);
  while (!Serial) { delay(10); } 
  
  delay(DELAY_ARRANQUE_MS);

  iniciarWebServer(); 
  initComms(); 
  iniciarHardware(); 
  iniciarDissipacao();
  iniciarSG90();

  // Garante que arranca travado fisicamente
  moverSG90(ANGULO_TRAVADO); 
  ultimoAnguloServo = ANGULO_TRAVADO;

  Serial.println("\n[SISTEMA] Smart Grid Online! Ciclo de Descida e Dissipacao Avancados Ativos.\n");
}

void loop() {
  processarWebServer(); 
  imprimirTelemetria(); 

  unsigned long tempoAtual = millis();

  // 1. LEITURA DOS SENSORES
  struct_message m1 = getDadosMini1(); 
  struct_message m3 = getDadosMini3(); 

  float consumo_cidade_mW = m1.potencia_mW;

  bool maxCima = (m3.distancia_cima_cm > 0.0 && m3.distancia_cima_cm <= 20.0);
  bool maxBaixo = (m3.distancia_baixo_cm > 0.0 && m3.distancia_baixo_cm <= 20.0);

  // 2. IDENTIFICAR O ESTADO DA REDE
  EstadoRede novoEstado;
  if (consumo_cidade_mW < LIMITE_CARGA_mW) novoEstado = REDE_LEVE;
  else if (consumo_cidade_mW <= LIMITE_ESTAVEL_mW) novoEstado = REDE_ESTAVEL;
  else novoEstado = REDE_SOBRECARGA;

  if (novoEstado != estadoAtual) {
    Serial.printf("\n >>> MUDANÇA DE ESTADO: %s (%.0fmW) <<<\n", nomesEstados[novoEstado], consumo_cidade_mW);
    estadoAtual = novoEstado;
  }

  // 3. LÓGICA DE CONTROLO
  int dutyMotor = 0;
  int dutyDissipacao = 0;
  bool motorSobe = true;
  bool abrirTrava = false; 

  switch (estadoAtual) {
    case REDE_LEVE:
      emCicloDePico = false;
      quedaConcluida = false; 
      
      motorSobe = true;
      abrirTrava = false; 
      dutyDissipacao = 0;
      
      // Motor sobe mais rápido agora (Mapeado de 100% até 50% no mínimo)
      dutyMotor = map(consumo_cidade_mW, 0, LIMITE_CARGA_mW, 100, 50);
      if (maxCima) dutyMotor = 0;
      break;

    case REDE_ESTAVEL:
      emCicloDePico = false;
      quedaConcluida = false;
      
      motorSobe = false;
      dutyMotor = 0;
      dutyDissipacao = 0;
      abrirTrava = false; 
      break;

    case REDE_SOBRECARGA:
      if (!emCicloDePico) {
        emCicloDePico = true;
        quedaConcluida = false; 
        tempoInicioPico = tempoAtual;
        Serial.println("!!! INICIO DO PICO: A DESTRAVAR !!!");
      }

      unsigned long tempoDecorrido = tempoAtual - tempoInicioPico;

      // FASE 1: Destravar (Primeiros 1.5s do pico)
      if (tempoDecorrido <= TEMPO_ALIVIO_TRAVA_MS) {
        motorSobe = true;    
        dutyMotor = 40;      
        abrirTrava = true;   
        dutyDissipacao = 0;
      } 
      // FASE 2: O Ciclo de Dissipação e Descida (Após Destravar)
      else {
        abrirTrava = true; // Mantém a trava aberta
        unsigned long tempoCiclo = tempoDecorrido - TEMPO_ALIVIO_TRAVA_MS;

        // Se o ciclo todo passou (30s + 45s + 30s = 105.000 ms), recomeça
        if (tempoCiclo > 105000) {
          tempoInicioPico = tempoAtual;
          quedaConcluida = false; // Permite uma nova descida no novo ciclo
          Serial.println("!!! REINICIO DO CICLO DE PICO !!!");
        }

        // Curva da Dissipação
        if (tempoCiclo <= 30000) {
          // Primeiros 30s: Sobe 0 a 100%
          dutyDissipacao = map(tempoCiclo, 0, 30000, 0, 100);
        } 
        else if (tempoCiclo <= 75000) {
          // Seguintes 45s: Mantém nos 100%
          dutyDissipacao = 100;
        } 
        else {
          // Últimos 30s: Desce 100 a 0%
          dutyDissipacao = map(tempoCiclo, 75000, 105000, 100, 0);
        }

        // --- Lógica de Descida do Motor ---
        if (!quedaConcluida) {
          motorSobe = false; // Começa a descer imediatamente
          
          // A velocidade de descida acompanha a dissipação (Mínimo 30%, Máximo 100%)
          dutyMotor = map(dutyDissipacao, 0, 100, 30, 100); 

          if (maxBaixo) {
            dutyMotor = 0;
            abrirTrava = false; // Fecha a trava imediatamente!
            quedaConcluida = true; // Tranca a descida pelo resto do ciclo!
            Serial.println("[TRAVA] Fim de curso atingido. Peso travado!");
          }
        } else {
          // Se já bateu no chão, fica parado e travado, mas a dissipação continua o seu ciclo
          dutyMotor = 0; 
          abrirTrava = false;
        }
      }
      break;
  }

  // Atualiza a variável global para o webserver
  dutyDissipacaoGlobal = dutyDissipacao;

  // 4. EXECUÇÃO DE COMANDOS DO MOTOR E DISSIPAÇÃO
  motorIBT2(constrain(dutyMotor, 0, 100), motorSobe);
  controlarDissipacao(constrain(dutyDissipacao, 0, 100));
  
  // 5. MOVIMENTO DO SERVO INTELIGENTE (Anti-Spam e Invertido)
  int anguloDesejado = abrirTrava ? ANGULO_SOLTO : ANGULO_TRAVADO;
  if (anguloDesejado != ultimoAnguloServo) {
    moverSG90(anguloDesejado);
    ultimoAnguloServo = anguloDesejado;
  }

  // Debug visual
  static unsigned long ultimoPrintLogs = 0;
  if (tempoAtual - ultimoPrintLogs > 2000) {
    Serial.printf("[REDE] Est: %s | Cons: %.0fmW | Dissip: %d%% | Mot: %d%% (%s) | Trava: %s\n", 
                  nomesEstados[estadoAtual], consumo_cidade_mW, dutyDissipacao, dutyMotor, 
                  (dutyMotor == 0 ? "PARADO" : (motorSobe ? "Sobe" : "Desce")), 
                  (abrirTrava ? "ABERTA" : "FECHADA"));
    ultimoPrintLogs = tempoAtual;
  }

  delay(50); 
}