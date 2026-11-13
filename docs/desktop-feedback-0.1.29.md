# Lem 0.1.29 — notificações e paridade desktop

Atualizar a app a partir deste pacote, recompilando o alvo `Lem` com Qt 6.12 num
diretório de build atualizado. A versão apresentada em Conta deve ser **0.1.29**.
O mesmo `Main.qml` e os controladores `Lem.cpp`/`LemOffline.cpp` são usados em
Windows, Linux e Android; não existe uma versão funcional separada de desktop.

## Notificações

O aviso temporário fica **acima da parte visível da barra inferior**, com 8 píxeis
lógicos de separação, inclusive durante a animação Android. A notificação tem no
máximo duas linhas e um botão de fechar. Clicar na mensagem abre os detalhes da
operação. Não cobre os botões Sinc./Rever nem aumenta as barras ou desloca perguntas.
A mesma notificação está disponível nas janelas desktop, pequenas e grandes.

O problema anterior era a âncora no fundo da janela: o aviso e os botões ocupavam
a mesma área, apesar de estarem em camadas diferentes. A correção usa a altura
visível efetiva do rodapé, não apenas uma alteração da ordem das camadas.

## Servidor predefinido e definições guardadas

O valor de fábrica é `https://lem.rodrod.pt` em **todas as plataformas**. O endereço
antigo podia continuar a aparecer porque QSettings reutilizava o valor guardado
antes da mudança de nome, incluindo valores recuperados por migração de armazenamento.

`ServerSettings.h` centraliza a política. Na primeira execução desta versão,
endereços HTTPS de protótipos na mesma família DNS do projeto, sem caminho próprio,
credenciais ou porta especial, são atualizados para o servidor Lem. Domínios de
servidores independentes, caminhos/portas específicos e escolhas explícitas guardadas
pela versão atual mantêm-se. Não há alteração automática de DNS.

Em **Conta**, o botão **Usar servidor Lem** permite repor o endereço de fábrica a
qualquer momento. Uma mudança de servidor termina a sessão de comunicação e muda
o espaço de dados selecionado; não apaga nem transfere reports de outro servidor.
É necessário iniciar sessão no servidor escolhido.

## Paridade verificada

| Funcionalidade | Windows / Linux | Android |
| --- | --- | --- |
| Login HTTPS, contas/perfis e política de versões | Controlador/API partilhados | Iguais |
| Contratos, IDs, pesquisa, importação Excel e gestão | Mesmas ações e permissões | Iguais |
| Grupos PC/PG, respostas rápidas, limpar resposta e observações removíveis | Mesmos controlos; navegação por botões/teclado | Idem e gestos laterais |
| Rascunhos, propriedade, transferências, cópias de conflito e eliminação | Mesma implementação | Igual |
| Fotografias: anexar, ampliar, remover e descarregar todas | Seletor de ficheiros | Galeria e app de câmara |
| PDF local sem sessão/rede e exportação ZIP do servidor | Mesmas funções e condições | Iguais |
| Gravação local em segundo plano e fotos enviadas por grupos | Mesmas filas e limites | Iguais |
| Estado da operação, progresso, sincronização, cache e limpeza | Mesmas ações e informação | Iguais |
| Barras que recolhem com deslocação e gesto lateral entre grupos | Barras fixas; não ativa gestos de smartphone | Comportamento Android |

Não foram encontrados fluxos de reports/permissões/exportação protegidos por
condições Android. Os condicionais nativos Android limitam-se à integração da
câmara, ao retorno dessa captura e à configuração/diagnóstico TLS da plataforma.

## Atualização do servidor e compatibilidade

A versão mínima continua **0.1.28**; a última suportada passa para **0.1.29**.
Atualizar também o servidor/política para permitir login com a nova app.
O deploy normal agora promove `LEM_LATEST_CLIENT_VERSION`, se estiver fixada num
valor anterior, para a versão que acompanha o pacote. Conserva a versão mínima,
segredos, domínio e uma última versão já configurada mais recente. Um `.env` sem
essa chave usa o valor atual definido no Compose. Não é necessário reset da base.

Se atualizar manualmente, indicar no `.env` e recriar a API:

```dotenv
LEM_MIN_CLIENT_VERSION=0.1.28
LEM_LATEST_CLIENT_VERSION=0.1.29
```

```bash
cd ~/apps/Lem/deploy
sudo docker compose up -d --build --force-recreate api
```

## Verificações

- Suite QML completa de reports em janela desktop e em tamanho de smartphone.
- Notificação em 390×844, 500×700, 800×650 e 1280×800; posições intermédias das
  barras, mensagens longas, alvos de toque separados e fecho da notificação.
- Teste nativo de preferências: valor inicial, migração de alias, escolhas explícitas,
  servidores independentes e endereços com caminho/porta próprios.
- Cliente C++ com API local: edição, gravação, propriedade, conflitos, fotos em lotes,
  descarregamento/cancelamento/repetição, eliminação, cache e progresso.
- PDF nativo offline e handshake de versões com APIs descartáveis.
- Suites de servidor/deploy, incluindo promoção da última versão sem mudar segredos/mínima.

Execução nativa e QML em Linux com Qt 6.10.2 offscreen; não substitui compilação
Qt 6.12 e teste físicos em Windows/Android. Os testes PowerShell e os workbooks
opcionais continuam dependentes das ferramentas/ficheiros respetivos. Não foi
feita alteração no servidor real nesta entrega.
