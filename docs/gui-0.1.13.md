# Ações das fotografias — 0.1.13

As ações de cada fotografia usam ícones geométricos: lupa para ampliar, caixote do lixo para remover e seta para descarregar quando o original não está no dispositivo. Os botões ficam na mesma linha, com área de toque de 48 px em Android e nomes completos para leitores de ecrã e dicas no desktop. A remoção tem cor vermelha e mantém a confirmação existente.

A visualização ampliada usa um botão X para fechar. O estado da fotografia (por enviar, no servidor ou a descarregar) continua visível em texto. Nos reports concluídos, a remoção mantém-se indisponível.

Atualização apenas do cliente. Não há alterações do servidor, base de dados, sincronização ou identificador Android. Inclui as melhorias das versões 0.1.11 e 0.1.12.

Verificação: testes QML em dimensões de Android e desktop para abrir e fechar a imagem, descarregar, confirmar/cancelar remoção, alinhamento dos ícones e proteção dos reports concluídos, juntamente com as regressões de observações, sessão, navegação e preenchimento. Inspeção visual da linha de ações no formato de smartphone. Sem avisos QML. Não foi executada uma compilação Android nesta revisão.
