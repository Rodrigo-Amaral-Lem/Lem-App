# Cartões do contrato — 0.1.35 R2

- Em Android, os dados apresentados em cada cartão são ID, número de série e
  morada. Para importações antigas sem morada, usa-se a localização disponível;
  valores em falta têm indicação explícita.
- Windows/Linux conservam os detalhes anteriores.
- Em todas as plataformas, o cartão usa fundo/bordo âmbar com **Por fazer** ou
  verde com **Concluído**. Existe texto além da cor.
- Basta um report `closed` desse carregador no contrato para marcar concluído.
  Só rascunhos não contam. Um report fechado localmente conta, mesmo com envio
  pendente; o botão desse report continua a indicar **por enviar**.
- Os botões de nova intervenção e acesso aos reports permanecem disponíveis.
  A pesquisa continua a usar todos os dados, mesmo os ocultos em Android.

O modelo da lista usa uma cópia JSON independente dos dados do controlador para
que a comparação de alterações e as cores não fiquem desatualizadas por wrappers
QVariant. A posição da lista continua a ser restaurada por ID/offset, como antes.

Verificação local em Qt Quick 6.10.2: dimensões Android e desktop, dados visíveis,
sem reports, só rascunho, fechado, fechado mais rascunho, fechado com envio
pendente e remoção do último fechado. Cores/textos atualizam sem avisos QML.
Regressões existentes de navegação/posição e limites de scroll também passaram.
Não foi compilado um novo APK ou EXE Windows nesta revisão.

Basta recompilar/instalar a app: versão 0.1.35, Android code 139. O código do
servidor, a política de versões e a base de dados não foram alterados.
