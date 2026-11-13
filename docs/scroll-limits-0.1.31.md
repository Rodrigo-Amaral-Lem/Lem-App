# Lem 0.1.31 — limite de deslocação no fim da página

As perguntas, a lista de IDs e a área Gestão passam a parar no limite do conteúdo.
Não é possível continuar a arrastar o último elemento muito para cima e expor uma
área vazia grande por baixo, quer a barra Sinc. esteja visível ou escondida.

Havia duas contribuições:

- O comportamento elástico padrão das páginas permitia arrastar para além do fim.
- A reserva final repetia parte da margem exterior e, nas colunas, mais um espaçamento.

A correção usa limites rígidos nos Flickables e reduz a reserva ao espaço realmente
ocupado pela barra inferior sobre a área de leitura, mais uma pequena separação.
Mantém-se uma pequena reserva fixa para o último cartão não ficar tapado quando
os botões reaparecem. Não se anima o tamanho da página ou do conteúdo; a posição
das perguntas e a geometria do indicador continuam estáveis durante os menus.

Páginas curtas ou listas vazias mantêm naturalmente espaço disponível no ecrã,
mas não podem ser arrastadas para criar espaço adicional. Listas com cartões de
alturas diferentes continuam virtualizadas; não são carregados todos os cartões
ou fotografias para calcular o limite.

Aplica-se ao Android e ao desktop. A captura pela câmara, sincronização e regras
de exportação não foram alteradas. A versão mínima compatível continua 0.1.28;
a última suportada passa para 0.1.31. Atualizar a app e a política do servidor
(o deploy atual promove automaticamente a última versão). Sem reset da base.

## Verificação

`python app/tests/test_scroll_limits.py` usa gestos reais de toque no fim dos reports
e dos IDs, com a barra visível/escondida, cartões de alturas variáveis, grupos curtos,
um único ID, catálogo vazio e janela desktop. Verifica a posição do último cartão e
a separação dos botões. O mesmo teste falha na versão anterior pela reserva excessiva.

A suite `test_scroll_chrome.py` continua a verificar cada frame da animação e os
limites do indicador lateral. Foram também executadas regressões dos reports,
notificações, grupos e exportação. Testes QML em Qt 6.10.2 offscreen/Linux;
compilação final Qt 6.12 e validação no Android físico ficam por confirmar.
