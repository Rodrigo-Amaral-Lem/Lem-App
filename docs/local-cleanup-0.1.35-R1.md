# Limpar rascunhos sincronizados — 0.1.35 R1

## Causa

A opção **Limpar cópias locais** só considerava reports com estado `closed`.
Um rascunho podia estar completamente confirmado no servidor, com fotografias
enviadas e sem alterações pendentes, e ainda assim nunca ser removido localmente.
Os testes anteriores verificavam limpeza de reports fechados e proteção de
rascunhos por enviar, mas não a remoção de um rascunho totalmente sincronizado.

## Comportamento corrigido

A limpeza manual remove cópias de reports **fechados ou em rascunho** quando:

- O servidor já confirmou a criação (revisão superior a zero).
- Não existem alterações por enviar nem conflito pendente.
- Todas as fotografias estão marcadas como enviadas.
- Não existem remoções de fotografias por confirmar.

Inclui o report aberto e os marcados para manter offline. Os diários são
removidos, os metadados locais são guardados sem essas cópias e as fotografias
privadas sem referências protegidas são apagadas. Rascunhos não publicados,
alterações por enviar, conflitos e originais na galeria permanecem no dispositivo.
O resultado indica quantas cópias/fotografias foram removidas e quantos reports
ficaram protegidos.

O report **continua na lista do contrato porque existe no servidor**. Ao abri-lo
novamente com rede, a app obtém os seus dados; as fotografias só voltam ao
dispositivo quando forem pedidas. A limpeza local não chama DELETE no servidor.
Apagar definitivamente um rascunho continua a exigir a ação de remoção do report,
com as permissões do proprietário/administrador.

A política automática de cache mantém a proteção de todos os rascunhos. Esta
alteração aplica-se à limpeza manual confirmada pelo utilizador.

## Verificação

- Teste novo ligado ao controlador anterior falha: a fotografia do rascunho
  sincronizado ainda existe após a limpeza.
- Controlador corrigido: cópia e fotografia sincronizadas removidas do disco;
  rascunhos por enviar, conflitos e remoções pendentes conservados.
- Integração com API temporária: enviar rascunho/fotografia, limpar, reabrir o
  mesmo UUID do servidor; respostas e metadados da fotografia conservados, sem
  caminho local para a imagem. Os testes existentes de edição, conflitos,
  cópias, remoção e download também passam.
- Navegação/diálogos Qt Quick em dimensões Android passaram sem avisos QML.

Testes locais com Qt 6.10.2/Linux. Não foi gerado um APK/EXE para esta revisão nem
alterado o servidor real. Basta recompilar/instalar a app: versão **0.1.35**, Android
code **138**. Código do servidor, compatibilidade e base de dados inalterados.
