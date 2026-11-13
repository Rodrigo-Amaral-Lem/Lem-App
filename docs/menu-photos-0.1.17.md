# Menu e fotografias do servidor — 0.1.17

O botão do menu alterna entre abrir e fechar a seleção. Aplica-se ao menu Contratos/Gestão e às opções do report. O toque no próprio botão deixa de fechar o menu na pressão e de o reabrir ao largar. Tocar fora ou usar Voltar/Escape continua a fechar.

Nos reports com dados enviados ao servidor, existe agora um botão **Descarregar fotografias**, no início do conteúdo do report, acima das perguntas. Abre a mesma confirmação da opção **Descarregar todas as fotografias** do menu: apresenta o total, quantas faltam no dispositivo e o tamanho conhecido. Confirme **Descarregar todas** para consultar as imagens sem rede.

Reutiliza o descarregamento em lote existente: não repete imagens já disponíveis, no máximo duas transferências simultâneas, progresso, cancelamento pelo menu e possibilidade de repetir para obter as que faltam. A app permite continuar a trabalhar. O report fica mantido offline até escolher **Permitir limpeza destas fotografias**. Precisa de sessão e rede para obter imagens em falta; fotografias locais nunca enviadas não podem ser recuperadas do servidor.

Ao abrir um report pela primeira vez do servidor, os metadados são atuais. Para atualizar os dados de um rascunho já em cache, use **Atualizar report do servidor**, respeitando as alterações locais pendentes.

Validação: testes QML com pressões reais do rato, repetindo abrir/fechar, toque fora e Escape; opção direta e opção do menu abrem a confirmação, confirmam o report correto e ficam indisponíveis durante um lote ativo. Testes de desktop e dimensões Android sem avisos QML. Inspeção visual do botão num report fechado. Backend de transferência sem alterações nesta versão; compilação Android física não executada.

Atualização apenas do cliente. Mantém o servidor 0.1.15. A política de fecho segue a orientação do Qt para menus ligados a botões: https://doc.qt.io/qt-6/qml-qtquick-controls-menu.html
