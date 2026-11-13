> Atualização 0.1.23: a identificação Android atual é `pt.lem.app`, uma instalação separada. Sincronizar trabalho pendente antes de trocar. As notas de compatibilidade desta página descrevem a entrega histórica.

# LEM 0.1.6 — formulário adaptado a Android

Esta entrega contém o projeto fonte Qt; não contém um APK compilado. Abrir `app/CMakeLists.txt` no Qt Creator, escolher o kit Android/OpenSSL já usado na 0.1.5 e recompilar. A versão apresentada em Conta é 0.1.6 e o versionCode Android é 106. O identificador `pt.lem.app`, armazenamento, TLS, câmara e API permanecem compatíveis com a entrega anterior. Não é necessária atualização do servidor nem reset da base de dados para estas alterações.

## Alterações

- Cabeçalho de report compacto, com ID do carregador, estado local/sincronizado e progresso de respostas. O seletor de contrato fica nos ecrãs Contratos/Gestão.
- Botão Voltar em telemóvel; opções do report (Conta, dados do carregador, última mensagem, PDF e ZIP) no menu superior.
- Grupos PCxx/PGxx com navegação horizontal e seletor de todos os grupos. A posição de scroll de cada grupo é conservada durante a sessão do report.
- Respostas OK / NOK / N/A em botões de escolha única, sem resposta pré-selecionada. N/A continua gravado como `NA`, tal como na API anterior. Uma resposta pode ser limpa.
- Observações opcionais recolhidas por defeito; NOK abre automaticamente o atributo obrigatório. Observações já guardadas continuam visíveis, mesmo depois de mudar a resposta.
- Câmara como ação principal em Android; Galeria como alternativa. Fotografias obrigatórias em falta assinaladas junto ao item. N/A dispensa a fotografia, conforme a regra existente.
- Miniaturas com tamanho de descodificação limitado e carregamento assíncrono. Botão Ampliar para consultar a fotografia, sem alterar o original. Não foi acrescentada eliminação de evidências nesta versão.
- Rever report apresenta requisitos por cumprir e permite abrir cada item. Concluir fica disponível apenas quando todas as respostas, observações NOK e fotografias obrigatórias estão presentes. O backend mantém a validação final existente.
- Reports não editáveis mostram respostas e observações como texto legível em vez de atributos desativados.
- Rodapé móvel compacto em repouso, com Sincronizar e Rever report. Feedback recente visível durante dez segundos e acessível integralmente em Ver / Estado da operação. O indicador de carregamento continua destacado no cabeçalho e no rodapé durante operações.
- Alvos de toque com pelo menos 48 unidades lógicas Qt em Android/largura compacta; rótulos de acessibilidade nas ações e sem dependência de glifos de ícones.
- Conta em painel com scroll para acomodar ecrãs baixos. O painel permanece aberto durante o login, permitindo ler falhas e tentar novamente; Fechar regressa à aplicação.
- Pesquisa sem resultados tem uma mensagem própria.

## Validação desta entrega

Ensaio isolado com PySide6/Qt 6.10.2 em Linux, renderização software e backend fictício, sem comunicar com o servidor:

- Arranque sem avisos QML em largura de telemóvel e de desktop.
- Escolha inicial vazia; botões OK/NOK/NA; abertura de observação NOK e gravação de texto.
- Contagem separada de respostas e requisitos; fotografia obrigatória; dispensa com NA; observação NOK obrigatória.
- Navegação nos oito grupos e por teclado; conservação de respostas, metadados fotográficos e posição de scroll.
- Revisão com cartões de largura válida, bloqueio de conclusão incompleta, navegação para o item em falta no mesmo grupo e noutro grupo, conclusão completa encaminhada ao backend.
- Câmara encaminhada ao checkpoint correto e Galeria apenas em Android.
- Leitura sem atributos editáveis de report concluído, e carregamento destacado durante operações.
- Renderizações inspecionadas a 390×844, 320×844 e 844×390. Exemplos em `docs/previews/` usam exclusivamente dados fictícios.
- Sete testes de configuração Android/OpenSSL aprovados. Não houve alteração às bibliotecas TLS nem à configuração da câmara.

Repetir:

```bash
python app/tests/test_report_tabs.py report mobile
python app/tests/test_report_tabs.py report
python -m pytest app/tests/test_android_openssl.py -q
```

Para os testes QML é necessário PySide6. Para os testes OpenSSL é necessário CMake no PATH, ou definir `CMAKE_COMMAND` com o caminho do executável.

Não foi compilado/instalado um APK nesta entrega. Falta confirmar num dispositivo físico: teclado e scroll dos atributos, Back do sistema, TalkBack, escala de texto do Android, dimensões físicas dos alvos de toque, recortes/barras do sistema, retorno da câmara e leitura de fotos reais. O tamanho de letra atual não constitui garantia de suporte integral à escala de acessibilidade. O carregamento preguiçoso dos grupos e o desempenho prolongado devem ser avaliados com o QML Profiler num telefone antes de outra alteração estrutural.
