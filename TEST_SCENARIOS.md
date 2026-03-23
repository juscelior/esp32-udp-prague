# Cenários de Teste — Avaliação do Prague/L4S em Dispositivos IoT (ESP32)

Este documento descreve os cenários experimentais utilizados na dissertação de mestrado para avaliar o desempenho do algoritmo de controle de congestionamento Prague, conforme especificado pela arquitetura L4S (Low Latency, Low Loss, Scalable Throughput), em um dispositivo IoT baseado no microcontrolador ESP32.

---

## 1. Descrição do Experimento

### 1.1 Objetivo

Avaliar o impacto de quatro variáveis independentes — **ECN do cliente**, **algoritmo de controle de congestionamento (CC)**, **disciplina de fila (qdisc)** e **configuração ECN do gateway** — sobre métricas de desempenho de rede (latência, jitter, taxa de perda, throughput e marcações ECN) em um enlace IoT real utilizando ESP32 como nó transmissor.

### 1.2 Variáveis Independentes

| Variável | Valores | Descrição |
|----------|---------|-----------|
| **ECN do cliente** | `ecn0`, `ecn1` | Configuração no ESP32 via `ECN_SENDER_ENABLE`: `ecn0` = Not-ECT (00), `ecn1` = ECT(1) (01). |
| **Algoritmo CC** | `prague`, `cubic`, `reno` | Algoritmo de controle de congestionamento configurado no gateway. **Prague** é o algoritmo L4S sob avaliação; **Cubic** e **Reno** são baselines clássicos da pilha TCP/IP. |
| **Qdisc** | `fq_codel`, `dualpi2` | Disciplina de fila no gargalo da rede. **fq_codel** é um AQM clássico amplamente implantado; **DualPI2** é o AQM L4S de fila dupla projetado para coexistência entre tráfego clássico e escalável. |
| **ECN (gateway)** | `0`, `1`, `2` | Codepoint ECN documentado no cenário de rede: `0` = Not-ECT, `1` = ECT(1), `2` = ECT(0). |

### 1.3 Variáveis Dependentes (Métricas)

| Métrica | Unidade | Descrição |
|---------|---------|-----------|
| RTT (Round-Trip Time) | ms | Latência fim-a-fim medida pelo cliente Prague. |
| Jitter | ms | Variação do RTT entre pacotes consecutivos. |
| Throughput | Mbps | Vazão efetiva medida no receptor. |
| Taxa de perda | % | Proporção de pacotes perdidos em relação ao total enviado. |
| Marcações ECN | contagem | Número de pacotes que receberam marcação CE (Congestion Experienced) pelo AQM. |
| Goodput | razão | Razão entre dados úteis recebidos e dados totais enviados. |
| Janela de congestionamento | pacotes | Tamanho da janela de congestionamento calculada pelo Prague CC. |

### 1.4 Parâmetros Fixos

| Parâmetro | Valor | Descrição |
|-----------|-------|-----------|
| Cenário de carga | `SCENARIO_HIGH` | Payload de 1383 bytes extras, duração de 180 s. |
| `MAX_WINDOW_ESP32` | `-1` (sem limite) | Sem limite local de janela; utiliza apenas a janela do Prague CC. |
| `MAX_BURST_ESP32` | `5` | Limite de burst por ciclo de envio. |
| Placa | ESP32-DevKit v1 | Microcontrolador com Wi-Fi 802.11 b/g/n. |
| Receptor | Linux (UDP Prague Receiver) | Servidor C++ baseado no fork `juscelior/udp_prague`. |
| Porta UDP | `5005` | Porta do receptor UDP Prague. |

### 1.5 Matriz Fatorial

A combinação completa das 4 variáveis independentes resulta em **2 × 3 × 2 × 3 = 36 cenários**.

---

## 2. Cenários de Teste

### 2.1 Grupo Prague (T01–T06)

O algoritmo Prague é o CC escalável da arquitetura L4S. Estes cenários avaliam seu comportamento combinado com cada qdisc e configuração ECN. Para cada cenário base (T01–T18) há duas variantes do **ECN do cliente**: `ecn0` (Not-ECT) e `ecn1` (ECT(1)).

---

#### T01 — Prague + fq_codel + ECN 0 (Not-ECT)

| Atributo | Valor |
|----------|-------|
| **ID** | T01 |
| **CC** | `prague` |
| **Qdisc** | `fq_codel` |
| **ECN** | `0` (Not-ECT) |
| **ECN cliente** | `ecn0` (Not-ECT) / `ecn1` (ECT(1)) |
| **Pasta (cliente ecn0)** | `experiments/t01-ecn-sender-ect0-prague-fqcodel-ecn0/` |
| **Pasta (cliente ecn1)** | `experiments/t01-ecn-sender-ect1-prague-fqcodel-ecn0/` |
| **Status** | [ ] Pendente |

**Descrição**: O remetente Prague opera sobre fq_codel sem sinalização ECN. Como os pacotes são marcados como Not-ECT, o AQM fq_codel não pode marcar CE — resta apenas o descarte (drop) como sinal de congestionamento. Este cenário avalia como o Prague reage a perdas puras sem feedback ECN.

**Hipótese**: O Prague sem ECN habilitado no remetente se comportará de forma semelhante a um CC clássico baseado em perda, com RTT elevado e possível bufferbloat, pois não receberá marcações ECN do AQM.

---

#### T02 — Prague + fq_codel + ECN 1 (ECT-1)

| Atributo | Valor |
|----------|-------|
| **ID** | T02 |
| **CC** | `prague` |
| **Qdisc** | `fq_codel` |
| **ECN** | `1` (ECT-1) |
| **ECN cliente** | `ecn0` (Not-ECT) / `ecn1` (ECT(1)) |
| **Pasta (cliente ecn0)** | `experiments/t02-ecn-sender-ect0-prague-fqcodel-ecn1/` |
| **Pasta (cliente ecn1)** | `experiments/t02-ecn-sender-ect1-prague-fqcodel-ecn1/` |
| **Status** | [ ] Pendente |

**Descrição**: O remetente Prague marca pacotes como ECT(1), o codepoint L4S. O fq_codel, sendo um AQM clássico, pode marcar CE ou descartar pacotes conforme sua lógica interna. Este cenário testa a interação entre sinalização L4S e um AQM que não implementa fila dupla.

**Hipótese**: O fq_codel marcará CE com frequência proporcional ao atraso na fila, e o Prague responderá reduzindo sua janela. Porém, como fq_codel não diferencia ECT(1) de ECT(0), a resposta pode ser mais agressiva (marcação clássica) do que o ideal L4S.

---

#### T03 — Prague + fq_codel + ECN 2 (ECT-0)

| Atributo | Valor |
|----------|-------|
| **ID** | T03 |
| **CC** | `prague` |
| **Qdisc** | `fq_codel` |
| **ECN** | `2` (ECT-0) |
| **ECN cliente** | `ecn0` (Not-ECT) / `ecn1` (ECT(1)) |
| **Pasta (cliente ecn0)** | `experiments/t03-ecn-sender-ect0-prague-fqcodel-ecn2/` |
| **Pasta (cliente ecn1)** | `experiments/t03-ecn-sender-ect1-prague-fqcodel-ecn2/` |
| **Status** | [ ] Pendente |

**Descrição**: O remetente Prague utiliza ECT(0), o codepoint ECN clássico (RFC 3168). O fq_codel trata ECT(0) da mesma forma que ECT(1) na prática — ambos permitem marcação CE ao invés de descarte.

**Hipótese**: Comportamento semelhante a T02. A comparação entre T02 e T03 permite verificar se o fq_codel trata ECT(0) e ECT(1) de forma diferenciada.

---

#### T04 — Prague + DualPI2 + ECN 0 (Not-ECT)

| Atributo | Valor |
|----------|-------|
| **ID** | T04 |
| **CC** | `prague` |
| **Qdisc** | `dualpi2` |
| **ECN** | `0` (Not-ECT) |
| **ECN cliente** | `ecn0` (Not-ECT) / `ecn1` (ECT(1)) |
| **Pasta (cliente ecn0)** | `experiments/t04-ecn-sender-ect0-prague-dualpi2-ecn0/` |
| **Pasta (cliente ecn1)** | `experiments/t04-ecn-sender-ect1-prague-dualpi2-ecn0/` |
| **Status** | [ ] Pendente |

**Descrição**: O remetente Prague envia pacotes Not-ECT sobre DualPI2. Como os pacotes não são ECN-capable, o DualPI2 os classifica na fila clássica e só pode sinalizar congestionamento por descarte.

**Hipótese**: O Prague sem ECN sobre DualPI2 se comportará como um fluxo clássico na fila clássica do DualPI2, com RTT e perda semelhantes a um CC clássico. A fila L4S permanecerá ociosa.

---

#### T05 — Prague + DualPI2 + ECN 1 (ECT-1) ★ Cenário L4S Ideal

| Atributo | Valor |
|----------|-------|
| **ID** | T05 |
| **CC** | `prague` |
| **Qdisc** | `dualpi2` |
| **ECN** | `1` (ECT-1) |
| **ECN cliente** | `ecn0` (Not-ECT) / `ecn1` (ECT(1)) |
| **Pasta (cliente ecn0)** | `experiments/t05-ecn-sender-ect0-prague-dualpi2-ecn1/` |
| **Pasta (cliente ecn1)** | `experiments/t05-ecn-sender-ect1-prague-dualpi2-ecn1/` |
| **Status** | [ ] Pendente |

**Descrição**: **Cenário de referência L4S.** O remetente Prague marca pacotes como ECT(1) e o DualPI2 os direciona para a fila L4S (low-latency). O DualPI2 aplica marcação CE proporcional e escalável, e o Prague ajusta sua taxa com base nesse feedback granular.

**Hipótese**: Este é o cenário com melhor desempenho esperado — RTT consistentemente baixo, jitter mínimo, perda próxima de zero e throughput elevado. A janela de congestionamento do Prague deve oscilar de forma suave e estável, demonstrando o comportamento escalável previsto pela arquitetura L4S (RFC 9332, RFC 9331).

---

#### T06 — Prague + DualPI2 + ECN 2 (ECT-0)

| Atributo | Valor |
|----------|-------|
| **ID** | T06 |
| **CC** | `prague` |
| **Qdisc** | `dualpi2` |
| **ECN** | `2` (ECT-0) |
| **ECN cliente** | `ecn0` (Not-ECT) / `ecn1` (ECT(1)) |
| **Pasta (cliente ecn0)** | `experiments/t06-ecn-sender-ect0-prague-dualpi2-ecn2/` |
| **Pasta (cliente ecn1)** | `experiments/t06-ecn-sender-ect1-prague-dualpi2-ecn2/` |
| **Status** | [ ] Pendente |

**Descrição**: O remetente Prague marca pacotes como ECT(0). O DualPI2 classifica pacotes ECT(0) na fila clássica (não na fila L4S), aplicando marcação CE no estilo PI clássico.

**Hipótese**: Mesmo com Prague no remetente, o DualPI2 tratará o fluxo como clássico. O RTT será significativamente maior que em T05. A comparação T05 vs T06 isola o efeito do codepoint ECT(1) no encaminhamento para a fila L4S.

---

### 2.2 Grupo Cubic (T07–T12)

Cubic é o algoritmo CC padrão do Linux moderno. Serve como **baseline clássico** para comparação com o Prague.

---

#### T07 — Cubic + fq_codel + ECN 0 (Not-ECT)

| Atributo | Valor |
|----------|-------|
| **ID** | T07 |
| **CC** | `cubic` |
| **Qdisc** | `fq_codel` |
| **ECN** | `0` (Not-ECT) |
| **ECN cliente** | `ecn0` (Not-ECT) / `ecn1` (ECT(1)) |
| **Pasta (cliente ecn0)** | `experiments/t07-ecn-sender-ect0-cubic-fqcodel-ecn0/` |
| **Pasta (cliente ecn1)** | `experiments/t07-ecn-sender-ect1-cubic-fqcodel-ecn0/` |
| **Status** | [ ] Pendente |

**Descrição**: **Baseline clássico.** Cubic sem ECN sobre fq_codel. Representa a configuração mais comum em redes atuais. O fq_codel gerencia a fila usando descarte como único sinal de congestionamento.

**Hipótese**: Latência moderada graças ao fq_codel (que combate bufferbloat mesmo sem ECN), mas superior ao cenário Prague+DualPI2+ECT(1). Taxa de perda não nula como mecanismo de controle.

---

#### T08 — Cubic + fq_codel + ECN 1 (ECT-1)

| Atributo | Valor |
|----------|-------|
| **ID** | T08 |
| **CC** | `cubic` |
| **Qdisc** | `fq_codel` |
| **ECN** | `1` (ECT-1) |
| **ECN cliente** | `ecn0` (Not-ECT) / `ecn1` (ECT(1)) |
| **Pasta (cliente ecn0)** | `experiments/t08-ecn-sender-ect0-cubic-fqcodel-ecn1/` |
| **Pasta (cliente ecn1)** | `experiments/t08-ecn-sender-ect1-cubic-fqcodel-ecn1/` |
| **Status** | [ ] Pendente |

**Descrição**: Cubic com marcação ECT(1) sobre fq_codel. Embora ECT(1) seja o codepoint L4S, o Cubic não é um CC escalável — ele reage a marcações CE com redução multiplicativa clássica.

**Hipótese**: O fq_codel marcará CE, e o Cubic reagirá com redução agressiva da janela (halving), podendo resultar em throughput inferior ao T02 (Prague com fq_codel+ECT-1). Demonstra que o codepoint ECT(1) sozinho não garante benefício sem um CC escalável.

---

#### T09 — Cubic + fq_codel + ECN 2 (ECT-0)

| Atributo | Valor |
|----------|-------|
| **ID** | T09 |
| **CC** | `cubic` |
| **Qdisc** | `fq_codel` |
| **ECN** | `2` (ECT-0) |
| **ECN cliente** | `ecn0` (Not-ECT) / `ecn1` (ECT(1)) |
| **Pasta (cliente ecn0)** | `experiments/t09-ecn-sender-ect0-cubic-fqcodel-ecn2/` |
| **Pasta (cliente ecn1)** | `experiments/t09-ecn-sender-ect1-cubic-fqcodel-ecn2/` |
| **Status** | [ ] Pendente |

**Descrição**: Cubic com ECT(0) sobre fq_codel. Configuração clássica com ECN habilitado conforme RFC 3168.

**Hipótese**: Comportamento semelhante a T08. A comparação T08 vs T09 verifica se há diferença prática entre ECT(0) e ECT(1) no fq_codel para um CC clássico.

---

#### T10 — Cubic + DualPI2 + ECN 0 (Not-ECT)

| Atributo | Valor |
|----------|-------|
| **ID** | T10 |
| **CC** | `cubic` |
| **Qdisc** | `dualpi2` |
| **ECN** | `0` (Not-ECT) |
| **ECN cliente** | `ecn0` (Not-ECT) / `ecn1` (ECT(1)) |
| **Pasta (cliente ecn0)** | `experiments/t10-ecn-sender-ect0-cubic-dualpi2-ecn0/` |
| **Pasta (cliente ecn1)** | `experiments/t10-ecn-sender-ect1-cubic-dualpi2-ecn0/` |
| **Status** | [ ] Pendente |

**Descrição**: Cubic sem ECN sobre DualPI2. Os pacotes Not-ECT são encaminhados para a fila clássica do DualPI2. A sinalização de congestionamento é feita por descarte.

**Hipótese**: Comportamento semelhante ao Cubic em uma fila FIFO com AQM PI. RTT controlado pelo PI clássico, mas com latência superior à fila L4S.

---

#### T11 — Cubic + DualPI2 + ECN 1 (ECT-1)

| Atributo | Valor |
|----------|-------|
| **ID** | T11 |
| **CC** | `cubic` |
| **Qdisc** | `dualpi2` |
| **ECN** | `1` (ECT-1) |
| **ECN cliente** | `ecn0` (Not-ECT) / `ecn1` (ECT(1)) |
| **Pasta (cliente ecn0)** | `experiments/t11-ecn-sender-ect0-cubic-dualpi2-ecn1/` |
| **Pasta (cliente ecn1)** | `experiments/t11-ecn-sender-ect1-cubic-dualpi2-ecn1/` |
| **Status** | [ ] Pendente |

**Descrição**: Cubic com ECT(1) sobre DualPI2. O DualPI2 encaminhará os pacotes para a fila L4S e aplicará marcação CE escalável (frequência de marcação proporcional ao quadrado da probabilidade).

**Hipótese**: Cenário problemático — o Cubic não é escalável e reagirá a cada CE com redução multiplicativa clássica, enquanto a fila L4S marca com frequência muito maior que a fila clássica. Pode resultar em throughput significativamente reduzido (starvation). Demonstra o risco de usar codepoint ECT(1) com um CC não L4S.

---

#### T12 — Cubic + DualPI2 + ECN 2 (ECT-0)

| Atributo | Valor |
|----------|-------|
| **ID** | T12 |
| **CC** | `cubic` |
| **Qdisc** | `dualpi2` |
| **ECN** | `2` (ECT-0) |
| **ECN cliente** | `ecn0` (Not-ECT) / `ecn1` (ECT(1)) |
| **Pasta (cliente ecn0)** | `experiments/t12-ecn-sender-ect0-cubic-dualpi2-ecn2/` |
| **Pasta (cliente ecn1)** | `experiments/t12-ecn-sender-ect1-cubic-dualpi2-ecn2/` |
| **Status** | [ ] Pendente |

**Descrição**: Cubic com ECT(0) sobre DualPI2. Os pacotes ECT(0) são classificados na fila clássica, recebendo marcação CE no estilo PI clássico.

**Hipótese**: Este é o cenário correto para Cubic com ECN sobre DualPI2 — a fila clássica marca CE com frequência compatível com a resposta multiplicativa do Cubic. RTT e throughput devem ser razoáveis.

---

### 2.3 Grupo Reno (T13–T18)

Reno é o CC TCP original com AIMD (Additive Increase, Multiplicative Decrease). Serve como segundo baseline para isolar o efeito do algoritmo CC.

---

#### T13 — Reno + fq_codel + ECN 0 (Not-ECT)

| Atributo | Valor |
|----------|-------|
| **ID** | T13 |
| **CC** | `reno` |
| **Qdisc** | `fq_codel` |
| **ECN** | `0` (Not-ECT) |
| **ECN cliente** | `ecn0` (Not-ECT) / `ecn1` (ECT(1)) |
| **Pasta (cliente ecn0)** | `experiments/t13-ecn-sender-ect0-reno-fqcodel-ecn0/` |
| **Pasta (cliente ecn1)** | `experiments/t13-ecn-sender-ect1-reno-fqcodel-ecn0/` |
| **Status** | [ ] Pendente |

**Descrição**: Reno clássico sem ECN sobre fq_codel. Baseline AIMD puro com controle de congestionamento guiado exclusivamente por perdas.

**Hipótese**: Desempenho inferior ao Cubic (T07) em termos de throughput devido ao crescimento linear mais lento da janela, mas com padrão de serrilhado (sawtooth) AIMD clássico visível nas métricas.

---

#### T14 — Reno + fq_codel + ECN 1 (ECT-1)

| Atributo | Valor |
|----------|-------|
| **ID** | T14 |
| **CC** | `reno` |
| **Qdisc** | `fq_codel` |
| **ECN** | `1` (ECT-1) |
| **ECN cliente** | `ecn0` (Not-ECT) / `ecn1` (ECT(1)) |
| **Pasta (cliente ecn0)** | `experiments/t14-ecn-sender-ect0-reno-fqcodel-ecn1/` |
| **Pasta (cliente ecn1)** | `experiments/t14-ecn-sender-ect1-reno-fqcodel-ecn1/` |
| **Status** | [ ] Pendente |

**Descrição**: Reno com ECT(1) sobre fq_codel. Semelhante a T08, mas com crescimento AIMD mais conservador.

**Hipótese**: Marcações CE do fq_codel causarão reduções multiplicativas frequentes. O throughput será inferior ao T02 (Prague) e possivelmente ao T08 (Cubic).

---

#### T15 — Reno + fq_codel + ECN 2 (ECT-0)

| Atributo | Valor |
|----------|-------|
| **ID** | T15 |
| **CC** | `reno` |
| **Qdisc** | `fq_codel` |
| **ECN** | `2` (ECT-0) |
| **ECN cliente** | `ecn0` (Not-ECT) / `ecn1` (ECT(1)) |
| **Pasta (cliente ecn0)** | `experiments/t15-ecn-sender-ect0-reno-fqcodel-ecn2/` |
| **Pasta (cliente ecn1)** | `experiments/t15-ecn-sender-ect1-reno-fqcodel-ecn2/` |
| **Status** | [ ] Pendente |

**Descrição**: Reno com ECT(0) sobre fq_codel. Configuração ECN clássica conforme RFC 3168 sobre um AQM amplamente implantado.

**Hipótese**: Comportamento semelhante a T14 na prática. Comparação T14 vs T15 isola possíveis diferenças de tratamento entre ECT(0) e ECT(1) no fq_codel.

---

#### T16 — Reno + DualPI2 + ECN 0 (Not-ECT)

| Atributo | Valor |
|----------|-------|
| **ID** | T16 |
| **CC** | `reno` |
| **Qdisc** | `dualpi2` |
| **ECN** | `0` (Not-ECT) |
| **ECN cliente** | `ecn0` (Not-ECT) / `ecn1` (ECT(1)) |
| **Pasta (cliente ecn0)** | `experiments/t16-ecn-sender-ect0-reno-dualpi2-ecn0/` |
| **Pasta (cliente ecn1)** | `experiments/t16-ecn-sender-ect1-reno-dualpi2-ecn0/` |
| **Status** | [ ] Pendente |

**Descrição**: Reno sem ECN sobre DualPI2. Pacotes Not-ECT são processados pela fila clássica com descarte como sinal de congestionamento.

**Hipótese**: Desempenho semelhante a T10, porém com throughput mais baixo devido ao crescimento AIMD linear do Reno.

---

#### T17 — Reno + DualPI2 + ECN 1 (ECT-1)

| Atributo | Valor |
|----------|-------|
| **ID** | T17 |
| **CC** | `reno` |
| **Qdisc** | `dualpi2` |
| **ECN** | `1` (ECT-1) |
| **ECN cliente** | `ecn0` (Not-ECT) / `ecn1` (ECT(1)) |
| **Pasta (cliente ecn0)** | `experiments/t17-ecn-sender-ect0-reno-dualpi2-ecn1/` |
| **Pasta (cliente ecn1)** | `experiments/t17-ecn-sender-ect1-reno-dualpi2-ecn1/` |
| **Status** | [ ] Pendente |

**Descrição**: Reno com ECT(1) sobre DualPI2. Os pacotes são direcionados para a fila L4S, que aplica marcação CE escalável. Como o Reno não é um CC escalável, ele reagirá de forma desproporcional às marcações.

**Hipótese**: Cenário de pior desempenho para Reno — marcação CE frequente da fila L4S combinada com redução multiplicativa agressiva resultará em sub-utilização severa do enlace (starvation).

---

#### T18 — Reno + DualPI2 + ECN 2 (ECT-0)

| Atributo | Valor |
|----------|-------|
| **ID** | T18 |
| **CC** | `reno` |
| **Qdisc** | `dualpi2` |
| **ECN** | `2` (ECT-0) |
| **ECN cliente** | `ecn0` (Not-ECT) / `ecn1` (ECT(1)) |
| **Pasta (cliente ecn0)** | `experiments/t18-ecn-sender-ect0-reno-dualpi2-ecn2/` |
| **Pasta (cliente ecn1)** | `experiments/t18-ecn-sender-ect1-reno-dualpi2-ecn2/` |
| **Status** | [ ] Pendente |

**Descrição**: Reno com ECT(0) sobre DualPI2. Os pacotes são classificados na fila clássica, recebendo marcação CE compatível com CC clássicos.

**Hipótese**: O cenário correto para Reno com ECN sobre DualPI2. Desempenho comparável a T16, porém com RTT ligeiramente menor graças à marcação CE no lugar de descarte.

---

## 3. Resumo da Matriz

| ID  | Done | ECN cliente | CC       | Qdisc      | ECN | Pasta |
| --- | ---- | ----------- | -------- | ---------- | --- | ----- |
| T01 | [ ] | ecn0 | prague   | fq_codel   | 0   | `experiments/t01-ecn-sender-ect0-prague-fqcodel-ecn0/` |
| T01 | [ ] | ecn1 | prague   | fq_codel   | 0   | `experiments/t01-ecn-sender-ect1-prague-fqcodel-ecn0/` |
| T02 | [ ] | ecn0 | prague   | fq_codel   | 1   | `experiments/t02-ecn-sender-ect0-prague-fqcodel-ecn1/` |
| T02 | [ ] | ecn1 | prague   | fq_codel   | 1   | `experiments/t02-ecn-sender-ect1-prague-fqcodel-ecn1/` |
| T03 | [ ] | ecn0 | prague   | fq_codel   | 2   | `experiments/t03-ecn-sender-ect0-prague-fqcodel-ecn2/` |
| T03 | [ ] | ecn1 | prague   | fq_codel   | 2   | `experiments/t03-ecn-sender-ect1-prague-fqcodel-ecn2/` |
| T04 | [ ] | ecn0 | prague   | dualpi2    | 0   | `experiments/t04-ecn-sender-ect0-prague-dualpi2-ecn0/` |
| T04 | [ ] | ecn1 | prague   | dualpi2    | 0   | `experiments/t04-ecn-sender-ect1-prague-dualpi2-ecn0/` |
| T05 | [ ] | ecn0 | prague   | dualpi2    | 1   | `experiments/t05-ecn-sender-ect0-prague-dualpi2-ecn1/` |
| T05 | [ ] | ecn1 | prague   | dualpi2    | 1   | `experiments/t05-ecn-sender-ect1-prague-dualpi2-ecn1/` |
| T06 | [ ] | ecn0 | prague   | dualpi2    | 2   | `experiments/t06-ecn-sender-ect0-prague-dualpi2-ecn2/` |
| T06 | [ ] | ecn1 | prague   | dualpi2    | 2   | `experiments/t06-ecn-sender-ect1-prague-dualpi2-ecn2/` |
| T07 | [ok] | ecn0 | cubic    | fq_codel   | 0   | `experiments/t07-ecn-sender-ect0-cubic-fqcodel-ecn0/` |
| T07 | [ ] | ecn1 | cubic    | fq_codel   | 0   | `experiments/t07-ecn-sender-ect1-cubic-fqcodel-ecn0/` |
| T08 | [ ] | ecn0 | cubic    | fq_codel   | 1   | `experiments/t08-ecn-sender-ect0-cubic-fqcodel-ecn1/` |
| T08 | [ ] | ecn1 | cubic    | fq_codel   | 1   | `experiments/t08-ecn-sender-ect1-cubic-fqcodel-ecn1/` |
| T09 | [ ] | ecn0 | cubic    | fq_codel   | 2   | `experiments/t09-ecn-sender-ect0-cubic-fqcodel-ecn2/` |
| T09 | [ ] | ecn1 | cubic    | fq_codel   | 2   | `experiments/t09-ecn-sender-ect1-cubic-fqcodel-ecn2/` |
| T10 | [ ] | ecn0 | cubic    | dualpi2    | 0   | `experiments/t10-ecn-sender-ect0-cubic-dualpi2-ecn0/` |
| T10 | [ ] | ecn1 | cubic    | dualpi2    | 0   | `experiments/t10-ecn-sender-ect1-cubic-dualpi2-ecn0/` |
| T11 | [ ] | ecn0 | cubic    | dualpi2    | 1   | `experiments/t11-ecn-sender-ect0-cubic-dualpi2-ecn1/` |
| T11 | [ ] | ecn1 | cubic    | dualpi2    | 1   | `experiments/t11-ecn-sender-ect1-cubic-dualpi2-ecn1/` |
| T12 | [ ] | ecn0 | cubic    | dualpi2    | 2   | `experiments/t12-ecn-sender-ect0-cubic-dualpi2-ecn2/` |
| T12 | [ ] | ecn1 | cubic    | dualpi2    | 2   | `experiments/t12-ecn-sender-ect1-cubic-dualpi2-ecn2/` |
| T13 | [ ] | ecn0 | reno     | fq_codel   | 0   | `experiments/t13-ecn-sender-ect0-reno-fqcodel-ecn0/` |
| T13 | [ ] | ecn1 | reno     | fq_codel   | 0   | `experiments/t13-ecn-sender-ect1-reno-fqcodel-ecn0/` |
| T14 | [ ] | ecn0 | reno     | fq_codel   | 1   | `experiments/t14-ecn-sender-ect0-reno-fqcodel-ecn1/` |
| T14 | [ ] | ecn1 | reno     | fq_codel   | 1   | `experiments/t14-ecn-sender-ect1-reno-fqcodel-ecn1/` |
| T15 | [ ] | ecn0 | reno     | fq_codel   | 2   | `experiments/t15-ecn-sender-ect0-reno-fqcodel-ecn2/` |
| T15 | [ ] | ecn1 | reno     | fq_codel   | 2   | `experiments/t15-ecn-sender-ect1-reno-fqcodel-ecn2/` |
| T16 | [ ] | ecn0 | reno     | dualpi2    | 0   | `experiments/t16-ecn-sender-ect0-reno-dualpi2-ecn0/` |
| T16 | [ ] | ecn1 | reno     | dualpi2    | 0   | `experiments/t16-ecn-sender-ect1-reno-dualpi2-ecn0/` |
| T17 | [ ] | ecn0 | reno     | dualpi2    | 1   | `experiments/t17-ecn-sender-ect0-reno-dualpi2-ecn1/` |
| T17 | [ ] | ecn1 | reno     | dualpi2    | 1   | `experiments/t17-ecn-sender-ect1-reno-dualpi2-ecn1/` |
| T18 | [ ] | ecn0 | reno     | dualpi2    | 2   | `experiments/t18-ecn-sender-ect0-reno-dualpi2-ecn2/` |
| T18 | [ ] | ecn1 | reno     | dualpi2    | 2   | `experiments/t18-ecn-sender-ect1-reno-dualpi2-ecn2/` |

### Progresso

| ECN cliente | fq_codel (ECN 0/1/2) | dualpi2 (ECN 0/1/2) | Total |
| ----------- | --------------------- | -------------------- | ----- |
| ecn0        | 1 / 9                 | 0 / 9                | 1 / 18 |
| ecn1        | 0 / 9                 | 0 / 9                | 0 / 18 |
| **Total**   | **1 / 18**             | **0 / 18**            | **1 / 36** |

---

## 4. Comparações Analíticas Planejadas

As comparações devem ser realizadas separando os resultados por **ECN do cliente** (ecn0 vs ecn1), pois a marcação no remetente altera o tipo de pacote injetado na rede.

As seguintes comparações entre cenários são planejadas para a análise dos resultados:

### 4.1 Efeito do CC (mantendo qdisc e ECN fixos)

| Comparação | Cenários | Pergunta |
|------------|----------|----------|
| Prague vs Cubic vs Reno (fq_codel, ECN 0) | T01 vs T07 vs T13 | Qual o impacto do CC quando não há ECN? |
| Prague vs Cubic vs Reno (fq_codel, ECN 1) | T02 vs T08 vs T14 | O codepoint L4S beneficia apenas o Prague? |
| Prague vs Cubic vs Reno (fq_codel, ECN 2) | T03 vs T09 vs T15 | Diferença entre CCs com ECN clássico no fq_codel? |
| Prague vs Cubic vs Reno (dualpi2, ECN 0) | T04 vs T10 vs T16 | Comportamento na fila clássica do DualPI2? |
| Prague vs Cubic vs Reno (dualpi2, ECN 1) | T05 vs T11 vs T17 | Quem se beneficia da fila L4S? Quem sofre starvation? |
| Prague vs Cubic vs Reno (dualpi2, ECN 2) | T06 vs T12 vs T18 | Comportamento na fila clássica com ECN? |

### 4.2 Efeito da Qdisc (mantendo CC e ECN fixos)

| Comparação | Cenários | Pergunta |
|------------|----------|----------|
| fq_codel vs dualpi2 (Prague, ECN 1) | T02 vs T05 | O Prague se beneficia mais do DualPI2? |
| fq_codel vs dualpi2 (Cubic, ECN 1) | T08 vs T11 | O DualPI2 prejudica CCs clássicos com ECT(1)? |
| fq_codel vs dualpi2 (Reno, ECN 0) | T13 vs T16 | Diferença de AQM para tráfego Not-ECT? |

### 4.3 Efeito do ECN (mantendo CC e qdisc fixos)

| Comparação | Cenários | Pergunta |
|------------|----------|----------|
| ECN 0 vs 1 vs 2 (Prague, dualpi2) | T04 vs T05 vs T06 | Qual codepoint ECN maximiza o desempenho L4S? |
| ECN 0 vs 1 vs 2 (Cubic, dualpi2) | T10 vs T11 vs T12 | Qual codepoint é seguro para CCs clássicos no DualPI2? |
| ECN 0 vs 1 vs 2 (Prague, fq_codel) | T01 vs T02 vs T03 | O ECN faz diferença com AQM clássico? |

### 4.4 Cenários-Chave

| Rótulo | Cenário | Significado |
|--------|---------|-------------|
| **L4S Ideal** | T05 | Prague + DualPI2 + ECT(1) — ponto ótimo da arquitetura L4S. |
| **Baseline Clássico** | T07 | Cubic + fq_codel + Not-ECT — configuração padrão da internet atual. |
| **AIMD Clássico** | T13 | Reno + fq_codel + Not-ECT — baseline AIMD puro. |
| **Risco de Starvation** | T11, T17 | CCs clássicos com ECT(1) na fila L4S do DualPI2. |

---

## 5. Configuração por Cenário

### 5.1 No ESP32 (`esp32dev/src/UDPPragueClient.ino`)

```cpp
#define ECN_SENDER_ENABLE <0|1>       // 0=Not-ECT (ecn0), 1=ECT(1) (ecn1)
const char* GW_CC_ALGO = "<algo>";    // "prague", "cubic" ou "reno"
const char* GW_QDISC   = "<qdisc>";  // "fq_codel" ou "dualpi2"
```

### 5.2 No Gateway Linux

```bash
# Algoritmo de controle de congestionamento
sudo sysctl -w net.ipv4.tcp_congestion_control=<algo>

# Disciplina de fila (substituir eth0 pela interface correta)
sudo tc qdisc replace dev eth0 root dualpi2
# ou
sudo tc qdisc replace dev eth0 root fq_codel
```

---

## 6. Procedimento de Execução

Para cada cenário base (T01–T18) e para cada ECN do cliente (ecn0/ecn1):

1. **Configurar o gateway**: definir CC (`sysctl`) e qdisc (`tc`).
2. **Configurar o ESP32**: ajustar `ECN_SENDER_ENABLE` (ecn0/ecn1), `GW_CC_ALGO`, `GW_QDISC` em `UDPPragueClient.ino` e fazer upload do firmware.
3. **Executar o experimento** pelo tempo definido (`TEST_DURATION_SEC = 180 s`).
4. **Coletar logs**: salvar `esp_client.log` e `server.log` na pasta do cenário.
5. **Gerar métricas**: executar `metrics_academic.ipynb` e salvar gráficos e CSV na pasta.
6. **Atualizar este documento**: marcar `Done` como `[x]` e atualizar o Progresso.

---

## 7. Artefatos por Cenário

Cada pasta `experiments/tXX-*/` deve conter:

| Arquivo | Descrição |
|---------|-----------|
| `esp_client.log` | Log bruto CSTATS do ESP32 (saída serial). |
| `server.log` | Log bruto `[RECVER]` do receptor C++ UDP Prague. |
| `*_client_metrics.png` | Métricas do cliente (RTT, jitter, inflight, pacing). |
| `*_server_metrics.png` | Métricas do servidor (throughput RX, CE-mark, loss). |
| `*_rtt_cdf.png` | CDF de RTT do cliente. |
| `*_transmission_analysis.png` | Comparativo TX/RX (pacotes e bytes). |
| `*_client.csv` | Séries temporais do cliente. |
| `*_server.csv` | Séries temporais do servidor. |
| `*_summary.csv` | Estatísticas agregadas. |
| `README.md` (opcional) | Notas de observação e desvios do cenário planejado. |
