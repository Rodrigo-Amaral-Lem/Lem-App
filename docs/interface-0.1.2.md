# LEM 0.1.2 — separadores do report

O report mostra apenas um grupo de perguntas de cada vez:

| Separador | Grupo |
|---|---|
| PC01 | Estado físico do carregador |
| PC02 | Manutenção do carregador |
| PC03 | Testes de carga |
| Segurança | Segurança (identificador interno SEG) |
| PG01 | Manutenção do site |
| PG02 | Manutenção QVE |
| PG03 | Correção de não conformidades |
| PG04 | Informações gerais |

A barra de separadores fica visível durante a deslocação vertical. Em ecrãs estreitos, desliza horizontalmente ou usa os botões ‹ / ›. Ao usar um separador com foco de teclado, as teclas esquerda/direita selecionam e focam o grupo anterior/seguinte. A barra acompanha a seleção para manter o separador ativo visível.

Cada separador indica respostas preenchidas / perguntas do grupo. Uma resposta é contada quando o seu valor não está vazio. A contagem não significa que a resposta ou as fotografias já passaram a validação de conclusão. As regras de validação continuam a abranger todos os grupos ao concluir o report.

Ao mudar de grupo, o formulário volta ao topo. As respostas, observações e fotografias continuam associadas aos mesmos checkpoints. Ao abrir outro report, a navegação começa no PC01. As perguntas, os seus identificadores, a estrutura dos dados e a exportação mantêm-se.

## Instalar

Fecha a app, extrai o pacote para uma pasta nova, abre `app/CMakeLists.txt` no Qt Creator e recompila com o kit Desktop Qt 6.12.0 MinGW 64-bit. Podes também substituir apenas a pasta `app` do projeto anterior, mantendo `shared` e usando o mesmo kit. Em Conta, confirma a versão **0.1.2**.

Não é necessário atualizar o servidor nem apagar dados. O pacote mantém a correção `LIBRARY DESTINATION` para configuração Android.

## Validação

O teste de navegação QML (`app/tests/test_report_tabs.py`) passou a 1100×820 e 390×844, com Qt 6.10.2/PySide6, renderização por software e backend simulado. Verificou introdução de observações pelo teclado, passagem pelos oito grupos através das setas, visibilidade exclusiva do grupo escolhido, navegação por teclado, conservação das respostas e dos metadados de uma fotografia, regresso às observações e ausência de avisos QML. As imagens dos dois tamanhos foram inspecionadas.

O teste não liga ao servidor, não lê rascunhos reais e não testa transferência de fotografias. O executável C++ Qt 6.12 e os builds nativos Windows/Android/iOS não foram compilados nesta sessão.
