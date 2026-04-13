//Incluindo bibliotecas

#include <driver/rtc_io.h>
#include "esp_sleep.h"
#include <Wire.h>
#include <SimpleDHT.h>
#include <SD.h>
#include <RTClib.h>
#include <WiFi.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <Preferences.h>
#include "esp_system.h"
#include "MAF_Firebase.h"
#include "LogStrings.h"
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

//Definindo pinos do ESP32 que serão utilizados no programa

#define uS_TO_MIN_FACTOR 60000000ULL
#define WAKEUP_GPIO    GPIO_NUM_34
#define pinDHT11 13
#define btn_config 16
#define CS_PIN 5
#define pin_abre_valv 26
#define pin_fecha_valv 27
#define led_interacao 33 
#define pin_critico 17
#define led_interacao_erro 32
#define ADC_BAT 35
#define sensor_fluxo 25
#define SCREEN_WIDTH 128   // Largura do display OLED
#define SCREEN_HEIGHT 64   // Altura do display OLED
#define LIMITE_UMIDADE_CHUVA 90

Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);// Configuração display OLED i2C

RTC_DATA_ATTR bool flag_rega = false; //Utilizada para sinalizar que está na hora de regar
RTC_DATA_ATTR bool houve_fluxo_na_rega = false; //Utilizada para sinalizar que houve fluxo de água durante a rega
RTC_DATA_ATTR bool flag_sono_rega = false; //Utilizada para sinalizar se o ESP estava em "sono de rega" (dormindo no tempo de rega)
RTC_DATA_ATTR bool falha_critica = false;//Utilizada para sinalizar se a válvula solenoide está aberta indevidamente.
RTC_DATA_ATTR bool rega_inibida_umidade = false; //Flag para decisão a respeito de umidade

bool recuperando_falha = false; //Utilizada para sinalizar recuperação de controle da válvula após falha.
bool flag_rede_ativa = false;//Utilizada para sinalizar que foi possível conectar no Wifi
bool flag_erro_rede = false;//Utilizada para sinalizar que não foi possível conectar no Wifi
bool estado_led_interacao = true;//Utilizada para piscar LED interação
bool estado_led_erro = true;//Utilizada para piscar LED erro

//Contadores para se utilizar afunção millis();
unsigned long contador_delay =0;
unsigned long contador_delay_erro =0;
unsigned long contador_delay_erro_valv =0; 

bool sensor_fluxo_estado=false; //Utilizada para sinalizar estadodo sensor de fluxo - TRUE (existe fluxo) - FALSE (não existe fluxo)

esp_sleep_wakeup_cause_t causa_deep_sleep_guardada; //Guarda causa do Deep Sleep
RTC_DATA_ATTR int bootCount = 0;

SimpleDHT11 dht11;
RTC_DS1307 rtc;
Preferences prefs; // Configuração de memória permanente, modelo chave-valor.
AsyncWebServer server(80); // Servidor WEB assíncrono (não trava loop)



//Configuração das Páginas WEB
const char geral[] PROGMEM = R"rawliteral(
<html>
<head>
  <meta charset="UTF-8">
  <meta name="viewport" content="width=device-width, initial-scale=1.0">
  <title>Irrigador MAF2000</title>
  <style>
    body {
    font-family: Verdana, Arial, Helvetica, sans-serif;
    color: #0a0a0a;
    margin: auto;
    margin-top: 20px;
    background: #d2dad2;
    min-width: 400px;
    max-width: 600px;
    }
    section{
    background: rgb(51, 231, 81);
    border: #787878 solid 3px;
    border-radius: 7px 7px 0px 0px;
    }
    h1 {
    font-size: 18px;
    text-align: center;
    color: rgb(28, 30, 31);
    padding: 7px 4px 0px 4px;
    }
    h2 {
    font-size: 12px;
    text-align: right;
    color: rgb(60, 153, 234);
    padding: 4px 8px 4px 8px;
    margin: 0px;
    
    }
    h3 {
    background: #6f7888;
    padding: 2px;
    margin: 0px;
    border: #787878 solid 2px;
    border-radius: 0px 0px 7px 7px;
    }
    table {
    width: 100%;
    }
    th {
    font-size: 11px;
    background: rgb(51, 231, 81);
    text-align: left;
    padding: 5px;
    border-radius: 3px 0px 0px 3px;
    width: 25%;
    }
    td {
    font-size: 11px;
    background: rgb(223, 236, 211);
    padding: 5px;
    border-radius: 0px 3px 3px 0px;
    }
    a{
    font-size: 12px;
    color: #2b3bcf;
    }
    .msg{
    font-size: 14px;
    text-align: center;
    padding: 5px;
    border-radius: 3px;
    }
  </style>
</head>
<body>
  <section>
    <h1>Irrigador MAF2000</h1>
    <h2>
      <a href="/geral">Geral</a>&nbsp;&nbsp;&nbsp;
      <a href="/configuracoes">Configurações</a>&nbsp;&nbsp;&nbsp;
      <a href="/dados_gravados">Cadastrados</a>&nbsp;&nbsp;&nbsp;
    </h2>
</section>
  <h3>
  <table>
    <tr>
      <th colspan="2" class="msg">Informações sobre o sistema</th>
    </tr>
    <tr>
      <th>Última rega</th><td>%ultima_rega%</td>
    </tr>
    <tr>
      <th>Temperatura atual</th><td>%temperatura_atual%</td>
    </tr>
    <tr>
      <th>Umidade</th><td>%umidade%</td>
    </tr>
    <tr>
      <th>Bateria</th><td>%bateria_utilizada%</td>
    </tr>
    <tr>
      <th>Reinicializações</th><td>%num_boot%</td>
    </tr>
    <tr>
      <th colspan="2" class="msg"><button onclick="mostrar()">Mostrar Dados Gravados</button></th>
    </tr>
  </table>
  </h3>
  <script>
    function mostrar(){
      window.location.assign("/dados_gravados");
    }
  </script>
</body>
</html>
)rawliteral";




const char configuracoes[] PROGMEM = R"rawliteral(
<html>
<head>
  <meta charset="UTF-8">
  <meta name="viewport" content="width=device-width, initial-scale=1.0">
  <title>Irrigador MAF2000</title>
  <style>
    body {
      font-family: Verdana, Arial, Helvetica, sans-serif;
      color: #0a0a0a;
      margin: auto;
      margin-top: 20px;
      background: #d2dad2;
      min-width: 400px;
      max-width: 600px;
    }
    section{
      background: rgb(51, 231, 81);
      border: #787878 solid 3px;
      border-radius: 7px 7px 0px 0px;
    }
    h1 {
      font-size: 18px;
      text-align: center;
      color: rgb(28, 30, 31);
      padding: 7px 4px 0px 4px;
    }
    h2 {
      font-size: 12px;
      text-align: right;
      color: rgb(60, 153, 234);
      padding: 4px 8px 4px 8px;
      margin: 0px;
    }
    h3 {
      background: #6f7888;
      padding: 2px;
      margin: 0px;
      border: #787878 solid 2px;
      border-radius: 0px 0px 7px 7px;
    }
    table {
      width: 100%%;
    }
    th {
      font-size: 11px;
      background: rgb(51, 231, 81);
      text-align: left;
      padding: 5px;
      border-radius: 3px 0px 0px 3px;
      width: 25%%;
    }
    td {
      font-size: 11px;
      background: rgb(223, 236, 211);
      padding: 5px;
      border-radius: 0px 3px 3px 0px;
    }
    a{
      font-size: 12px;
      color: #2b3bcf;
    }
    .msg{
      font-size: 14px;
      text-align: center;
      padding: 5px;
      border-radius: 3px;
    }
  </style>
</head>
<body>
  <section>
    <h1>Irrigador MAF2000</h1>
    <h2>
      <a href="/geral">Geral</a>&nbsp;&nbsp;&nbsp;
      <a href="/configuracoes">Configurações</a>&nbsp;&nbsp;&nbsp;
      <a href="/dados_gravados">Cadastrados</a>&nbsp;&nbsp;&nbsp;
    </h2>
  </section>

  <form name="save" action="/pg_save" method="POST">
    <h3>
    <table>
      <tr>
        <th colspan="2" class="msg">Configuração</th>
      </tr>
      <tr>
        <th>Dias da Semana</th>
        <td>
          <div>
            <input type="radio" id="todos_dias" name="dias" value="1" checked>
            <label for="todos_dias">Todos os dias</label><br>
            <input type="radio" id="dia_alt" name="dias" value="0">
            <label for="dia_alt">Dias alternados</label><br>
          </div>
        </td>
      </tr>
      <tr>
        <th>Frequência</th>
        <td>
          <div>
            <input type="radio" id="freq_rega_d1x" name="fre_rega_d" value="1" onclick="check()" checked>
            <label for="freq_rega_d1x">1x ao dia</label><br>
            <input type="radio" id="freq_rega_d2x" name="fre_rega_d" value="2" onclick="check()">
            <label for="freq_rega_d2x">2x ao dia</label><br>
          </div>
        </td>
      </tr>
      <tr>
        <th>Horário da Rega</th>
        <td>
          <div>
            <br><label for="hora_rega_1">Horário: </label>
            <input style="margin-left: 15px;" type="number" name="hora_rega_1" id="hora_rega_1" min="0" max="23">
            <div hidden id="fre2">
              <br><label for="hora_rega_2">Horário: </label>
              <input style="margin-left: 15px;" type="number" name="hora_rega_2" id="hora_rega_2" min="0" max="23">
            </div>
          </div>
        </td>
      </tr>
      <tr>
        <th>Tempo da Rega (minutos)</th>
        <td>
          <label for="tempo_rega">Tempo: </label>
          <input style="margin-left: 20px;" type="number" name="tempo_rega" id="tempo_rega" min="1" max="60">
        </td>
      </tr>
      <tr>
        <th>Definição Data RTC</th>
        <td>
          <div>
            <br><label for="rtc_set">Data Atual: </label>
            <input type="datetime-local" id="rtc_set" name="rtc_set">
          </div>
        </td>
      </tr>
      <tr>
        <th>Usar Internet?</th>
        <td>
          <div>
            <input type="radio" id="online" name="internet" value="1" onclick="net()" checked>
            <label for="online">Sim</label><br>
            <input type="radio" id="offline" name="internet" value="0" onclick="net()">
            <label for="offline">Não</label><br>
          </div>
        </td>
      </tr>
      <tr>
        <th>Usar Bateria?</th>
        <td>
          <div>
            <input type="radio" id="bateria_sim" name="bateria" value="1" onclick="bat()" checked>
            <label for="bateria_sim">Sim</label><br>
            <input type="radio" id="bateria_nao" name="bateria" value="0" onclick="bat()">
            <label for="bateria_nao">Não</label><br>
          </div>
        </td>
      </tr>
      <tr id="tipo_bat">
        <th>Tipo de Bateria?</th>
        <td>
          <div>
            <input type="radio" id="bat_interna" name="tipo_bateria" value="7" checked>
            <label for="bat_interna">7,4 V interna</label><br>
            <input type="radio" id="bat_externa" name="tipo_bateria" value="12">
            <label for="bat_externa">12 V externa</label><br>
          </div>
        </td>
      </tr>
      <tr id="wifi">
        <th>Wifi</th>
        <td>
          <div>
            <label for="wifi_config">SSID:</label>&nbsp;
            <input style="margin-left: 10px;" type="text" id = "wifi_ssid" name="wifi_ssid">
          </div>
          <div>
            <label for="wifi_config">Senha:</label>
            <input style="margin-left: 8px;" type="password" id = "wifi_pass" name="wifi_pass">
          </div>
        </td>
      </tr>
      <tr id="semnet">
        <th>Dados Firebase</th>
        <td>
          <div>
            <label for="link_db">Link DB:</label>
            <input style="margin-left: 0px;" type="text" id="link_db" name="link_db">
          </div>
        </td>
      </tr>
      <tr>
        <th colspan="2" class="msg">
          <input type="button" value="Gravar" onclick="valida()">
        </th>
      </tr>
    </table>
    </h3>
  </form>

  <script>
    // Atualiza o campo rtc_set com a hora local do navegador
    function atualizarRTC() {
      const rtcInput = document.getElementById('rtc_set');
      if (!rtcInput) return;

      // Não sobrescreve se o usuário estiver editando manualmente
      if (document.activeElement === rtcInput) return;

      const now = new Date();
      const localISOTime = new Date(now.getTime() - now.getTimezoneOffset() * 60000)
        .toISOString()
        .slice(0, 16); // YYYY-MM-DDTHH:MM

      rtcInput.value = localISOTime;
    }

    // Inicializa e agenda atualização a cada minuto
    window.addEventListener('DOMContentLoaded', function () {
      atualizarRTC();                    // preenche ao carregar
      setInterval(atualizarRTC, 60000);  // atualiza a cada minuto
      const q1 = document.getElementById('hora_rega_1');
      if (q1) q1.value = 6;

  // preenche horário padrão da segunda rega (19:00)
      const q2 = document.getElementById('hora_rega_2');
      if (q2) q2.value = 19;

  // preenche tempo de rega padrão (15 minutos)
      const tempo = document.getElementById('tempo_rega');
      if (tempo) tempo.value = 15;
    });


    function net() {
      if (document.getElementById('online').checked == true) {
        document.getElementById('semnet').removeAttribute('hidden');
        document.getElementById('wifi').removeAttribute('hidden');
    } else {
        document.getElementById('semnet').setAttribute('hidden', 'true');
        document.getElementById('wifi').setAttribute('hidden', 'true');
    }
    }

    function bat() {
      if (document.getElementById('bateria_sim').checked == true) {
        document.getElementById('tipo_bat').removeAttribute('hidden');
    } else {
        document.getElementById('tipo_bat').setAttribute('hidden', 'true');
    }
    }

    function check(){
      if(document.getElementById('freq_rega_d2x').checked == true)
        document.getElementById('fre2').removeAttribute('hidden');
      else{
        document.getElementById('fre2').setAttribute('hidden','true');
        document.getElementById('hora_rega_2').value='';
      }
    }



    function valida(){
      let sem_internet = document.getElementById('offline').checked;
      let box_h_rega = document.getElementById('freq_rega_d2x').checked;
      let h_rega = document.getElementById('hora_rega_1').value;
      let h_rega2 = document.getElementById('hora_rega_2').value;
      let rtc_set = document.getElementById('rtc_set').value;
      let tempo_rega = document.getElementById('tempo_rega').value;
      let link_db = document.getElementById('link_db').value;
      let wifi_ssid = document.getElementById('wifi_ssid').value;
      let wifi_pass = document.getElementById('wifi_pass').value;

      if(tempo_rega == "" || tempo_rega < 1 || tempo_rega > 60){
        alert("Digite um valor válido para o tempo de rega");
        return;
      }
      if(rtc_set == ""){
        alert("Digite a data/horário atual para o RTC");
        return;
      }
      if (h_rega == "") {
        alert("Digite horário para rega");
        return;
      }
      if(box_h_rega == true){
        if(h_rega == "" || h_rega2 == ""){
          alert("Digite horário para rega");
          return;
        }else{
          if(h_rega == ""){
            alert("Digite horário para rega");
          }
        }
      }
      if (h_rega < 0 || h_rega > 23 || h_rega2 < 0 || h_rega2 > 23){
        alert("Digite horário válido");
          return;
      }
      if (sem_internet == false){
        if(wifi_ssid == "" || wifi_pass == "" || link_db == "" ){
          alert("Digite algum valor para os dados de Wifi e Firebase");
          return;
        }
      }
      document.save.submit();
    }
  </script>
</body>
</html>
)rawliteral";


const char dados_gravados[] PROGMEM = R"rawliteral(

<html>
<head>
  <meta charset="UTF-8">
  <meta name="viewport" content="width=device-width, initial-scale=1.0">
  <title>Irrigador MAF2000</title>
  <style>
    body {
    font-family: Verdana, Arial, Helvetica, sans-serif;
    color: #0a0a0a;
    margin: auto;
    margin-top: 20px;
    background: #d2dad2;
    min-width: 400px;
    max-width: 600px;
    }
    section{
    background: rgb(51, 231, 81);
    border: #787878 solid 3px;
    border-radius: 7px 7px 0px 0px;
    }
    h1 {
    font-size: 18px;
    text-align: center;
    color: rgb(28, 30, 31);
    padding: 7px 4px 0px 4px;
    }
    h2 {
    font-size: 12px;
    text-align: right;
    color: rgb(60, 153, 234);
    padding: 4px 8px 4px 8px;
    margin: 0px;
    
    }
    h3 {
    background: #6f7888;
    padding: 2px;
    margin: 0px;
    border: #787878 solid 2px;
    border-radius: 0px 0px 7px 7px;
    }
    table {
    width: 100%;
    }
    th {
    font-size: 11px;
    background: rgb(51, 231, 81);
    text-align: left;
    padding: 5px;
    border-radius: 3px 0px 0px 3px;
    width: 25%;
    }
    td {
    font-size: 11px;
    background: rgb(223, 236, 211);
    padding: 5px;
    border-radius: 0px 3px 3px 0px;
    }
    a{
    font-size: 12px;
    color: #2b3bcf;
    }
    .msg{
    font-size: 14px;
    text-align: center;
    padding: 5px;
    border-radius: 3px;
    }
  </style>
</head>
<body>
  <section>
    <h1>Irrigador MAF2000</h1>
    <h2>
      <a href="/geral">Geral</a>&nbsp;&nbsp;&nbsp;
      <a href="/configuracoes">Configurações</a>&nbsp;&nbsp;&nbsp;
      <a href="/dados_gravados">Cadastrados</a>&nbsp;&nbsp;&nbsp;
    </h2>
</section>
  <h3>
  <table>
    <tr>
      <th colspan="2" class="msg">Dados Cadastrados</th>
    </tr>
    <tr>
      <th>Wifi</th><td><span id = "ssid">%wifi_ssid%</span></br><span id = "wifi_s">%wifi_pass%</span></td>
    </tr>
    <tr>
      <th>Link DB</th><td id = "link_db">%link_db%</td>
    </tr>
    <tr>
      <th>Bateria</th><td id = "t_bat">%bateria_utilizada%</td>
    </tr>
    <tr>
      <th>Rega</th>
        <td>
          <span id="fre_d">Frequência/dia: %frequencia_dia%</span><br/>
          <span id="freq_s">Frequência/semana: %frequencia_semana%</span><br/>
          <span id="hora1">Horário 1: %hora_rega_1%</span><br/>
          <span id="hora2">Horário 2: %hora_rega_2%</span><br/>
          <span id="tempo_rega">Tempo de rega: %tempo_rega%</span>
        </td>
    </tr>
  </table>
  </h3>
</body>
</html>



)rawliteral";

//Fim página WEB

String processor(const String &var){ // Função utilizada para mostrar dados gravados no ESP na página WEB
  Serial.print("Placeholder pedido: ");
  Serial.println(var);

  String resposta = "--";

  prefs.begin("maf2000", true);  // Somente leitura

  if (var == "ultima_rega") {
    resposta = prefs.getString("ultima_rega", "Sem rega ainda");
  }
  else if (var == "temperatura_atual") {
    int t = prefs.getInt("temperatura", -127);
    if (t == -127) resposta = "N/D";
    else resposta = String(t) + " °C";
  }
  else if (var == "umidade") {
    int u = prefs.getInt("umidade", -1);
    if (u < 0) resposta = "N/D";
    else resposta = String(u) + " %";
  }
  else if (var == "bateria_utilizada") {
    resposta = tensao_vin(ADC_BAT);
  }
  else if (var == "num_boot") {
    resposta = String(bootCount);
  }

  prefs.end();
  return resposta;
}

String montarPaginaGeral() {
  // Lê os dados salvos
  prefs.begin("maf2000", true);
  String ultima_rega = prefs.getString("ultima_rega", "Sem rega ainda");
  int t = prefs.getInt("temperatura", -127);
  int u = prefs.getInt("umidade", -1);
  prefs.end();

  // Monta strings legíveis
  String temperatura_atual;
  if (t == -127) temperatura_atual = "N/D";
  else temperatura_atual = String(t) + " °C";

  String umidade;
  if (u < 0) umidade = "N/D";
  else umidade = String(u) + " %";

  String bateria = tensao_vin(ADC_BAT);
  String num_boot_str = String(bootCount);

  // Copia o html do progman para uma String
  String page = FPSTR(geral);

  // Substitui os placeholders manualmente
  page.replace("%ultima_rega%",        ultima_rega);
  page.replace("%temperatura_atual%",  temperatura_atual);
  page.replace("%umidade%",            umidade);
  page.replace("%bateria_utilizada%",  bateria);
  page.replace("%num_boot%",           num_boot_str);

  return page;
}


String montarPaginaDados() {
  // Lê dados das Preferences
  prefs.begin("maf2000", true);
  String wifi_ssid        = prefs.getString("wifi_ssid", "(nao configurado)");
  String wifi_pass        = prefs.getString("wifi_pass", "(nao configurada)");
  String link_db          = prefs.getString("link_db", "(nao configurado)");
  int tempo_rega          = prefs.getInt("tempo_rega", 15);
  int frequencia_rega     = prefs.getInt("frequencia_rega", 1); // 1 ou 2x ao dia
  int dias_rega           = prefs.getInt("dias_rega", 1);       // 1 = todos, 0 = alternados
  int hora_rega_1         = prefs.getInt("hora_rega_1", 6);
  int hora_rega_2         = prefs.getInt("hora_rega_2", 19);
  prefs.end();

  // Estado da bateria
  String bateria = tensao_vin(ADC_BAT);

  // Frequência por dia
  String freqDiaStr;
  if (frequencia_rega <= 1) {
    freqDiaStr = "1x ao dia";
  } else {
    freqDiaStr = String(frequencia_rega) + "x ao dia";
  }

  // Frequência por semana (aprox.)
  String freqSemanaStr;
  if (dias_rega == 1) {
    int vezesSemana = 7 * frequencia_rega;
    freqSemanaStr = String(vezesSemana) + "x/semana (todos os dias)";
  } else {
    // dias alternados  aprox. 3 dias de rega na semana
    int vezesSemana = 3 * frequencia_rega;
    freqSemanaStr = String(vezesSemana) + "x/semana (dias alternados)";
  }

  // Tempo de rega
  String tempoRegaStr = String(tempo_rega) + " min";

  // Formata horários sempre como HH:00
  auto fmtHora = [](int h) -> String {
    if (h < 0 || h > 23) return String("--");
    String s;
    if (h < 10) s = "0" + String(h);
    else        s = String(h);
    s += ":00";
    return s;
  };

  String hora1Str = fmtHora(hora_rega_1);

  // Se frequência for 1x/dia, horário 2 pode ser irrelevante
  String hora2Str;
  if (frequencia_rega <= 1) {
    hora2Str = "--";
  } else {
    hora2Str = fmtHora(hora_rega_2);
  }

  // Copia o html do progman para uma String
  String page = FPSTR(dados_gravados);

  // Substitui placeholders
  page.replace("%wifi_ssid%",           wifi_ssid);
  page.replace("%wifi_pass%",           wifi_pass);
  page.replace("%link_db%",             link_db);
  page.replace("%bateria_utilizada%",   bateria);
  page.replace("%tempo_rega%",          tempoRegaStr);
  page.replace("%frequencia_dia%",      freqDiaStr);
  page.replace("%frequencia_semana%",   freqSemanaStr);
  page.replace("%hora_rega_1%",         hora1Str);
  page.replace("%hora_rega_2%",         hora2Str);

  return page;
}



void displayMensagem(const String &linha1, const String &linha2 = "", const String &linha3 = "", const String &linha4 = "") { // Função para mostrar mensagens no display (OLED) - Evita repetição de código.
  display.clearDisplay(); 
  display.setTextSize(1); 
  display.setTextColor(SSD1306_WHITE); 
  display.setCursor(0, 0);

  if (linha1.length() > 0){
    display.println(linha1);
  }

  if (linha2.length() > 0) {
    display.println(linha2);
  }
  if (linha3.length() > 0){
    display.println(linha3);
  }
  if (linha4.length() > 0) {
    display.println(linha4);
  }

  display.display();
}

void mostrarTelaResumo(const String &dataHoraUltRega, int temperatura, int umidade, const String &proxRegaEm) { // Função utilizadas para mostrar resumo de informação do ESP quando não acordar por timer (quando nao estiver na hora de rega)

  String l1 = "Ultima rega:";
  String l2 = dataHoraUltRega;  
  
  String l3 = "T:" + String(temperatura) + "C U:" + String(umidade) + "%";
  
  String l4 = "Prox em: " + proxRegaEm;

  displayMensagem(l1, l2, l3, l4);
}


bool valvulaAbrir() {
  // Estado inicial: desconhecido/fechada
  Serial.println("Comando: Abrir válvula");
  delay(1000);
  digitalWrite(pin_abre_valv, LOW);
  digitalWrite(pin_fecha_valv, LOW);
  digitalWrite(led_interacao, HIGH);
  digitalWrite(pin_abre_valv, HIGH);
  delay(1000);
  digitalWrite(pin_abre_valv, LOW);

  displayMensagem("Rega iniciada", "Abrindo valvula", "Verificando fluxo...");

  Serial.println("Válvula aberta com sucesso");
  sensor_fluxo_estado = true;
  digitalWrite(led_interacao, LOW);

  return true;
}


bool valvulaFechar() {
  Serial.println("Comando: FECHAR válvula");
  // Estado inicial: desconhecido/fechada
  delay(1000);
  digitalWrite(pin_abre_valv, LOW);
  digitalWrite(pin_fecha_valv, LOW);
  digitalWrite(led_interacao, HIGH);
  digitalWrite(pin_fecha_valv, HIGH);
  delay(1000);
  digitalWrite(pin_fecha_valv, LOW);
  if (digitalRead(sensor_fluxo) == LOW) {  // ainda vê fluxo
  Serial.println("Irrigador com problemas, falha crítica acionada");
  sensor_fluxo_estado = true;
  falha_critica = true;

  displayMensagem("ERRO DE REGA!", "Fluxo indevido detectado", "Verifique a válvula");

  digitalWrite(led_interacao, LOW);

  return false;
}
  if (recuperando_falha) {
    displayMensagem("Falha recuperada!", "Fluxo interrompido", "Sistema normalizado"); 
    Serial.println("Sistema reestabelecido após BUG na válvula");
  } else {
    displayMensagem("Rega concluida", "Fluxo OK", "Sem erros");
    Serial.println("Válvula fechada com sucesso");
  }
  
  sensor_fluxo_estado = false;
  falha_critica = false;
  digitalWrite(led_interacao, LOW);

  return true;
}

LogStrings montar_strings_log(const DateTime& now) { // Struct para salvar os logs de maneira mais fácil e sem repetição de código
  LogStrings log;

  const char* dias[7] = { "Domingo", "Segunda-Feira", "Terca-Feira", "Quarta-Feira", "Quinta-Feira", "Sexta-Feira", "Sabado" };

  // DATA: formato dd/mm/aaaa
  log.data =
      (now.day()   < 10 ? "0" : "") + String(now.day())   + "/" +
      (now.month() < 10 ? "0" : "") + String(now.month()) + "/" +
      String(now.year());

  // HORA: formato hh:mm:ss
  log.hora =
      (now.hour()   < 10 ? "0" : "") + String(now.hour())   + ":" +
      (now.minute() < 10 ? "0" : "") + String(now.minute()) + ":" +
      (now.second() < 10 ? "0" : "") + String(now.second());

  // DIA DA SEMANA
  log.dia_semana = dias[now.dayOfTheWeek()];

  return log;
}

bool ajustar_RTC(const String &rtc_set) {// Ajusta o RTC conforme os dados informados pelos usuário vem de método POST /pg_save
  String s = rtc_set;
  s.trim();

  if (s.length() < 16) {
    Serial.println("rtc_set invalido: " + s);
    return false;
  }

  // Detecta AM/PM (caso venha nesse formato)
  bool isPM = (s.indexOf("PM") != -1) || (s.indexOf("pm") != -1);
  bool isAM = (s.indexOf("AM") != -1) || (s.indexOf("am") != -1);

  // Remove AM/PM da string se existir
  if (isAM || isPM) {
    int pos = s.indexOf("PM");
    if (pos == -1) pos = s.indexOf("pm");
    if (pos == -1) pos = s.indexOf("AM");
    if (pos == -1) pos = s.indexOf("am");
    if (pos != -1) {
      s.remove(pos, 2);
      s.trim();
    }
  }

  // Formato esperado principal:
  // "YYYY-MM-DDTHH:MM"  (padrão datetime-local)
  // ou "YYYY-MM-DD HH:MM"
  if (s.length() < 16) {
    Serial.println("rtc_set muito curto apos limpar AM/PM: " + s);
    return false;
  }

  int year  = s.substring(0, 4).toInt();
  int month = s.substring(5, 7).toInt();
  int day   = s.substring(8,10).toInt();

  // Procura separador entre data e hora: 'T' ou espaço
  int timeStart = s.indexOf('T');
  if (timeStart == -1) {
    timeStart = s.indexOf(' ');
  }
  if (timeStart == -1) {
    Serial.println("Nao encontrei separador T/espaco em: " + s);
    return false;
  }

  int hour   = s.substring(timeStart + 1, timeStart + 3).toInt();
  int minute = s.substring(timeStart + 4, timeStart + 6).toInt();
  int second = 0;

  // Se tiver segundos (ex: YYYY-MM-DDTHH:MM:SS)
  if (s.length() >= timeStart + 9 && s.charAt(timeStart + 6) == ':') {
    second = s.substring(timeStart + 7, timeStart + 9).toInt();
  }

  // Ajuste de 12h → 24h se vier com AM/PM
  if (isPM && hour < 12) {
    hour += 12;       // 1PM–11PM → 13–23
  }
  if (isAM && hour == 12) {
    hour = 0;         // 12AM → 00h
  }

  Serial.print("Ajustando RTC para: ");
  Serial.print(day);   Serial.print('/');
  Serial.print(month); Serial.print('/');
  Serial.print(year);  Serial.print(' ');
  Serial.print(hour);  Serial.print(':');
  Serial.print(minute);Serial.print(':');
  Serial.println(second);

  rtc.adjust(DateTime(year, month, day, hour, minute, second));
  return true;
}


void firebase(bool envia, bool sincroniza) {//Função de envio e sincronização de dados com Firebase.
  prefs.begin("maf2000", false);
  String link_db = prefs.getString("link_db", "");  

  if (link_db.length() == 0) {
    Serial.println("Firebase: link_db nao configurado, abortando envio/sincronizacao.");
    prefs.end();
    return;
  }

  firebaseSetURL(link_db);
  DateTime now = rtc.now();

  LogStrings log = montar_strings_log(now);
  if (sincroniza) {
    String s_freq  = firebaseReadString("/irrigador_db_app/frequencia_rega");
    String s_h1    = firebaseReadString("/irrigador_db_app/hora_rega_1");
    String s_h2    = firebaseReadString("/irrigador_db_app/hora_rega_2");
    String s_tempo = firebaseReadString("/irrigador_db_app/tempo_rega");
    String s_dias  = firebaseReadString("/irrigador_db_app/dias_rega");

    int frequencia_rega = s_freq.toInt();
    int hora_rega_1     = s_h1.toInt();
    int hora_rega_2     = s_h2.toInt();
    int tempo_rega      = s_tempo.toInt();
    int dias_rega       = s_dias.toInt();

    // frequencia: só 1 ou 2
    if (frequencia_rega == 1 || frequencia_rega == 2) {
      prefs.putInt("frequencia_rega", frequencia_rega);
    } else {
      Serial.println("Erro ao sincronizar frequencia_rega");
    }

    // hora 1: 0..23
    if (hora_rega_1 >= 0 && hora_rega_1 <= 23) {
      prefs.putInt("hora_rega_1", hora_rega_1);
    } else {
      Serial.println("Erro ao sincronizar hora_rega_1");
    }

    // hora 2: 0..23 (ou você pode aceitar só se frequencia_rega == 2)
    if (hora_rega_2 >= 0 && hora_rega_2 <= 23) {
      prefs.putInt("hora_rega_2", hora_rega_2);
    } else {
      Serial.println("Erro ao sincronizar hora_rega_2");
    }

    // tempo: 1..60
    if (tempo_rega >= 1 && tempo_rega <= 60) {
      prefs.putInt("tempo_rega", tempo_rega);
    } else {
      Serial.println("Erro ao sincronizar tempo_rega");
    }

    // dias: 0 ou 1 (0 é válido!)
    if (dias_rega == 0 || dias_rega == 1) {
      prefs.putInt("dias_rega", dias_rega);
    } else {
      Serial.println("Erro ao sincronizar dias_rega");
    }
    Serial.printf("SYNC OK: freq=%d h1=%d h2=%d tempo=%d dias=%d\n", frequencia_rega, hora_rega_1, hora_rega_2, tempo_rega, dias_rega);
  }

  if (envia) {
    Serial.println("Gravando...");

    bool data       = firebaseWriteString("/irrigador_db_esp/data_ultima_rega",   log.data);
    bool hora       = firebaseWriteString("/irrigador_db_esp/hora_ultima_rega",   log.hora);
    bool dia_semana = firebaseWriteString("/irrigador_db_esp/semana_ultima_rega", log.dia_semana);

    int freq_reda_d = prefs.getInt("frequencia_rega", 0);
    bool frequencia_rega = firebaseWriteString("/irrigador_db_esp/frequencia_rega",String(prefs.getInt("frequencia_rega", -1)));
    bool hora_rega_1 = firebaseWriteString("/irrigador_db_esp/hora_rega_1",String(prefs.getInt("hora_rega_1", -1)));
    bool hora_rega_2 = false;
    if (freq_reda_d == 2){
      hora_rega_2 = firebaseWriteString("/irrigador_db_esp/hora_rega_2",String(prefs.getInt("hora_rega_2", 0)));
    }
    bool dias_rega = firebaseWriteString("/irrigador_db_esp/dias_rega",String(prefs.getInt("dias_rega", -1)));
    bool tempo_rega = firebaseWriteString("/irrigador_db_esp/tempo_rega",String(prefs.getInt("tempo_rega", -1)));
    
    


    int  temperatura      = 0;
    int  umidade          = 0;
    bool flag_temperatura = false;
    bool flag_umidade     = false;

    if (le_temp_umidade(temperatura, umidade)) {
      flag_temperatura = firebaseWriteString("/irrigador_db_esp/temperatura", String(temperatura));
      flag_umidade     = firebaseWriteString("/irrigador_db_esp/umidade",     String(umidade));
    } else {
      Serial.println("Nao consegui ler DHT11, nao vou enviar temp/umidade para o Firebase.");
    }

    String status_bat = tensao_vin(ADC_BAT);
    bool fb_bat       = firebaseWriteString("/irrigador_db_esp/tensao_vin", status_bat);

    Serial.println(data         ? "Data gravada no Firebase"          : "Data falhou");
    Serial.println(hora         ? "Hora gravada no Firebase"          : "Hora falhou");
    Serial.println(dia_semana   ? "Dia da semana gravado"             : "Dia da semana falhou");
    Serial.println(flag_temperatura ? "Temperatura gravada"           : "Temperatura falhou");
    Serial.println(flag_umidade     ? "Umidade gravada"               : "Umidade falhou");
    Serial.println(fb_bat       ? "Status tensão gravado"             : "Status tensão falhou");
    Serial.println(frequencia_rega   ? "Quantidade diária gravada"             : "Quantidade diária falhou");
    Serial.println(hora_rega_1 ? "Hora rega 1 gravado"           : "Hora rega 1 falhou");
    if (freq_reda_d == 2) {
      Serial.println(hora_rega_2 ? "Hora rega 2 gravado" : "Hora rega 2 falhou");
    }
    Serial.println(dias_rega     ? "Frequência diária gravada"               : "Frequência diária falhou");
    Serial.println(tempo_rega       ? "Tempo rega gravado"             : "Tempo rega falhou");
  }

  

  prefs.end();
}


String tensao_vin(int pin_adc) {
  const float R1 = 150000.0;
  const float R2 = 10000.0;
  const float fator = (R1 + R2) / R2;  // 16
  const int amostras = 30;

  uint32_t acumulador = 0;
  for (int i = 0; i < amostras; i++) {
    acumulador += (uint32_t)analogReadMilliVolts(pin_adc);
    delayMicroseconds(200);
  }

  float Vadc_mV  = (float)acumulador / (float)amostras;
  float Vin = (Vadc_mV * fator) / 1000.0;

  prefs.begin("maf2000", true);
  int tipo = prefs.getInt("tipo_bateria", 7);
  int bat  = prefs.getInt("bateria", 0);
  prefs.end();

  // formata tensão com 2 casas (bom p/ UI)
  String Vfonte = String(Vin, 2) + "V";

  if (bat != 1) {
    return "Fonte externa (" + Vfonte + ")";
  }

  // Bateria selecionada
  if (tipo == 12) {
    if (Vin >= 12.8) return "Bat 12V - Carregada (" + Vfonte + ")";
    if (Vin >= 12.0) return "Bat 12V - Normal (" + Vfonte + ")";
    if (Vin >= 11.8) return "Bat 12V - Baixa (" + Vfonte + ")";
    if (Vin >= 11.5) return "Bat 12V - Critica (" + Vfonte + ")";
    return "Bat 12V - Morta (" + Vfonte + ")";
  } else {
    // 2S Li-ion
    if (Vin >= 8.30) return "Bat 7,4V - Carregada (" + Vfonte + ")";
    if (Vin >= 7.40) return "Bat 7,4V - Normal (" + Vfonte + ")";
    if (Vin >= 7.00) return "Bat 7,4V - Baixa (" + Vfonte + ")";
    if (Vin >= 6.60) return "Bat 7,4V - Critica (" + Vfonte + ")";
    return "Bat 7,4V - Morta (" + Vfonte + ")";
  }
}



bool le_temp_umidade(int &temp, int &umid) {// Trabalha com sensor DHT11
  byte t = 0, h = 0;

  for (int tent = 0; tent < 3; tent++) {
    int err = dht11.read(pinDHT11, &t, &h, NULL);

    if (err == SimpleDHTErrSuccess) {
      temp = (int)t;
      umid = (int)h;
      return true;
    }

    Serial.print("DHT11 erro (tentativa ");
    Serial.print(tent + 1);
    Serial.print("), err=");
    Serial.print(SimpleDHTErrCode(err));
    Serial.print(",");
    Serial.println(SimpleDHTErrDuration(err));

    delay(1500);
  }

  return false;
}


void imprimir_log_serial(const DateTime& now, int temperatura, int umidade, bool teste_rega) {//Imprimir log de rega
  LogStrings log = montar_strings_log(now);
  String statusVin = tensao_vin(ADC_BAT);

  Serial.print(log.data);
  Serial.print(',');
  Serial.print(log.hora); 
  Serial.print(',');
  Serial.print(log.dia_semana);
  Serial.print(',');

  Serial.print(temperatura);
  Serial.print("\xC2\xB0""C");
  Serial.print(',');
  Serial.print(umidade);
  Serial.print('%');
  Serial.print(',');
  Serial.print(statusVin);
  Serial.print(',');
  Serial.print(flag_erro_rede ? "Erro_rede" : "Sem_erro");
  Serial.print(',');
  if (teste_rega) {
    Serial.print("Rega ok");
  } else if (rega_inibida_umidade) {
    Serial.print("Rega inibida por umidade");
  } else {
    Serial.print("Rega falhou");
  }
  Serial.print(',');
  Serial.println(houve_fluxo_na_rega ? "Houve fluxo na rega" : "Sem fluxo detectado na rega");
}


void gravar_log_SD(File &f, const DateTime& now, int temperatura, int umidade,bool teste_rega) {//Gravar log de rega
  LogStrings log = montar_strings_log(now);
  String statusVin = tensao_vin(ADC_BAT);

  f.print(log.data);
  f.print(',');
  f.print(log.hora);
  f.print(',');
  f.print(log.dia_semana);
  f.print(',');
  f.print(temperatura);
  f.print(',');
  f.print(umidade);
  f.print(',');
  f.print(statusVin);
  f.print(',');
  if(flag_erro_rede){
  f.print("Erro_rede");
  } else {
    f.print("Sem_erro");
  }
  f.print(',');
  if (teste_rega) {
    f.print("Rega_ok");
  } else if (rega_inibida_umidade) {
    f.print("Rega_inibida_por_umidade");
  } else {
    f.print("Rega_falhou");
  }
  f.print(',');
  f.println(houve_fluxo_na_rega ? "Houve_fluxo" : "Sem_fluxo");
}

void pisca_led_erro(){ //Interação com LED erro
    if(millis() >= (contador_delay_erro + 1000)){
      contador_delay_erro = millis();
      estado_led_erro = !estado_led_erro; 
      digitalWrite(led_interacao_erro, !estado_led_erro);
    } 
}

void valvulaFechar_falha() {//Função para tentar fechar válvula quando em falha
  recuperando_falha = true;
  if (millis() >= (contador_delay_erro_valv + 10000)) { // a cada 10s
    contador_delay_erro_valv = millis();
    Serial.println("Tentando fechar válvula novamente após falha crítica");

    if (valvulaFechar()) {
      falha_critica = false;
      digitalWrite(pin_critico, LOW);
      digitalWrite(led_interacao_erro, LOW);
      Serial.println("Válvula fechou com sucesso na tentativa de recuperação.");
      esp_restart();
    } else {
      Serial.println("Continua falha ao fechar válvula.");
    }
  }
}


void pisca_led_interacao(){//Interação com usuário
    if(millis() >= (contador_delay + 1000)){
      contador_delay = millis();
      estado_led_interacao = !estado_led_interacao;
      digitalWrite(led_interacao, !estado_led_interacao);
    } 
}


bool grava_exibe_log(bool teste_rega) {// Função que concentra as funções de gravar e exibir lOGs
  Serial.println("abrindo cartao SD");

  File f = SD.open("/log.csv", FILE_APPEND);
  if (!f) {
    Serial.println("erro ao abrir arquivo do cartao SD");
    DateTime now = rtc.now();
    LogStrings log = montar_strings_log(now);
    
    if (teste_rega && houve_fluxo_na_rega) {
      prefs.begin("maf2000", false);
      prefs.putString("ultima_rega", log.data + " - " + log.hora);
      prefs.end();
    }
    return false;
  }

  if (f.size() == 0) {
    f.println("data,hora,dia_da_semana,temperatura_C,umidade_pct,status_vin,erro_rede,status_rega,fluxo");
  }

  DateTime now = rtc.now();

  int temperatura = 0;
  int umidade     = 0;
  if (!le_temp_umidade(temperatura, umidade)) {
    Serial.println("Falha lendo DHT11, vou gravar log de erro.");
    temperatura = -127;
    umidade     = -1;
  }

  imprimir_log_serial(now, temperatura, umidade, teste_rega);
  gravar_log_SD(f, now, temperatura, umidade, teste_rega);


  LogStrings log = montar_strings_log(now);

  prefs.begin("maf2000", false);
  if (teste_rega && houve_fluxo_na_rega) {
      prefs.putString("ultima_rega", log.data + " - " + log.hora);
  }
  prefs.putInt("temperatura",temperatura);
  prefs.putInt("umidade",umidade);
  prefs.end();

  f.flush();
  f.close();

  Serial.println("dados gravados");
  return true;
}


esp_sleep_wakeup_cause_t print_get_wakeup_reason() {// Função para guardar e exibir causa do DeepSleep.
  esp_sleep_wakeup_cause_t wakeup_reason = esp_sleep_get_wakeup_cause();

  causa_deep_sleep_guardada = wakeup_reason;
  
  switch (wakeup_reason) {
    case ESP_SLEEP_WAKEUP_EXT0:     Serial.println("Wakeup by EXT0"); break;
    case ESP_SLEEP_WAKEUP_EXT1:     Serial.println("Wakeup by EXT1"); break;
    case ESP_SLEEP_WAKEUP_TIMER:    Serial.println("Wakeup by TIMER"); break;
    default:                        Serial.printf("Wakeup not by deep sleep: %d\n", wakeup_reason); break;
  }
  return wakeup_reason;
}

int calc_prox_sono(float hora_atual) {// Calcula a quantidade de minutos até a próxima rega.
  int hora_rega_1 = 0;
  int hora_rega_2 = 0;
  int frequencia_rega = 1; // 1: 1x ao dia, 2: 2x ao dia
  int dias_rega = 1;       // 1: todos os dias, 0: dias alternados

  // Lê configurações principais
  prefs.begin("maf2000", true);
  hora_rega_1     = prefs.getInt("hora_rega_1", 6);   // padrão 6h
  hora_rega_2     = prefs.getInt("hora_rega_2", 19);  // padrão 19h
  frequencia_rega = prefs.getInt("frequencia_rega", 1);
  dias_rega       = prefs.getInt("dias_rega", 1);
  String ultima   = prefs.getString("ultima_rega", "");
  prefs.end();

  DateTime now = rtc.now();


  if (dias_rega == 1) {
    int minutos = 0;

    if (frequencia_rega == 1) {
      // Apenas uma rega por dia
      if (hora_rega_1 > hora_atual) {
        minutos = (hora_rega_1 - hora_atual) * 60;
      } else {
        minutos = (24.0f - hora_atual + hora_rega_1) * 60;
      }
      return minutos;
    }

    // Duas regas por dia
    if (hora_rega_1 > hora_atual) {
      return (hora_rega_1 - hora_atual) * 60;
    }
    if (hora_rega_2 > hora_atual) {
      return (hora_rega_2 - hora_atual) * 60;
    }

    // Já passou das duas → próxima é amanhã na hora 1
    minutos = (24.0f - hora_atual + hora_rega_1) * 60;
    return minutos;
  }

  // Bloco para dias alternados
  int lastDay = -1, lastMonth = -1, lastYear = -1;

  if (ultima.length() >= 10) {
    // formato esperado: "dd/mm/aaaa - hh:mm:ss"
    lastDay   = ultima.substring(0, 2).toInt();
    lastMonth = ultima.substring(3, 5).toInt();
    lastYear  = ultima.substring(6, 10).toInt();
  }

  int dias_desde_ultima = 9999; // valor bem grande se não der pra calcular

  if (lastYear > 0 && lastMonth > 0 && lastDay > 0) {
    DateTime dtLast(lastYear, lastMonth, lastDay, 0, 0, 0);
    long diff_seg = now.unixtime() - dtLast.unixtime();
    if (diff_seg >= 0) {
      dias_desde_ultima = diff_seg / 86400; // 86400s = 1 dia
    }
  }

  bool hoje_pode_irrigar = (dias_desde_ultima >= 2) || (dias_desde_ultima == 9999);

  //Se ainda não se passaram 2 dias desde a última rega: não regar
  if (!hoje_pode_irrigar && lastYear > 0) {
    // Próxima rega é em (ultima_rega + 2 dias), horário = hora_rega_1
    DateTime dtLast(lastYear, lastMonth, lastDay, hora_rega_1, 0, 0);
    DateTime proxima = dtLast + TimeSpan(2, 0, 0, 0);

    long diff_seg = proxima.unixtime() - now.unixtime();
    if (diff_seg < 60) diff_seg = 60; // garante pelo menos 1 minuto

    int minutos = diff_seg / 60;
    return minutos;
  }

  // Se já passaram 2 ou mais dias: hoje é dia de irrigar,
  

  // Dia de regar (alternando)
  if (frequencia_rega == 1) {
    // Uma rega por dia alternado
    if (hora_rega_1 > hora_atual) {
      return (hora_rega_1 - hora_atual) * 60;
    } else {
      // próxima rega: hoje + 2 dias, na hora 1
      DateTime proxima(now.year(), now.month(), now.day(), hora_rega_1, 0, 0);
      proxima = proxima + TimeSpan(2, 0, 0, 0);

      long diff_seg = proxima.unixtime() - now.unixtime();
      if (diff_seg < 60) diff_seg = 60;

      int minutos = diff_seg / 60;
      return minutos;
    }
  }

  // Duas regas por dia alternado
  if (hora_rega_1 > hora_atual) {
    return (hora_rega_1 - hora_atual) * 60;
  }
  if (hora_rega_2 > hora_atual) {
    return (hora_rega_2 - hora_atual) * 60;
  }

  // Já passou das duas de hoje → próxima é em 2 dias na hora 1
  {
    DateTime proxima(now.year(), now.month(), now.day(), hora_rega_1, 0, 0);
    proxima = proxima + TimeSpan(2, 0, 0, 0);

    long diff_seg = proxima.unixtime() - now.unixtime();
    if (diff_seg < 60) diff_seg = 60;

    int minutos = diff_seg / 60;
    return minutos;
  }
}

bool rega(int tempo_rega) {// Função responsável por regar
  //Verifica se é o momento de regar.
  if (!flag_rega) {
    return false;
  }

  // Primeira chamada: ainda não entrei no "sono da rega"
  if (!flag_sono_rega) {
  sensor_fluxo_estado = false; 
  houve_fluxo_na_rega = false; 
  rega_inibida_umidade = false;

  int temperatura = 0;
  int umidade = 0;

  if (le_temp_umidade(temperatura, umidade)) {
    Serial.print("Temperatura atual: ");
    Serial.print(temperatura);
    Serial.println(" C");

    Serial.print("Umidade atual: ");
    Serial.print(umidade);
    Serial.println(" %");

    prefs.begin("maf2000", false);
    prefs.putInt("temperatura", temperatura);
    prefs.putInt("umidade", umidade);
    prefs.end();

    if (umidade >= LIMITE_UMIDADE_CHUVA) {
      Serial.println("Umidade >= 90%. Rega inibida por possibilidade de chuva.");
      displayMensagem("Rega inibida", "Umidade alta detectada", "Possivel chuva", "Aguardando proxima");

      rega_inibida_umidade = true;
      flag_sono_rega = false;
      flag_rega = false;
      houve_fluxo_na_rega = false;

      return false;
    }
    } else {
      Serial.println("Falha ao ler DHT11 antes da rega. Prosseguindo com a rega.");
    }

    flag_sono_rega = true;

    if(valvulaAbrir()){
      Serial.println("Abrir Válvula");
      Serial.println("Válvula solenoide biestável foi acionada!!");
      Serial.print("Vou dormir ");
      Serial.print(tempo_rega);
      Serial.println(" minutos enquanto a rega não acaba");
      Serial.println("Lendo Sensor de fluxo");
      delay(1000);
      
      int leituraFluxo = digitalRead(sensor_fluxo);
      Serial.print("Leitura sensor fluxo pos-abertura: ");
      Serial.println(leituraFluxo);

      if (leituraFluxo == LOW) {        // contato fechado = fluxo
        houve_fluxo_na_rega = true;
        } else {
            houve_fluxo_na_rega = false;  // contato aberto = sem fluxo
        }
      esp_sleep_enable_timer_wakeup((uint64_t)tempo_rega * uS_TO_MIN_FACTOR);
      Serial.flush();
      esp_deep_sleep_start();
      return true;
    }else{
      Serial.println("Válvula solenoide biestável não foi acionada!!");
      Serial.println("Rega cancelada");

      flag_sono_rega = false;
      flag_rega      = false;
      houve_fluxo_na_rega = false;
      return false;
    }  
  }

  // Segunda chamada: já dormi pelo tempo_rega
  if (flag_sono_rega) {
  // Pulso na válvula desligando válvula
    if(!valvulaFechar()){
      flag_sono_rega = false;
      flag_rega = false;
      Serial.println("Falha crítica.");
      return false;
    }
    

    flag_sono_rega = false;
    flag_rega = false; 
    return true;         // sinaliza para o código que a rega terminou
  }

  return false;
}

void configuracao_pag_web(){// Configuração das rotas WEB

  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request){
    String page = montarPaginaGeral();
    request->send(200, "text/html; charset=utf-8", page);
  });

  server.on("/geral", HTTP_GET, [](AsyncWebServerRequest *request){
    String page = montarPaginaGeral();
    request->send(200, "text/html; charset=utf-8", page);
  });
  server.on("/dados_gravados", HTTP_GET, [](AsyncWebServerRequest *request){
    String page = montarPaginaDados();
    request->send(200, "text/html; charset=utf-8", page);
  });
 
  server.on("/configuracoes", HTTP_GET, [](AsyncWebServerRequest *request){
    AsyncWebServerResponse *response =
        request->beginResponse_P(200, "text/html; charset=utf-8",
                                 configuracoes, processor);

    response->addHeader("Cache-Control", "no-store");

    request->send(response);
  });

 

  server.on("/pg_save", HTTP_POST, [](AsyncWebServerRequest *request){ // Rota que recebe e salva as informações configuradas via APP

    String dias       = request->getParam("dias", true)->value();
    String fre_rega_d = request->getParam("fre_rega_d", true)->value();

    String bateria = request->hasParam("bateria", true) // 1 para usando bateria  e 0 para usando fonte externa
                  ? request->getParam("bateria", true)->value()
                  : "0";

    String tipo_bateria = request->hasParam("tipo_bateria", true)// 7 para bateria itnerna e 12 para bateria externa
                  ? request->getParam("tipo_bateria", true)->value()
                  : "7"; // default interno

    String h1 = request->getParam("hora_rega_1", true)->value();

    String h2 = request->hasParam("hora_rega_2", true)
                  ? request->getParam("hora_rega_2", true)->value()
                  : "";

    String tempo_rega = request->getParam("tempo_rega", true)->value();
    String rtc_set    = request->getParam("rtc_set", true)->value();

    String internet   = request->getParam("internet", true)->value();

    String wifi_ssid = request->hasParam("wifi_ssid", true)
                  ? request->getParam("wifi_ssid", true)->value()
                  : "";

    String wifi_pass = request->hasParam("wifi_pass", true)
                  ? request->getParam("wifi_pass", true)->value()
                  : "";

    String link_db = request->hasParam("link_db", true)
                  ? request->getParam("link_db", true)->value()
                  : "";
    
    Serial.println("=== DADOS RECEBIDOS ===");
    Serial.println("dias: " + dias);
    Serial.println("fre_rega_d: " + fre_rega_d);
    Serial.println("h1: " + h1);
    Serial.println("h2: " + h2);
    Serial.println("tempo_rega: " + tempo_rega);
    Serial.println("rtc_set: " + rtc_set);
    Serial.println("internet: " + internet);
    Serial.println("bateria: " + bateria);
    Serial.println("tipo bateria: " + tipo_bateria);
    Serial.println("wifi_ssid: " + wifi_ssid);
    Serial.println("wifi_pass: " + wifi_pass);
    Serial.println("link_db: " + link_db);

    prefs.begin("maf2000", false);
    prefs.putString("wifi_ssid", wifi_ssid);
    prefs.putString("wifi_pass", wifi_pass);
    prefs.putInt("hora_rega_1", h1.toInt());
    prefs.putInt("dias_rega", dias.toInt());
    prefs.putInt("modo_opera", internet.toInt());
    prefs.putInt("hora_rega_2", h2.toInt());
    prefs.putInt("frequencia_rega", fre_rega_d.toInt());
    prefs.putInt("bateria", bateria.toInt());
    prefs.putInt("tipo_bateria", tipo_bateria.toInt());
    prefs.putString("link_db", link_db);
    prefs.putInt("tempo_rega", tempo_rega.toInt());

    prefs.end();
    ajustar_RTC(rtc_set);
    request->send(200, "text/plain", "Configuracoes recebidas!");
    displayMensagem("Configuracoes recebidas!", "Para iniciar o funcionamento de fato desative o botao configurar e resete o dispositivo");
  });
}

void setup() {
  delay(1000);
  Serial.begin(115200);
  pinMode(pin_critico, OUTPUT);
  pinMode(led_interacao, OUTPUT);
  pinMode(led_interacao_erro, OUTPUT);
  pinMode(btn_config, INPUT_PULLUP);
  pinMode(sensor_fluxo, INPUT_PULLUP);
  pinMode(pin_abre_valv, OUTPUT);
  pinMode(pin_fecha_valv, OUTPUT);
  digitalWrite(pin_critico, LOW);
  digitalWrite(led_interacao_erro, LOW);
  digitalWrite(led_interacao, LOW);
  digitalWrite(pin_abre_valv, LOW);
  digitalWrite(pin_fecha_valv, LOW);
  rega_inibida_umidade = false;
  analogSetWidth(12);
  analogSetPinAttenuation(ADC_BAT, ADC_11db);

  Wire.begin();
  

  if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    Serial.println("Falha ao inicializar o display OLED!");
  }

  display.clearDisplay();
  display.setTextSize(1);              // Tamanho básico do texto
  display.setTextColor(SSD1306_WHITE); // Cor do texto (branco)
  display.setCursor(0, 0);

  display.println("Irrigador ESP32");
  display.println("Inicializando...");
  display.display();                   // Envia para o display
  delay(1000);                         // Tempo de leitura


  if(flag_sono_rega == false && digitalRead(sensor_fluxo) == LOW){
    valvulaFechar();
  }
  if (falha_critica) {
    Serial.println("Falha crítica logo ao iniciar. Entrando em modo de erro permanente.");
    return;   // termina o setup, o loop() começa e cai no while(falha_critica)
  }
  int modo_opera; //-1: não config, 0: sem internet, 1: com internet
  prefs.begin("maf2000", true);
  modo_opera = prefs.getInt("modo_opera",-1);
  prefs.end();

  if (!rtc.begin()) {
    Serial.println("Erro ao iniciar RTC");
    pisca_led_erro();
    Serial.println("Vou dar reboot");
    Serial.flush();
    esp_restart();
  }


  ++bootCount;
  Serial.println("Boot number: " + String(bootCount));
  causa_deep_sleep_guardada = print_get_wakeup_reason();

  

  // configura pull-up do domínio RTC (vale durante deep sleep)
  rtc_gpio_init(WAKEUP_GPIO);
  rtc_gpio_set_direction(WAKEUP_GPIO, RTC_GPIO_MODE_INPUT_ONLY);
  rtc_gpio_pullup_en(WAKEUP_GPIO);
  rtc_gpio_pulldown_dis(WAKEUP_GPIO);
  rtc_gpio_hold_en(WAKEUP_GPIO);

  

  if(digitalRead(btn_config) == HIGH && modo_opera == -1){
    Serial.println("A configuração está incompleta! Vou rebotar, então ative o pino de configuração");
    displayMensagem("Configuração incompleta.", "O dispositivo está sendo reiniciado", "Ative o modo de configuração");
    pisca_led_erro();
    esp_restart();
  }

  if (digitalRead(btn_config) == LOW){
    flag_rega          = false;
    flag_sono_rega     = false;
    houve_fluxo_na_rega = false;
    falha_critica = false;
    rega_inibida_umidade = false;

    const char* ap_ssid = "MAF2000"; 
    const char* ap_pass = "12345678";
    WiFi.mode(WIFI_AP);
    bool ap_ok = WiFi.softAP(ap_ssid, ap_pass);
    delay(500);
    if(ap_ok == false){
      pisca_led_erro();
      Serial.print("Não foi possível ativar o ciclo de configuração");
      Serial.print("Vou rebotar em alguns segundos para tentar resolver o problema");
      displayMensagem("Não foi possível iniciar a conexão", "O dispositivo está sendo reiniciado", "Ative o modo de configuração");
      pisca_led_erro();
      Serial.flush();
      esp_restart();
    }
    Serial.println("AP Iniciado!");
    Serial.print("IP do AP: ");
    Serial.println(WiFi.softAPIP());
    configuracao_pag_web();
    server.begin();
    Serial.println("Servidor Async iniciado.");

    displayMensagem("Modo de configuracao iniciado.", "Programe seu dispositivo via APP MAF2000","LED de interação ativo");
    return;
  }

  displayMensagem("Irrigador ESP32","Sistema pronto","Processando eventos");


  if(digitalRead(btn_config) == HIGH && modo_opera == 0){
    if (causa_deep_sleep_guardada != ESP_SLEEP_WAKEUP_TIMER) {
      DateTime now = rtc.now();
      float hora_atual = now.hour() + now.minute()/60.0f;
      int tempo_dormindo_min = calc_prox_sono(hora_atual);

      if (!rtc.isrunning()) {
        Serial.print("Configuração incompleta, data não configurada ou acabou a pilha do relógio. Configure novamente ativando o pino de configuração ou insira uma pilha nova");
        Serial.print("Vou rebotar em alguns segundos,");
        displayMensagem("Configuração incompleta.", "O dispositivo está sendo reiniciado", "Ative o modo de configuração");
        pisca_led_erro();
        Serial.flush();
        esp_restart();
      }

      prefs.begin("maf2000", true);
      String dataHoraUltRega = prefs.getString("ultima_rega", "Sem rega ainda");
      int temperatura = 0;
      int umidade     = 0;
      le_temp_umidade(temperatura, umidade);
      prefs.end();
      
      if (tempo_dormindo_min < 1) {
        tempo_dormindo_min = 1;  // nunca dormir 0 minutos
      }

      String proxRegaEm = String(tempo_dormindo_min) + " min";
      mostrarTelaResumo(dataHoraUltRega, temperatura, umidade, proxRegaEm);
      delay(5000); // 5 segundos



      Serial.print("Ainda não está na hora de regar, vou dormir ");
      Serial.print(tempo_dormindo_min);
      Serial.println("minutos");
      delay(2000);

      if (falha_critica) {
        Serial.println("Falha crítica ativa, cancelando deep sleep.");
        } else {
          esp_sleep_enable_timer_wakeup((uint64_t)tempo_dormindo_min * uS_TO_MIN_FACTOR);
          esp_sleep_enable_ext0_wakeup(WAKEUP_GPIO, 0);
          Serial.flush();
          esp_deep_sleep_start();
        }
    }
    flag_rega = true;
  }
  
  if(digitalRead(btn_config) == HIGH && modo_opera == 1){ 
    if (causa_deep_sleep_guardada != ESP_SLEEP_WAKEUP_TIMER) {
      DateTime now = rtc.now();
      float hora_atual = now.hour() + now.minute()/60.0f;
      int tempo_dormindo_min = calc_prox_sono(hora_atual);

      if (!rtc.isrunning()) {
        Serial.print("Configuração incompleta, data não configurada ou acabou a pilha do relógio. Configure novamente ativando o pino de configuração ou insira uma pilha nova");
        Serial.print("Vou rebotar em alguns segundos,");
        displayMensagem("Configuração incompleta.", "O dispositivo está sendo reiniciado", "Ative o modo de configuração");
        pisca_led_erro();
        Serial.flush();
        esp_restart();
      }

      prefs.begin("maf2000", true);
      String dataHoraUltRega = prefs.getString("ultima_rega", "Sem rega ainda");
      int temperatura = 0;
      int umidade     = 0;
      le_temp_umidade(temperatura, umidade);
      prefs.end();
      
      if (tempo_dormindo_min < 1) {
        tempo_dormindo_min = 1;  // nunca dormir 0 minutos
      }

      String proxRegaEm = String(tempo_dormindo_min) + " min";
      mostrarTelaResumo(dataHoraUltRega, temperatura, umidade, proxRegaEm);
      delay(5000); 



      Serial.print("Ainda não está na hora de regar, vou dormir ");
      Serial.print(tempo_dormindo_min);
      Serial.println("minutos");
      delay(2000);
      if (falha_critica) {
        Serial.println("Falha crítica ativa, cancelando deep sleep.");
        } else {
          esp_sleep_enable_timer_wakeup((uint64_t)tempo_dormindo_min * uS_TO_MIN_FACTOR);
          esp_sleep_enable_ext0_wakeup(WAKEUP_GPIO, 0);
          Serial.flush();
          esp_deep_sleep_start();
        }
    }
    flag_rega = true;

    if (flag_sono_rega) {
      prefs.begin("maf2000", true);
      String wifi_ssid = prefs.getString("wifi_ssid");
      String wifi_pass = prefs.getString("wifi_pass");
      prefs.end();
      Serial.print("Vou me conectar na rede: ");
      Serial.println(wifi_ssid);
      Serial.print("Senha: ");
      Serial.println(wifi_pass);
      WiFi.mode(WIFI_STA);        // modo cliente
      WiFi.begin(wifi_ssid.c_str(), wifi_pass.c_str());     // inicia conexão
      Serial.print("Conectando");
      unsigned long startAttemptTime = millis();
      const unsigned long timeoutConexao = 15000;   // 15 segundos

      while (WiFi.status() != WL_CONNECTED && (millis() - startAttemptTime) < timeoutConexao) {
        Serial.print('.');
        digitalWrite(led_interacao, HIGH);
        delay(250);
        digitalWrite(led_interacao, LOW);
        delay(250);
      }
      Serial.println();

      if (WiFi.status() == WL_CONNECTED) {
        digitalWrite(led_interacao, LOW);
        flag_rede_ativa = true;
        flag_erro_rede = false;
        Serial.println("\nConectado!");
        Serial.print("IP: ");
        Serial.println(WiFi.localIP());
      } else {
        Serial.println("");
        Serial.println("Não foi possível conectar, então não será enviado dados atualizados ao Firebase.");
        Serial.println("O ocorrido será adicionado ao log do SD CARD.");
        flag_rede_ativa = false;
        flag_erro_rede = true;
      }
    }else{
      flag_rede_ativa = false;
      Serial.println("Primeira etapa da rega: não vou conectar ao WiFi agora. Conectarei após o tempo de rega.");
    }
  }

  
  if (!SD.begin(CS_PIN)) {
  Serial.println("erro ao iniciar SD");
  pisca_led_erro();
  } else {
  Serial.println("SD OK!");
  }


  if (!rtc.isrunning()) {
    Serial.print("Configuração incompleta, data não configurada ou acabou a pilha do relógio. Configure novamente ativando o pino de configuração ou insira uma pilha nova");
    Serial.print("Vou rebotar em alguns segundos,");
    displayMensagem("Configuração incompleta.", "O dispositivo está sendo reiniciado", "Ative o modo de configuração");
    pisca_led_erro();
    Serial.flush();
    esp_restart();
  }

}

void loop() {
  //Tratamento de falha crítica permanente
  while (falha_critica == true){
    digitalWrite(pin_critico, HIGH);  // ACIONA BUZZER
    delay(1000);
    digitalWrite(pin_critico, LOW); // DESLIGA BUZZER (se for ativo em LOW)
    delay(1000);

    pisca_led_erro();
    valvulaFechar_falha(); // tenta recuperar periodicamente
  }

  //Ciclo de rega
  if (flag_rega == true) {
    DateTime now = rtc.now();
    float hora_atual;
    int tempo_dormindo_min;
    int modo_opera;  // 1: c/ internet  0: s/ internet
    int tempo_rega;

    // Lê configurações básicas
    prefs.begin("maf2000", false);
    modo_opera = prefs.getInt("modo_opera", -1);
    tempo_rega = prefs.getInt("tempo_rega", 15);
    prefs.end();

    if (tempo_rega < 1)  tempo_rega = 1;
    if (tempo_rega > 60) tempo_rega = 60;

    // Executa a rega (abre/dorme/fecha válvula)
    bool teste_rega = rega(tempo_rega);

    // Se modo internet e rede já conectada, envia / sincroniza Firebase
    if (modo_opera == 1 && flag_rede_ativa == true) {
      firebase(true, true);
    }

    // Grava log no SD + atualiza ultima_rega, temperatura, umidade em Preferences
    grava_exibe_log(teste_rega);

    // Se virou falha crítica durante a rega, não dorme
    if (falha_critica) {
      Serial.println("Entrando em modo de falha crítica. Não vou dormir.");
      return;
    }

    // Calcula próxima rega (após essa execução)
    now        = rtc.now();
    hora_atual = now.hour() + now.minute()/60.0f;
    tempo_dormindo_min = calc_prox_sono(hora_atual);

    Serial.print("Tempo até próxima rega: ");
    Serial.println(tempo_dormindo_min);
    Serial.print("Vou dormir ");

    if (tempo_dormindo_min < 1) {
      tempo_dormindo_min = 1;  // nunca dormir 0 minutos
    }

    // Lê dados já atualizados nas Preferences
    prefs.begin("maf2000", true);
    String dataHoraUltRega = prefs.getString("ultima_rega", "Sem rega ainda");
    int temperatura        = prefs.getInt("temperatura", -127);
    int umidade            = prefs.getInt("umidade", -1);
    prefs.end();

    String proxRegaEm = String(tempo_dormindo_min) + " min";

    // Atualiza display conforme resultado da rega
    if (teste_rega) {
      mostrarTelaResumo(dataHoraUltRega, temperatura, umidade, proxRegaEm);
    } else if (rega_inibida_umidade) {
      displayMensagem("Rega inibida",
                      "Umidade >= 90%",
                      "Possivel chuva",
                      "Prox: " + proxRegaEm);
    } else {
      displayMensagem("Rega falhou",
                      "Verifique registro geral",
                      "Prox: " + proxRegaEm);
    }

    delay(5000); // tempo para o usuário enxergar a tela

    // Configura o deep sleep
    esp_sleep_enable_timer_wakeup((uint64_t)tempo_dormindo_min * uS_TO_MIN_FACTOR);
    esp_sleep_enable_ext0_wakeup(WAKEUP_GPIO, 0);
    Serial.flush();
    esp_deep_sleep_start();
  }

  // Modo configuração interação com o usuário
  if (digitalRead(btn_config) == LOW) {
    pisca_led_interacao();
  }

  // 4) Segurança extra: se detectar fluxo indevido fora da rega, tenta fechar
  if (digitalRead(sensor_fluxo) == LOW && flag_sono_rega == false) {
    valvulaFechar();
  }
}
