const MAX_LINHAS = 10; // Limite de histórico no navegador
const TAXA_ATUALIZACAO_MS = 1000; // Taxa de atualização em milissegundos 

// --- Variáveis Globais para o Gráfico ---
let graficoValores;
const historicoTempo = [];
const historicoCidade = [];
const historicoBateria = [];
const historicoDissipacao = []; // Array para o gráfico de dissipação
 
// --- Inicialização das Tabelas ---
function inicializarTabelasComZeros(idTabela) {
  const tbody = document.querySelector(`#${idTabela} tbody`);
  if (!tbody) return;

  tbody.innerHTML = ''; // Garante que a tabela está vazia antes de começar

  for (let i = 0; i < MAX_LINHAS; i++) {
    const linhaZero = document.createElement('tr');
    linhaZero.innerHTML = `
      <td>0.00</td>
      <td>0.00</td>
      <td>0.00</td>
    `;
    tbody.appendChild(linhaZero); // Adiciona as 10 linhas iniciais
  }
}
// --- Inicialização do Gráfico ---
function inicializarGrafico() {
  const ctx = document.getElementById('grafico_valores');
  if (!ctx) return;

  // Configuração global de estilos para o tema escuro
  Chart.defaults.color = '#94a3b8';
  Chart.defaults.font.family = 'system-ui, -apple-system, sans-serif';

  graficoValores = new Chart(ctx, {
    type: 'line',
    data: {
      labels: historicoTempo,
      datasets: [
        { 
          label: 'Cidade (mW)', 
          data: historicoCidade, 
          borderColor: '#3b82f6', 
          tension: 0.3, pointRadius: 0, borderWidth: 2,
          yAxisID: 'y' // Associa ao eixo da esquerda
        },
        { 
          label: 'Bateria (mW)', 
          data: historicoBateria, 
          borderColor: '#10b981', 
          tension: 0.3, pointRadius: 0, borderWidth: 2,
          yAxisID: 'y' // Associa ao eixo da esquerda
        },
        { 
          label: 'Dissipação (%)', 
          data: historicoDissipacao, 
          borderColor: '#f59e0b', 
          tension: 0.3, pointRadius: 0, borderWidth: 2,
          yAxisID: 'y1' // Associa ao eixo da direita
        }
      ]
    },
    options: {
      responsive: true,
      maintainAspectRatio: false,
      animation: false,
      scales: {
        x: { grid: { color: '#334155' } },
        y: { 
          type: 'linear',
          display: true,
          position: 'left',
          grid: { color: '#334155' },
          beginAtZero: true 
        },
        y1: { 
          type: 'linear',
          display: true,
          position: 'right', // Coloca este eixo do lado direito
          beginAtZero: true,
          max: 100, // Força o topo do gráfico a ser 100%
          grid: { drawOnChartArea: false } // Evita que as linhas de grelha se cruzem
        }
      },
      plugins: {
        legend: { labels: { color: '#e2e8f0', usePointStyle: true } }
      }
    }
  });
}

// --- Funções de Manipulação de Dados ---
function atualizarGrafico(pCidade, pBateria, pDissipacao) {
  if (!graficoValores) return;

  const agora = new Date();
  const hora = `${agora.getHours().toString().padStart(2, '0')}:${agora.getMinutes().toString().padStart(2, '0')}:${agora.getSeconds().toString().padStart(2, '0')}`;

  // Adiciona os dados ao histórico
  historicoTempo.push(hora);
  historicoCidade.push(pCidade);
  historicoBateria.push(pBateria);
  historicoDissipacao.push(pDissipacao);

  // Remove dados antigos para manter o limite de linhas e não sobrecarregar
  if (historicoTempo.length > MAX_LINHAS) {
    historicoTempo.shift();
    historicoCidade.shift();
    historicoBateria.shift();
    historicoDissipacao.shift();
  }

  graficoValores.update();
}

function adicionarLinha(idTabela, v, c, p) {
  const tbody = document.querySelector(`#${idTabela} tbody`);
  if (!tbody) return; 

  const novaLinha = document.createElement('tr');
  novaLinha.innerHTML = `
    <td>${v}</td>
    <td>${c}</td>
    <td>${p}</td>
  `;
  
  // Insere no topo
  tbody.prepend(novaLinha);

  // Remove a última linha caso ultrapasse o limite
  if (tbody.children.length > MAX_LINHAS) {
    tbody.removeChild(tbody.lastElementChild);
  }

  novaLinha.classList.add('linha-nova');
}

// --- Loop Principal ---
function atualizar() {
  fetch('/dados')
    .then(res => res.json())
    .then(data => {
      
      // 1. Força a leitura a ser sempre positiva e em formato numérico
      const potBateriaPositiva = Math.abs(Number(data.p_master));
      const potCidade = Number(data.p_mini1);

      // 2. Atualiza as tabelas com os valores reais
      adicionarLinha('tabela_cidade', Number(data.v_mini1).toFixed(2), Number(data.c_mini1).toFixed(2), potCidade.toFixed(2));
      adicionarLinha('tabela_bateria', Number(data.v_master).toFixed(2), Number(data.c_master).toFixed(2), potBateriaPositiva.toFixed(2));

      // 3. Atualiza o gráfico
      atualizarGrafico(potCidade, potBateriaPositiva, data.dissipacao);

      // 4. Lógica de Estados
      if (data.estado) {
        const estadoTxt = document.getElementById('estado_txt');
        estadoTxt.innerText = data.estado;

        switch (data.estado) {
          case "APAGAO": estadoTxt.style.color = "#ef4444"; break; 
          case "NOITE":  estadoTxt.style.color = "#3b82f6"; break; 
          case "MANHA":  estadoTxt.style.color = "#eab308"; break; 
          case "TARDE":  estadoTxt.style.color = "#f97316"; break; 
          default:       estadoTxt.style.color = "#10b981"; break; 
        }
      }
    })
    .catch(err => console.error("Erro ao obter dados da telemetria:", err));
}

// --- Inicialização do fluxo ---
inicializarGrafico();

// Inicializa as tabelas a zeros antes do primeiro loop
inicializarTabelasComZeros('tabela_cidade');
inicializarTabelasComZeros('tabela_bateria');

// Inicia o processo periódico
setInterval(atualizar, TAXA_ATUALIZACAO_MS);