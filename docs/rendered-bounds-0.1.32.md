# Lem 0.1.32 — limites pelo último cartão desenhado

Esta revisão trata o caso em que a deslocação ainda continuava muito para além da
última pergunta com a barra Sinc. escondida. A alteração da 0.1.31, limitada ao
arrasto elástico e ao espaçamento, não resolveu o caso observado no Android físico.

## Alteração

- As páginas de perguntas e Gestão usam agora Flickables explícitos, sem os
  wrappers automáticos de ScrollView para determinar a área deslocável.
- Nas perguntas, a altura deslocável termina na posição real da última pergunta
  do grupo, mais a pequena reserva necessária à barra inferior. O tamanho inferido
  do contentor já não decide o limite. Grupos, notas e fotografias continuam dinâmicos.
- A altura da barra Sinc. é fixa em função dos controlos e padding, mesmo quando
  invisível. O seu layout interno não pode aumentar a reserva no fim do documento.
- A lista de IDs continua virtualizada. Quando o último cartão está instanciado,
  a posição desse cartão limita o deslocamento e determina o fim do indicador.
  Não é necessário carregar todos os cartões ou fotografias para esta verificação.
- Mantêm-se a área de leitura fixa, a animação dos menus, a separação das notificações
  e o acesso ao último cartão com a barra visível. A roda do rato, teclas de paginação
  e Home/End continuam disponíveis no desktop.

Mantém-se apenas a reserva pequena e fixa para a barra poder reaparecer sem tapar
o último cartão ou deslocar as perguntas. Não existe uma reserva de um ecrã inteiro.
Uma página curta pode naturalmente ter espaço livre abaixo do conteúdo; não pode
ser arrastada para criar mais espaço.

## Verificações

`test_scroll_end_transition.py` chega ao fim por gestos reais que escondem os menus,
em vez de começar com um salto programático para o fim. Repete arrasto, inversão
de sentido, alteração de resposta/modelo, mudança de grupo e redimensionamento,
comparando a posição da última pergunta/cartão com o fundo da área de leitura.

Passaram também as regressões de limites, animação frame a frame, notificações,
reports em tamanhos desktop/mobile, navegação entre grupos e entrada por rato/teclado.
A posição do fim e a altura da barra são verificadas durante os gestos. Estes testes
usam Qt 6.10.2 offscreen em Linux; a confirmação em Qt 6.12/AGM Android 15 continua
necessária. A correção anterior passou neste ambiente sem reproduzir o problema
observado no dispositivo; esta revisão elimina a inferência de altura e acrescenta
as verificações pelo conteúdo efetivamente desenhado.

## Instalação

Recompilar/instalar a app deste pacote e confirmar **0.1.32** na área Conta.
Não há alterações funcionais na API nem no esquema da base de dados. Num servidor
existente basta permitir esta versão: em `deploy/.env`, definir
`LEM_LATEST_CLIENT_VERSION=0.1.32` e recriar a API com
`sudo docker compose up -d --force-recreate api`.
A mínima pode manter-se em 0.1.28. Não é preciso reinstalar software do servidor
nem apagar dados. Um deploy completo continua disponível, mas não é necessário
para aplicar esta correção da interface.
