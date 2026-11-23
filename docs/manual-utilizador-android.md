# Manual de utilização da app Lem para Android

Android · Aplicação 0.8.0 · Edição 2.18 · 21 de novembro de 2026

Versão de leitura e impressão: [manual-utilizador-android.pdf](manual-utilizador-android.pdf).

Este ficheiro é a fonte editável do conteúdo. O PDF é gerado com o modelo Typst e inclui as ilustrações da interface. Consulte [Editar e gerar os manuais](manuals/README.md).

Registar intervenções de manutenção, reunir evidências e entregar relatórios completos.

**Para utilizadores finais**

Técnicos que utilizam a app no terreno.

## 01 Como consultar este manual

Este manual acompanha a utilização da app Lem, desde a entrada na conta até à entrega de um relatório. Pode seguir o guia rápido ou consultar diretamente a tarefa de que precisa.

### Âmbito e convenções

Na interface, o relatório de intervenção é designado por **report**. Neste manual, as expressões a negrito identificam botões, opções ou estados da app. Utilize o toque para selecionar opções e deslize o dedo para percorrer as perguntas. O botão de retrocesso do sistema permite regressar ao ecrã anterior quando aplicável.

Este manual descreve a versão 0.8.0 e o formulário de manutenção atual. Relatórios antigos ou fechados podem apresentar perguntas diferentes. As capturas utilizam dados fictícios; a disposição varia com o tamanho do ecrã e o sistema operativo. As capturas mostram a app num emulador Android, em orientação vertical.

Este documento explica a aplicação. Os ensaios elétricos e os procedimentos de segurança seguem as instruções técnicas da Lem e devem ser realizados por pessoal qualificado.

### Consultar as notas do contrato

Ao criar um rascunho, o técnico ou administrador vê as **Notas** do PCVE ou da intervenção se houver texto. Com as notas vazias, abre diretamente o formulário. O técnico volta a ver as instruções ao reabrir o report. Escolha **Voltar ao report** para continuar ou consulte as notas no menu do report. A última leitura fica disponível sem rede, mesmo depois de o ID sair do contrato.

O administrador pode abrir o ícone de notas junto à seta do cartão, escolher **Editar notas** e **Guardar**. Para apagar as notas, guarde o campo vazio. No Excel, uma célula **Notas** vazia também apaga a nota do ID. Omitir a coluna conserva o texto. Se a pré-visualização indicar uma fórmula por calcular, recalcule a folha e guarde o ficheiro antes de importar.

## 02 Começar a utilizar a app

### Preparar o dispositivo

Instale a versão disponibilizada pela Lem. Tenha as credenciais da sua conta e confirme que dispõe de espaço livre para fotografias e vídeos. No primeiro acesso, ligue o dispositivo à Internet para obter os contratos e os carregadores atribuídos.

1. Abra a app e toque no **ícone da conta**, no canto superior direito.

2. Confirme a **Ligação ao servidor**. Utilize **Usar servidor Lem** ou o endereço indicado pelo administrador da sua instalação.

3. Preencha **Email** e **Password** e escolha **Entrar**. Se não tiver conta ou acesso ao contrato, contacte o administrador.

4. Em **Contratos**, selecione o contrato e escolha **Atualizar** para obter a informação disponível no servidor.

Ao fechar e reabrir a app, a sessão é retomada quando o servidor a confirma. Se estiver sem rede, pode continuar a usar os dados locais enquanto a app aguarda ligação. O servidor pede novo login após 365 dias ou quando a administração revoga a sessão. Num dispositivo partilhado, escolha **Terminar sessão** quando acabar.

### Alterar a minha password

Com sessão iniciada e ligação ao servidor, abra o **ícone da conta** e escolha **Alterar a minha password**. Introduza a password atual, escreva a nova (12 a 200 caracteres), repita-a e escolha **Guardar password**. A sessão continua ativa neste dispositivo. Nos outros dispositivos, entre com a nova password.

### Repor a password de um técnico (administrador)

Em **Gestão → Contas e acessos**, selecione a conta de um técnico e escolha **Repor password do técnico…**. Introduza a nova password duas vezes (12 a 200 caracteres) e confirme. É necessária ligação e servidor atualizado. O técnico deve voltar a entrar em cada dispositivo; os seus reports e acessos mantêm-se. Esta opção não permite alterar passwords de administradores.

### Guia rápido de uma intervenção

| Etapa | O que fazer |
| --- | --- |
| 1 · Identificar | Confirme o contrato, o ID do carregador, a morada e o número de série. |
| 2 · Registar | Abra **Nova intervenção** ou retome o rascunho existente. Preencha os grupos e associe as evidências a cada pergunta. |
| 3 · Validar | Abra **Rever report** ou **Rever** e resolva os itens por validar. |
| 4 · Entregar | Escolha **Concluir report**, sincronize e confirme a receção pelo servidor. |

## 03 Escolher o contrato e o carregador

1. **Selecione o contrato.** No seletor superior, escolha o contrato da intervenção e utilize **Atualizar** quando tiver rede.

2. **Procure o equipamento.** Em **Pesquisar carregadores…**, introduza o ID, número de série, morada, modelo ou CPE. Limpe a pesquisa para voltar à lista completa.

3. **Confirme a identificação.** Verifique o ID, S/N e morada no cartão. O botão de navegação, quando disponível, abre a localização numa aplicação externa.

4. **Abra a intervenção.** Toque em **Nova intervenção** ou no **Rascunho** que pretende continuar. Deslize verticalmente para consultar os restantes carregadores.

### Consultar PCVEs

Em **PCVEs**, abra a ficha e escolha um dos **Relatórios associados**. **Voltar** regressa ao mesmo ponto da ficha. **Atualizar** guarda a ficha para consulta sem rede. Em **Gestão → Contratos → Gerir IDs do contrato**, o administrador pode **Adicionar ou atualizar por Excel**. Os contratos partilham a ficha técnica do PCVE. **Retirar** conserva a ficha e os relatórios. No cartão, **Fechado 30_03_16** indica 30 de março entre as 16h00 e as 16h59, seguido do nome do relatório.

### Evitar registos duplicados

Retome o rascunho do trabalho em curso, mesmo que a ligação falhe.

Se surgir **Falha ao criar rascunho**, verifique o armazenamento do dispositivo. Escolha **Voltar à lista** e toque novamente em **Nova intervenção**. A app abre o novo rascunho quando consegue guardá-lo.

O cartão **Concluído** indica que esse PCVE tem uma intervenção fechada no contrato. Confirme a entrega no estado de sincronização do próprio relatório.

### Ordens de trabalho

Em **Ordens** (no menu de navegação, **Ordens de trabalho**), escolha uma ordem para consultar os técnicos, as datas e as intervenções. **Informação do ID** mostra a ficha e as notas do contrato antes da visita. **Operador** identifica quem opera o PCVE e **Cliente** identifica o cliente do trabalho. Podem ser entidades diferentes.

O administrador escolhe **Nova ordem**, indica o início e o prazo, seleciona técnicos e acrescenta os IDs por fazer. **Subir** e **Descer** organizam a sequência sugerida. Em **Editar ordem** pode ajustar o prazo, a equipa e as intervenções por fazer. Depois do início, a data inicial fica fixa. Um ID com rascunho em curso permanece na ordem. Antes do dia de início, **Cancelar ordem** e **Confirmar cancelamento** libertam os IDs pendentes. A ordem é eliminada 24 horas após o cancelamento. Os relatórios e os anexos já guardados são conservados.

A partir do dia de início em Lisboa, o técnico pode escolher qualquer ID e **Abrir intervenção**. Para trabalhar sem rede, atualize a ordem após o início enquanto ainda tem ligação. O seu rascunho offline aparece como **Em preenchimento · por sincronizar**. **Consultar rascunho** abre o trabalho do colega identificado na ordem. **Consultar cópia local em conflito** abre o seu trabalho da mesma visita. Se recuperar a atribuição, sincronize. Para entregar o trabalho a outro técnico, escolha **Guardar cópia para entrega**. O ZIP inclui as respostas e os anexos disponíveis no dispositivo. Se a intervenção for impossível, escolha **Assinalar impedimento**, escreva o motivo e confirme em **Marcar inválida**. O administrador pode planear uma nova visita com um rascunho próprio. A última tarefa resolvida regista a hora de fecho. **Recuperar rascunho** abre uma cópia anterior para consulta. **Relatório removido** identifica um documento apagado após o fecho.

Quando perder uma atribuição ou o servidor apagar um report que ainda tem alterações locais, procure **Cópias locais para recuperação** na lista de ordens. Abra a cópia para consulta e escolha **Guardar cópia para entrega** para exportar as respostas e os anexos disponíveis neste dispositivo. Se um PCVE for retirado do contrato, **Consultar rascunho** na ordem continua a abrir o trabalho existente.

O criador segue a ordem. Qualquer administrador pode geri-la ou escolher **Seguir** para receber impedimentos e a conclusão. Autorize as notificações de atribuição e início. O aviso de início é entregue com rede das 07h00 às 20h00 de Lisboa, no próprio dia.

### Corretivas e comissionamentos

Abra **Manutenções corretivas** no menu de navegação ou em **Intervenções**. Para criar um contrato, o administrador escolhe **Contratos de corretivas** e **Importar contrato**. Preencha o cliente, o nome e as datas. Use a primeira folha de um ficheiro XLSX com um ID já registado por linha na coluna A, sem cabeçalho. Um ID desconhecido ou repetido impede a importação de toda a folha.

O nome segue o formato **Ano_Cliente_NomeContrato**, com o ano do início. As datas podem começar a partir de 1 de janeiro do ano atual. A data final pode coincidir com o início ou chegar ao segundo aniversário. Ambos os dias contam para a cobertura do PCVE.

1. Na ficha do PCVE, escolha **Nova corretiva**, ou procure o ID no menu das corretivas. A app sugere o contrato válido com início mais recente. Pode escolher outro contrato válido da lista ou **Sem contrato** para uma visita esporádica.
2. Escreva as **Notas para o técnico** e escolha **Criar corretiva**. Atribua-a na criação ou em **Guardar atribuição**. Para a incluir numa ordem, pesquise **COR-…**.
3. O técnico abre a visita atribuída pela ordem, quando existe. Para entregar um rascunho sincronizado, o administrador escolhe o novo técnico e **Transferir atribuição e rascunho**. A referência, as notas, o rascunho e os anexos enviados seguem juntos.

Para comissionar um PCVE, abra a ficha ou o menu **Comissionamentos** e escolha **Novo comissionamento**. O técnico pode criar o seu próprio comissionamento para um PCVE a que tenha acesso. O administrador pode incluir o comissionamento numa **Nova ordem** ou **Editar ordem**. Pesquise pelo ID do PCVE ou pela referência **COM-…**. Uma ordem pode juntar preventivas, corretivas e comissionamentos do mesmo PCVE. O técnico abre o comissionamento pela ordem quando esta começa. O comissionamento continua sem contrato e usa o mesmo formulário preventivo que a corretiva. O tipo de intervenção aparece no PDF.

## 04 Preencher e navegar no relatório

No topo do relatório encontra o ID do carregador, o estado e a contagem de respostas. A linha de grupos permite mudar de secção.

Em ecrãs compactos, utilize as setas ou abra **Grupos do report** para chegar aos restantes grupos. A app conserva a posição ao mudar de grupo.

Percorra as perguntas de cima para baixo. Cada cartão apresenta o código, a descrição, os controlos de resposta e as evidências aplicáveis.

A contagem de respostas ajuda a acompanhar o progresso. A revisão final verifica também medições, observações e fotografias obrigatórias.

| Ordem | Grupo | Conteúdo |
| --- | --- | --- |
| 1 | SEG | Segurança |
| 2 | SITE | Posto de carregamento |
| 3 | QVE | QVE e botoneira |
| 4 | EST | Estado geral do equipamento |
| 5 | MAN | Manutenção e medições |
| 6 | CARG | Ensaios de carregamento |
| 7 | FIN | Informações finais e técnico |

**OK** regista conformidade; **NOK** regista uma não conformidade; **N/A** significa que o item não é aplicável. Quando escolher NOK, descreva o problema nas observações. Em SEG.02 a SEG.06, o campo aparece automaticamente para NOK; em SEG.02 e SEG.04, aparece também para N/A. Não existe botão de observação nestas perguntas.

## 05 Registar medições e avaliações

Nos campos de medição, introduza o resultado do instrumento. Quando existirem botões OK e NOK junto ao valor, estes registam uma **avaliação separada**. Selecionar OK ou NOK não substitui a leitura obrigatória.

| Campos | Como preencher |
| --- | --- |
| MAN.02 | Selecione OK, NOK ou N/A. NOK exige uma observação. N/A não exige observação. |
| MAN.04 a MAN.10 | Introduza a tensão em V. Aceita vírgula ou ponto decimal, até 3 algarismos antes do separador e 2 casas decimais. |
| MAN.11 | Indique o calibre configurado em **mA**, até 6 algarismos, sem casas decimais; deve ser superior a zero. |
| MAN.12 | Introduza a intensidade medida em **mA**, até 6 algarismos, sem casas decimais, ou selecione N/A. **Anexe uma fotografia quando aplicável. Em N/A, a fotografia é opcional e a observação é obrigatória.** A avaliação é calculada quando MAN.11 contém um calibre válido. |
| MAN.14 a MAN.17 | No campo **Valor medido (ou NA)**, introduza o resultado do isolamento. São possíveis leituras do instrumento como **>500 MΩ** ou **OL**, além de N/A. |
| MAN.18 | Introduza a resistência de terra em Ω, até 5 algarismos inteiros e 2 casas decimais, ou selecione N/A. Ao selecionar N/A, o campo do valor mostra **NA**. A avaliação é calculada a partir de MAN.11, sem substituir a leitura. |
| MAN.19 | Selecione OK, NOK ou N/A. |

### Intensidade diferencial e resistência de terra

**1 A = 1000 mA; 0,03 A = 30 mA.** Em MAN.11 e MAN.12, a vírgula ou o ponto ficam visíveis com um aviso. Escreva um valor inteiro em mA para validar a leitura. Em rascunhos antigos, valores com unidade explícita são convertidos quando possível; um valor sem unidade ou ambíguo é mostrado como **Valor anterior** e deve ser confirmado em mA.

Com MAN.11 válido, MAN.12 fica **NOK** se a leitura for menor ou igual a 50% ou maior ou igual a 105% do calibre configurado. Entre esses limites, fica OK. Exemplo: para 300 mA, 150 mA e 315 mA são NOK. A app sugere «Valor de intensidade de disparo diferencial fora do calibre configurado».

Para MAN.18, a app usa por agora **R máximo = 50 / (MAN.11 / 1000)**, em Ω. Acima desse limite, marca NOK e sugere «Valor de terra de proteção elevado e acima do limite para o calibre da proteção diferencial configurada». Até ao limite, marca OK; se a leitura também for superior a 50 Ω, sugere «Valor de terra de proteção elevado mas dentro de limites para o calibre da proteção diferencial configurada». Para 300 mA, 166,66 Ω fica OK e 166,67 Ω fica NOK. A igualdade ao limite é aceite.

**O critério temporário de 50 V não cobre todas as instalações.** As RTIEBT, 413.1.4.2, estabelecem RA × Ia ≤ 50 V no esquema TT. O Guia Técnico CTE64, edição 3, 5.3.2 (página 28), prevê **25 V** para postos acessíveis ao público no exterior. Consulte as referências na secção 16; confirme o caso aplicável antes de interpretar o resultado.

Mudar MAN.11 recalcula MAN.12 e MAN.18. O servidor verifica o mesmo cálculo ao guardar e fechar. N/A retira a avaliação calculada; voltar a uma medição exige uma leitura válida. Uma avaliação NOK exige observação e, quando aplicável, fotografia.

### Indicações de tensão fora do intervalo

| Medição | Intervalo de referência da app |
| --- | --- |
| MAN.04 a MAN.06 | 360 a 440 V, inclusive |
| MAN.07 a MAN.09 | 207 a 253 V, inclusive |
| MAN.10 | Valores iguais ou superiores a 50 V são assinalados fora do intervalo. |

Uma tensão fora do intervalo pode ser registada e fica assinalada. A indicação ajuda a revisão e não substitui a avaliação técnica.

## 06 Adicionar fotografias e vídeos

1. Abra a pergunta a que a evidência pertence. Confirme o código antes de anexar o ficheiro.

2. Utilize **Tirar fotografia** para captar no local ou **Galeria** para escolher uma imagem existente. Para adicionar um vídeo, utilize o botão de vídeo junto à pergunta correspondente.

3. Se o seletor permitir, escolha vários ficheiros. Aguarde o processamento e leia o resultado: um ficheiro recusado não impede o processamento dos restantes.

4. Verifique a miniatura e a legibilidade do detalhe relevante. Uma fotografia do equipamento deve permitir identificar claramente o que pretende comprovar.

### Consultar e corrigir uma evidência

Nas fotografias disponíveis no dispositivo, use **Editar fotografia** para abrir a pré-visualização. O botão com a **seta circular**, sem texto, roda 90° para a direita. O botão com o **símbolo de recorte**, também sem texto, ativa ou desativa o recorte; fica colorido quando ativo. O recorte é opcional: arraste os cantos da seleção apenas se precisar. Pode combinar as duas operações e confirmar em **Guardar**. **Repor** desfaz os ajustes e **Cancelar** sai sem alterar a fotografia. O original da galeria permanece intacto. Também pode ampliar ou eliminar a fotografia; se ainda não estiver disponível localmente, descarregue-a primeiro.

Os vídeos têm controlos de reprodução ou descarga. Confirme o enquadramento e a duração antes de enviar. A assinatura não pode ser recortada nem rodada; para a substituir, registe uma nova assinatura em FIN.02.

| Tipo ou limite | Utilização |
| --- | --- |
| Fotografias | Prefira JPG/JPEG ou PNG. Limite por ficheiro: 20 MiB. |
| Vídeos | MP4 ou 3GP, até 120 segundos e 60 MiB por ficheiro. |
| Total por relatório | Até 200 evidências e 400 MiB no conjunto dos ficheiros. |

**Evidência obrigatória significa sempre fotografia:** um vídeo não cumpre este requisito. Em MAN.14 a MAN.18, anexe uma fotografia para OK e NOK; em N/A, escreva uma observação. Associe uma fotografia à própria pergunta que a exige. N/A dispensa a fotografia, incluindo em **MAN.12**.

### Fotografias antes e depois da manutenção

Em **EST.01**, registe o interior, exterior e estado de limpeza antes da tarefa, mostrando todos os componentes. Em **MAN.21**, registe as mesmas vistas e a limpeza depois da manutenção. **EST.10 foi retirado**. EST.13 verifica apenas o tarifário e EST.16 apenas os autocolantes da DGEG.

### Observações gerais opcionais

**SEG.07**, **SITE.06**, **QVE.14**, **EST.18**, **MAN.22**, **CARG.07** e **FIN.03** permitem observações, fotografias e vídeos sem botões OK/NOK/N/A. Podem ficar vazios. Em **SEG.02 a SEG.06** não existem novos anexos nem botão de observação. A observação é obrigatória para NOK e, em **SEG.02 e SEG.04**, também para N/A.

## 07 Registar o nome e a assinatura

Em **FIN.02**, introduza o nome do técnico antes de utilizar os controlos de assinatura. Confirme o nome antes de assinar. Pode guardar ou substituir a assinatura durante a sincronização em segundo plano; o envio não elimina a nova assinatura.

1. **Abra a área de assinatura.** Toque em **Assinar** depois de preencher o nome.

2. **Desenhe a assinatura.** Se necessário, toque em **Limpar** e repita o desenho.

3. **Guarde e confirme.** Toque em **Guardar** e verifique que a assinatura ficou associada ao nome correto.

Em **FIN.02**, pode escrever uma observação. Os botões gerais de fotografia e vídeo não aparecem; utilize os controlos próprios para a assinatura.

### Confirmar ou substituir a assinatura

A assinatura é registada como imagem associada ao nome do técnico e à data. Alterar o nome invalida a assinatura anterior; nesse caso, volte a assinar.

Uma fotografia anexada a outra pergunta não substitui a assinatura de FIN.02. Para a substituir, utilize novamente o controlo **Assinar**. **A assinatura do técnico em FIN.02 é obrigatória para fechar o report.** Pode guardar um rascunho sem assinatura. Confirme que a assinatura corresponde ao nome indicado; se alterar o nome, assine novamente.

### Utilizar o ecrã tátil

Se o teclado estiver aberto após escrever o nome, feche-o para ter mais espaço. Evite sair da área de desenho enquanto está a assinar e confirme a legibilidade antes de guardar.

## 08 Copiar dados de outro carregador

A app permite reutilizar dados de **outro carregador do mesmo contrato**. Confirme no local se os resultados continuam aplicáveis ao equipamento de destino. A cópia não altera o relatório de origem.

| Opção | O que é copiado |
| --- | --- |
| Copiar QVE de outro carregador… | Todo o grupo QVE, incluindo QVE.14, com respostas, observações e fotografias/vídeos associados. |
| Copiar Medições de outro carregador… | Apenas MAN.04 a MAN.10, com observações e evidências associadas. Os outros itens MAN e MAN.22 mantêm-se. |

1. No relatório de destino, abra o grupo **QVE** ou **MAN** e utilize a opção de cópia correspondente.

2. Selecione o carregador/relatório de origem. Leia a identificação e confirme a aplicabilidade da informação.

3. Confirme a cópia. Após a preparação dos ficheiros, são substituídos os dados do âmbito escolhido: todo o QVE na cópia QVE, ou apenas MAN.04 a MAN.10 na cópia de medições. Os restantes itens MAN mantêm-se.

4. Aguarde o resultado da operação e reveja os valores, as observações e as evidências no destino. Complete ou corrija o que não se aplica.

### Copiar QVE e medições de origens diferentes

Pode pedir a cópia do outro grupo enquanto a primeira está em curso e escolher uma origem diferente. A segunda operação aguarda a conclusão da primeira. Pode continuar a editar os campos que não estão a ser substituídos.

**Mantenha a app aberta até terminar.** O pedido em espera não é conservado após reiniciar a aplicação. Durante a cópia, o fecho do relatório e a mudança de relatório ou de sessão ficam limitados.

### Se a cópia não terminar

Pode ser necessária Internet para descarregar evidências da origem. Se ocorrer um erro na preparação, a app conserva os dados anteriores no âmbito de destino. Leia a mensagem, confirme a ligação e repita a operação quando necessário. Não crie outro relatório apenas para contornar a falha.

## 09 Rever e concluir a intervenção

1. **Abra a revisão.** No rodapé do relatório, escolha **Rever report** ou **Rever**. A lista indica os itens por validar.

2. **Corrija cada pendência.** Escolha **Abrir item** para regressar à pergunta. Preencha a resposta ou leitura, a observação exigida e a fotografia em falta.

3. **Repita a revisão.** Confirme que já não existem itens por validar. A conclusão fica disponível quando os requisitos estão cumpridos e as operações de cópia/anexação terminaram.

4. **Conclua a intervenção.** Escolha **Concluir report** quando os dados estiverem corretos. Em seguida, sincronize e confirme o estado do envio.

### Pendências que merecem atenção

**NOK:** exige uma observação. **N/A:** exige motivo em SEG.01, SEG.02, SEG.04; SITE.02 a SITE.04; QVE.02, QVE.04, QVE.09 a QVE.12; EST.01, EST.15, EST.16, EST.17; MAN.12 a MAN.18, MAN.20, MAN.21; cada ensaio CARG.01 e CARG.05. **MAN.02:** N/A não exige observação. **MAN.12:** N/A exige motivo, mas dispensa fotografia. Em SEG.02 a SEG.06, o campo obrigatório aparece automaticamente, sem botão de observação.

Uma medição com apenas OK/NOK pode continuar incompleta. Um vídeo, ou uma fotografia colocada noutra pergunta, não resolve uma fotografia obrigatória em falta.

As sugestões de observação para N/A estão listadas na secção 14.

### Depois do fecho

O relatório fechado passa a ser consultado como registo da intervenção. Um administrador pode criar uma correção no desktop; essa cópia passa ao formulário e regras atuais. O original mantém-se até a correção ser aceite. O estado de fecho no dispositivo pode anteceder a confirmação pelo servidor. Antes de considerar o trabalho entregue, siga a verificação da secção 10.

## 10 Confirmar a sincronização

A app guarda as alterações no dispositivo e organiza o envio em segundo plano. Respostas e ficheiros podem ser enviados em momentos diferentes. Utilize o botão **Sinc.**, identificado pelo símbolo de sincronização, para pedir o envio manual e acompanhe as mensagens da operação.

Pode voltar ao ecrã inicial ou mudar de aplicação enquanto descarrega evidências. Permita as notificações para acompanhar a transferência e tocar na notificação para regressar à Lem. Com sessão iniciada, o Android agenda a atualização dos contratos, dos carregadores do contrato selecionado e da base de IDs de PCVEs. A rede e a poupança de bateria podem adiar a atualização. Se encerrar a Lem ou se o sistema terminar o processo, reabra a app e repita o descarregamento para obter as evidências em falta.

| Indicação | O que significa e o que fazer |
| --- | --- |
| A guardar no dispositivo… | A gravação local ainda está em curso. Aguarde. |
| Guardado no dispositivo · por sincronizar | Existem alterações guardadas apenas neste dispositivo. Conserve os dados e sincronize quando tiver ligação. |
| Sincronizado | O relatório não apresenta alterações locais pendentes. Depois de concluir, confirme também que o fecho foi aceite pelo servidor e que não existe erro de envio. |
| A enviar / transferência em curso | Aguarde as respostas e as evidências. Consulte **Estado da operação** no menu do relatório. |
| Erro ao guardar · mantenha a app aberta | Não limpe dados, não desinstale e não reinicie por iniciativa própria. Registe a mensagem e contacte o suporte. |

### Confirmar a entrega

1. Confirme a ligação à Internet e a sessão da conta. Após reabrir a app, pode ser necessário voltar a **Entrar**, mesmo que os dados locais continuem visíveis.

2. Peça **Sinc.** e aguarde o fim das operações. Corrija qualquer mensagem que exija intervenção.

3. Verifique que o relatório está fechado, sincronizado e sem erro de envio. Quando houver dúvida, consulte a versão do servidor antes de limpar cópias ou mudar de dispositivo.

### Quando a rede falha

Conserve o relatório existente e tente novamente com ligação estável. Não apague um rascunho, não limpe os dados da app e não crie duplicados para resolver uma falha temporária de envio.

Se uma ordem futura adiar o envio, a app conserva o rascunho e volta a tentar quando a ordem começa ou é cancelada. Use **Sinc.** para verificar de imediato.

## 11 Preparar o trabalho sem rede

### Antes de ficar sem ligação

1. Entre na conta com rede e atualize o contrato. Abra os relatórios de que vai precisar para confirmar que estão disponíveis no dispositivo.

2. No menu do relatório, escolha **Descarregar todas as evidências**. Confirme a descarga e aguarde o resultado.

3. Abra algumas fotografias/vídeos necessários para verificar a disponibilidade. A presença da lista ou das miniaturas não garante que todos os originais estejam descarregados.

Os relatórios descarregados por esta opção ficam mantidos offline e protegidos da limpeza automática. Para retirar essa proteção, utilize **Permitir limpeza destas fotografias** no menu do relatório.

### Durante o trabalho sem rede

Pode continuar a trabalhar sobre os dados disponíveis localmente. A cópia de dados de outro carregador e a consulta de evidências dependem de os ficheiros de origem já estarem presentes. As exportações do servidor requerem ligação.

Ao recuperar a ligação, entre novamente na conta se necessário, sincronize e confirme a entrega. Evite editar o mesmo trabalho em vários dispositivos enquanto existem alterações por enviar.

### Proteger os dados no dispositivo

A cache offline da app não é cifrada pela própria aplicação e não exige biometria. Proteja o acesso à conta do sistema e ao dispositivo. No Android, as cópias de segurança e transferências do sistema não incluem os dados privados da app; não conte com elas para recuperar rascunhos por enviar. Sincronize e confirme a receção antes de desinstalar ou trocar de dispositivo.

### Libertar espaço com segurança

No menu da conta, **Limpar cópias locais** remove fotografias e vídeos privados da app que já foram confirmados pelo servidor e cumprem as condições de limpeza. Inclui cópias que estavam mantidas offline. Os relatórios, as respostas e o histórico permanecem disponíveis.

A limpeza não apaga os originais da galeria nem as evidências do servidor. Trabalho pendente, em conflito ou sem confirmação mantém os ficheiros protegidos. Depois da limpeza, será necessária rede para voltar a descarregar os originais removidos.

**Antes de desinstalar, limpar os dados da app ou mudar de telemóvel ou tablet:** confirme que todo o trabalho foi recebido pelo servidor. Os rascunhos não enviados não são recuperados pela simples reinstalação da app.

## 12 Exportar e consultar o histórico

### Guardar uma cópia para consulta ou entrega

1. Abra um relatório **fechado** e o respetivo menu de opções.

2. Escolha **Exportar PDF** para o documento de leitura ou **PDF + evidências (ZIP)** para o conjunto com os ficheiros originais.

3. Escolha o destino no seletor de ficheiros do sistema e aguarde a confirmação. Abra o ficheiro guardado antes de o partilhar.

| Formato | Conteúdo e utilização |
| --- | --- |
| PDF | Relatório para leitura e impressão, com os resultados e as evidências apresentadas no documento. Os vídeos não são reproduzidos no PDF. |
| ZIP | Inclui o PDF, os dados do relatório e as fotografias/vídeos originais organizados por pergunta. Utilize-o quando a entrega exigir os ficheiros originais. |

O nome dos ficheiros junta o ID do carregador e o nome do contrato no formato **ID_Contrato**. Fotografias e vídeos acrescentam o seu identificador. As datas constam do relatório.

**PDF e ZIP sem rede:** são gerados no dispositivo se o relatório estiver fechado e tiver todas as evidências locais. Se faltarem ficheiros, a exportação usa o servidor e exige ligação, sessão iniciada e relatório sincronizado. Com ligação, a app confirma se o relatório foi substituído ou apagado antes de exportar. Confirme o conteúdo antes de o entregar.

**Exportação de um contrato inteiro:** está disponível na app para desktop Windows/Linux. No Android, exporte cada report pelo respetivo menu.

### Consultar intervenções anteriores

Na lista de carregadores, abra o relatório fechado que pretende consultar. Os relatórios anteriores conservam o formulário e a informação histórica guardados na intervenção. Por isso, podem apresentar uma ordem ou perguntas diferentes das de um rascunho atual.

Em **EST.04**, pode indicar vários números de série do carregador e dos dispensers no mesmo campo; a fotografia desse item é opcional.

Quando existir uma correção de fabricante, modelo ou número de série nos campos EST.02 a EST.04, o fecho aceite pode atualizar a identificação do equipamento. Confirme esses dados com cuidado; não utilize um valor genérico para ultrapassar a validação.

### Criar uma nova intervenção a partir de outra

A opção **Criar rascunho como cópia** cria um relatório independente para revisão. Confirme o carregador, a data, os resultados e todas as evidências antes de concluir o novo trabalho. Uma cópia não comprova, por si só, que os ensaios foram repetidos.

## 13 Resolver dificuldades frequentes

| Situação | O que fazer |
| --- | --- |
| O contrato ou o carregador não aparece | Confirme a conta e o contrato selecionados, limpe a pesquisa e escolha **Atualizar**. Se continuar em falta, peça ao administrador para confirmar a atribuição. |
| Vejo dados locais mas não consigo enviar | Abra a conta e volte a **Entrar**. Confirme a ligação e o endereço do servidor e peça **Sinc.**. |
| Não consigo concluir | Abra **Rever** e use **Abrir item**. Confirme leituras, justificações NOK/N/A e fotografias. Verifique o relógio se o fim for anterior ao início. Se atingir o limite de texto, reduza as observações no mesmo rascunho. Em MAN.02, substitua texto de um rascunho antigo por OK, NOK ou N/A. Aguarde cópias e anexações em curso. |
| Uma fotografia ou um vídeo foi recusado | Leia o motivo. Verifique o formato, o tamanho e a duração. Para uma fotografia incompleta, selecione novamente o original. Experimente JPG/PNG para imagens ou MP4/3GP para vídeos. Confirme os restantes ficheiros aceites. |
| A evidência está na lista mas não abre | O original pode não estar no dispositivo. Ligue-se à rede e descarregue a evidência ou todas as evidências do relatório. |
| A câmara não abre no Android | Confirme que a câmara do dispositivo funciona e aceite as autorizações pedidas pelo Android. Em alternativa, escolha uma fotografia da galeria. |
| Assinar está desativado | Preencha primeiro o nome em FIN.02 e confirme que o relatório pode ser editado. Se alterar o nome, registe a assinatura novamente. |
| Existe um conflito ou o proprietário mudou | Compare o trabalho local com o servidor. Preserve uma cópia independente antes de escolher uma resolução que substitua dados locais. Peça ajuda ao administrador se o relatório passou para outro técnico. |
| Surge um erro de leitura ou gravação local | Mantenha a app e os seus dados. Não apague ficheiros nem reinstale para tentar reparar. Envie a mensagem e a identificação do relatório ao suporte. |

**Pedido de suporte:** indique a versão da app, o sistema operativo, o contrato, o ID do carregador/relatório, a hora e a mensagem apresentada. Envie uma captura do erro quando possível, evitando palavras-passe e dados desnecessários.

A eliminação permanente de um relatório no servidor não é revertida por uma cópia antiga no dispositivo. As opções de apagar relatórios e substituir versões devem ser utilizadas apenas quando essa é a intenção.

## 14 Referência para o fim da intervenção

### Fotografias obrigatórias no formulário atual

| Grupo | Itens com fotografia obrigatória quando aplicáveis |
| --- | --- |
| SEG | SEG.01 |
| SITE | SITE.02 a SITE.04 |
| QVE | QVE.01 e QVE.04 |
| EST | EST.01, EST.05, EST.06, EST.13 e EST.16 |
| MAN | MAN.04 a MAN.10, MAN.12, MAN.14 a MAN.18 e MAN.21 |
| CARG | Cada ensaio CARG.01 de conector e CARG.05 |
| FIN | FIN.01 |

N/A dispensa a fotografia, incluindo em **MAN.12**, mas pode exigir uma observação. Consulte os itens e motivos da secção 9. A revisão aplica as regras do relatório aberto.

### Observações sugeridas ao selecionar N/A

Ao selecionar N/A com a observação vazia, a app sugere os motivos abaixo. Confirme e adapte o texto à situação real. Ao mudar o estado da pergunta, a sugestão automática inalterada é removida. Ao tocar ou clicar dentro do campo para escrever, apenas o texto sugerido é apagado imediatamente; o campo de observação continua aberto e com o cursor ativo para escrever. Uma observação escrita ou modificada pelo utilizador mantém-se, mesmo que tenha o mesmo texto de uma sugestão. Esta regra aplica-se a todas as perguntas com observações sugeridas, incluindo as sugestões das medições. O botão de observação pede confirmação antes de apagar o texto, mesmo quando obrigatório. Nesse caso, o campo fica visível e vazio e impede a conclusão até ser preenchido. Em **QVE.03**, a observação é opcional.

| Item | Sugestão editável |
| --- | --- |
| SITE.02 | Sem sinalização horizontal |
| SITE.03 | Sem sinalética vertical |
| SITE.04 | Sem proteção mecânica |
| QVE.02, QVE.11, QVE.12, MAN.12 e MAN.13 | Não testado devido a veículo em carga |
| QVE.03 | Botoneira sem dupla sinalização |
| QVE.09 | Apertos não verificados devido a veículo em carga |
| QVE.10 | QVE sem sinalética |
| MAN.20 | Apertos não verificados devido a veículo em carga |
| Cada ensaio CARG.01 de conector | Veículo usado não compatível |
| EST.15 | Sem TPM |

### Verificação antes de sair do local

1. Confirme o contrato e a identificação física do carregador.

2. Verifique as leituras, as unidades e as avaliações OK/NOK. Justifique as não conformidades.

3. Abra as fotografias obrigatórias e confirme a legibilidade. Verifique também os vídeos que anexou.

4. Confirme o nome do técnico e a assinatura obrigatória em FIN.02.

5. Execute a revisão, conclua e confirme a sincronização. Se estiver sem rede, mantenha o trabalho neste dispositivo até conseguir enviá-lo.

### Termos usados na app

**Rascunho:** relatório em preparação. **Evidência:** fotografia ou vídeo associado a uma pergunta. **Sincronizar:** trocar alterações com o servidor. **Offline:** utilizar os dados presentes no dispositivo sem ligação. **Conflito:** situação em que as versões ou as permissões exigem uma decisão antes de prosseguir.

Mantenha o dispositivo protegido e partilhe os relatórios apenas pelos canais da Lem. Para contas, acessos, correções de propriedade ou recuperação de dados, contacte o administrador ou o suporte responsável pela instalação.

## 15 Referência completa dos campos

Esta referência abrange todos os campos do formulário atual. A secção 5 detalha o formato das medições; a secção 14 apresenta os motivos sugeridos para N/A. Os códigos e o número de ensaios de carga dependem dos conectores do carregador.

**Regras comuns:** todos os campos exigem resposta, exceto as observações gerais indicadas como opcionais. NOK, como resposta ou avaliação, exige observação. A coluna Requisitos indica se N/A também exige observação e se é necessária fotografia. Um vídeo não substitui uma fotografia obrigatória. N/A dispensa a fotografia. As regras do relatório aberto prevalecem em relatórios antigos.


### SEG - Segurança

| Campo e verificação | Como preencher | Requisitos |
| --- | --- | --- |
| **SEG.01** — Área delimitada de segurança. | Selecione OK, NOK ou N/A. | N/A: observação obrigatória. Fotografia obrigatória quando aplicável. |
| **SEG.02** — Tem os EPIs necessários para realizar o serviço? | Selecione OK, NOK ou N/A. | N/A: observação obrigatória. Sem anexos de fotografia ou vídeo. Campo abre quando obrigatório, sem botão. |
| **SEG.03** — Os EPIs estão em bom estado e dentro da validade? | Selecione OK, NOK ou N/A. | N/A: observação opcional. Sem anexos de fotografia ou vídeo. Campo abre quando obrigatório, sem botão. |
| **SEG.04** — Tem ferramentas manuais isoladas, multímetro e analisador de multifunções? | Selecione OK, NOK ou N/A. | N/A: observação obrigatória. Sem anexos de fotografia ou vídeo. Campo abre quando obrigatório, sem botão. |
| **SEG.05** — Tem os elementos para aplicar o procedimento Lockout/Tagout? | Selecione OK, NOK ou N/A. | N/A: observação opcional. Sem anexos de fotografia ou vídeo. Campo abre quando obrigatório, sem botão. |
| **SEG.06** — Tem condições de trabalho adequadas para realizar o serviço em segurança (chuva, vento, visibilidade, etc...)? | Selecione OK, NOK ou N/A. | N/A: observação opcional. Sem anexos de fotografia ou vídeo. Campo abre quando obrigatório, sem botão. |
| **SEG.07** — Observações gerais de segurança | Observações, fotografias e vídeos opcionais. Pode ficar vazio. | Sem OK/NOK/N/A. |


### SITE - Posto de carregamento

| Campo e verificação | Como preencher | Requisitos |
| --- | --- | --- |
| **SITE.01** — Verifique a limpeza geral do posto de carregamento | Selecione OK, NOK ou N/A. | N/A: observação opcional. Fotografia opcional. |
| **SITE.02** — Verifique o estado da pintura / sinalização horizontal | Selecione OK, NOK ou N/A. | N/A: observação obrigatória. Fotografia obrigatória quando aplicável. |
| **SITE.03** — Verifique o estado da sinalética vertical (carregamento VE e números de lugares) | Selecione OK, NOK ou N/A. | N/A: observação obrigatória. Fotografia obrigatória quando aplicável. |
| **SITE.04** — Verifique o estado da proteção mecânica (apertos, retrorrefletores, estado geral) | Selecione OK, NOK ou N/A. | N/A: observação obrigatória. Fotografia obrigatória quando aplicável. |
| **SITE.05** — Verifique possíveis sinais de vandalismo (cabos cortados, fechadura forçada, carregador danificado, etc.) | Selecione OK, NOK ou N/A. | N/A: observação opcional. Fotografia opcional. |
| **SITE.06** — Observações gerais do local | Observações, fotografias e vídeos opcionais. Pode ficar vazio. | Sem OK/NOK/N/A. |


### QVE - QVE e Botoneira

| Campo e verificação | Como preencher | Requisitos |
| --- | --- | --- |
| **QVE.01** — Botoneira de emergência - Verifique integridade | Selecione OK, NOK ou N/A. | N/A: observação opcional. Fotografia obrigatória quando aplicável. |
| **QVE.02** — Botoneira de emergência - Verifique o correto funcionamento | Selecione OK, NOK ou N/A. | N/A: observação obrigatória. Fotografia opcional. |
| **QVE.03** — Botoneira de emergência - Verifique a dupla sinalização | Selecione OK, NOK ou N/A. | N/A: observação opcional. Fotografia opcional. |
| **QVE.04** — QVE - Fotografias (QVE aberto sem espelhos) | Selecione OK, NOK ou N/A. | N/A: observação obrigatória. Fotografia obrigatória quando aplicável. |
| **QVE.05** — QVE - Revisão do grau de proteção IP (isolamento, fechaduras, etc.) | Selecione OK, NOK ou N/A. | N/A: observação opcional. Fotografia opcional. |
| **QVE.06** — QVE - Verifique a inexistência de condensação ou água no interior | Selecione OK, NOK ou N/A. | N/A: observação opcional. Fotografia opcional. |
| **QVE.07** — QVE - Verifique a inexistência de sinais de sobreaquecimento no interior | Selecione OK, NOK ou N/A. | N/A: observação opcional. Fotografia opcional. |
| **QVE.08** — QVE - Verifique a limpeza interna e externa | Selecione OK, NOK ou N/A. | N/A: observação opcional. Fotografia opcional. |
| **QVE.09** — QVE - Verifique apertos | Selecione OK, NOK ou N/A. | N/A: observação obrigatória. Fotografia opcional. |
| **QVE.10** — QVE - Verifique sinalética QVE (quadro elétrico e corte geral) | Selecione OK, NOK ou N/A. | N/A: observação obrigatória. Fotografia opcional. |
| **QVE.11** — Teste o correto funcionamento do interruptor geral | Selecione OK, NOK ou N/A. | N/A: observação obrigatória. Fotografia opcional. |
| **QVE.12** — Teste o correto funcionamento do(s) disjuntor(es) | Selecione OK, NOK ou N/A. | N/A: observação obrigatória. Fotografia opcional. |
| **QVE.13** — Verifique que o esquema unifilar está presente no QVE | Selecione OK, NOK ou N/A. | N/A: observação opcional. Fotografia opcional. |
| **QVE.14** — QVE - Observações gerais | Observações, fotografias e vídeos opcionais. Pode ficar vazio. | Sem OK/NOK/N/A. |


### EST - Estado Geral — EST.01 a EST.09

| Campo e verificação | Como preencher | Requisitos |
| --- | --- | --- |
| **EST.01** — Fotografias do interior e exterior do carregador e sua limpeza antes da tarefa executada (visualizar todos os componentes internos e externos) | Selecione OK, NOK ou N/A. | N/A: observação obrigatória. Fotografia obrigatória quando aplicável. |
| **EST.02** — Indique o fabricante do carregador | Escreva o fabricante. | Texto obrigatório. Fotografias e vídeos opcionais. |
| **EST.03** — Indique o modelo completo do carregador | Escreva o modelo completo. | Texto obrigatório. Fotografias e vídeos opcionais. |
| **EST.04** — Indique o(s) número(s) de série do carregador (e dispensers, caso aplicável) | Registe os números de série do carregador e dos dispensers aplicáveis. | Texto obrigatório. Fotografias e vídeos opcionais. |
| **EST.05** — Fotografia da chapa de características | Selecione OK, NOK ou N/A. | N/A: observação opcional. Fotografia obrigatória quando aplicável. |
| **EST.06** — Verifique o ID da estação | Selecione OK, NOK ou N/A. | N/A: observação opcional. Fotografia obrigatória quando aplicável. |
| **EST.07** — Revisão do grau de proteção IP (bucins de cabos de borracha ou espuma em boas condições?) | Selecione OK, NOK ou N/A. | N/A: observação opcional. Fotografia opcional. |
| **EST.08** — Verifique a inexistência de condensação ou água no ecrã ou interior do carregador | Selecione OK, NOK ou N/A. | N/A: observação opcional. Fotografia opcional. |
| **EST.09** — Verifique a inexistência de sinais de sobreaquecimento no interior do carregador; utilizar termografia para melhor despiste | Selecione OK, NOK ou N/A. | N/A: observação opcional. Fotografia opcional. |


### EST - Estado Geral — EST.11 a EST.18

| Campo e verificação | Como preencher | Requisitos |
| --- | --- | --- |
| **EST.11** — Verifique o estado dos vinis de decoração | Selecione OK, NOK ou N/A. | N/A: observação opcional. Fotografia opcional. |
| **EST.12** — Verifique o estado físico dos componentes do carregador (conectores, suportes, ecrã, etc.) | Selecione OK, NOK ou N/A. | N/A: observação opcional. Fotografia opcional. |
| **EST.13** — Verifique se o tarifário está visível e em boas condições | Selecione OK, NOK ou N/A. | N/A: observação opcional. Fotografia obrigatória quando aplicável. |
| **EST.14** — Verifique se o ecrã funciona corretamente | Selecione OK, NOK ou N/A. | N/A: observação opcional. Fotografia opcional. |
| **EST.15** — Verifique se o terminal de pagamento (TPM) está instalado e em boas condições | Selecione OK, NOK ou N/A. | N/A: observação obrigatória com a sugestão «Sem TPM». Fotografia opcional. |
| **EST.16** — Verifique se os autocolantes da DGEG estão visíveis e em boas condições | Selecione OK, NOK ou N/A. | N/A: observação obrigatória. Fotografia obrigatória quando aplicável. |
| **EST.17** — Verifique se todo o hardware da estação de carregamento está a funcionar corretamente | Selecione OK, NOK ou N/A. | N/A: observação obrigatória. Fotografia opcional. |
| **EST.18** — Observações gerais do equipamento | Observações, fotografias e vídeos opcionais. Pode ficar vazio. | Sem OK/NOK/N/A. |


### MAN - Manutenção — MAN.01 a MAN.10

| Campo e verificação | Como preencher | Requisitos |
| --- | --- | --- |
| **MAN.01** — Atualize o software da estação de carregamento (indicar a versão ou NA) | Indique a versão de software ou NA. | Texto obrigatório. Fotografias e vídeos opcionais. |
| **MAN.02** — Limpeza dos módulos de potência | Selecione OK, NOK ou N/A. | N/A: observação opcional. Fotografia opcional. |
| **MAN.03** — Material utilizado | Liste o material utilizado ou indique que não utilizou material. | Texto obrigatório. Fotografias e vídeos opcionais. |
| **MAN.04** — Medir valor de tensão L1 - L2 (360-440 V) e registar foto | Introduza a tensão em V ou N/A. Consulte o formato na secção 5. | N/A: observação opcional. Fotografia obrigatória quando aplicável. |
| **MAN.05** — Medir valor de tensão L1 - L3 (360-440 V) e registar foto | Introduza a tensão em V ou N/A. Consulte o formato na secção 5. | N/A: observação opcional. Fotografia obrigatória quando aplicável. |
| **MAN.06** — Medir valor de tensão L2 - L3 (360-440 V) e registar foto | Introduza a tensão em V ou N/A. Consulte o formato na secção 5. | N/A: observação opcional. Fotografia obrigatória quando aplicável. |
| **MAN.07** — Medir valor de tensão N - L1 (207-253 V) e registar foto | Introduza a tensão em V ou N/A. Consulte o formato na secção 5. | N/A: observação opcional. Fotografia obrigatória quando aplicável. |
| **MAN.08** — Medir valor de tensão N - L2 (207-253 V) e registar foto | Introduza a tensão em V ou N/A. Consulte o formato na secção 5. | N/A: observação opcional. Fotografia obrigatória quando aplicável. |
| **MAN.09** — Medir valor de tensão N - L3 (207-253 V) e registar foto | Introduza a tensão em V ou N/A. Consulte o formato na secção 5. | N/A: observação opcional. Fotografia obrigatória quando aplicável. |
| **MAN.10** — Medir valor de tensão N - T (<50 V) e registar foto | Introduza a tensão em V ou N/A. Consulte o formato na secção 5. | N/A: observação opcional. Fotografia obrigatória quando aplicável. |


### MAN - Manutenção — MAN.11 a MAN.22

| Campo e verificação | Como preencher | Requisitos |
| --- | --- | --- |
| **MAN.11** — Proteção diferencial QVE e Carregador - Indicar a intensidade de disparo configurada | Introduza o calibre em mA, de 1 a 999999, sem decimais, ou N/A. | Texto obrigatório. Fotografias e vídeos opcionais. |
| **MAN.12** — Proteção diferencial QVE e Carregador - Indique o valor da intensidade de disparo com testador | Registe a intensidade em mA, até 6 algarismos e sem decimais, ou N/A. Avaliação calculada com MAN.11. | N/A: observação obrigatória. Fotografia obrigatória quando aplicável. |
| **MAN.13** — Proteção diferencial QVE e Carregador - Teste manual do dispositivo | Selecione OK, NOK ou N/A. | N/A: observação obrigatória. Fotografia opcional. |
| **MAN.14** — Teste de isolamento: Terra - L1 (>500 MΩ) | Registe o isolamento (por exemplo, >500 MΩ ou OL) ou N/A. Avaliação OK/NOK separada. | N/A: observação obrigatória. Fotografia obrigatória para OK e NOK; dispensada em N/A. |
| **MAN.15** — Teste de isolamento: Terra - L2 (>500 MΩ) | Registe o isolamento (por exemplo, >500 MΩ ou OL) ou N/A. Avaliação OK/NOK separada. | N/A: observação obrigatória. Fotografia obrigatória para OK e NOK; dispensada em N/A. |
| **MAN.16** — Teste de isolamento: Terra - L3 (>500 MΩ) | Registe o isolamento (por exemplo, >500 MΩ ou OL) ou N/A. Avaliação OK/NOK separada. | N/A: observação obrigatória. Fotografia obrigatória para OK e NOK; dispensada em N/A. |
| **MAN.17** — Teste de isolamento: Terra - N (>500 MΩ) | Registe o isolamento (por exemplo, >500 MΩ ou OL) ou N/A. Avaliação OK/NOK separada. | N/A: observação obrigatória. Fotografia obrigatória para OK e NOK; dispensada em N/A. |
| **MAN.18** — Valor da resistência da terra | Registe a resistência em Ω ou N/A. O valor mostra NA em N/A. Avaliação calculada com MAN.11 e o critério de 50 V. | N/A: observação obrigatória. Fotografia obrigatória para OK e NOK; dispensada em N/A. |
| **MAN.19** — Verifique o estado dos filtros | Selecione OK, NOK ou N/A. | N/A: observação opcional. Fotografia opcional. |
| **MAN.20** — Verifique todos os apertos do carregador (fontes de alimentação, disjuntores diferenciais, disjuntores, contadores, placas de hardware, etc.) | Selecione OK, NOK ou N/A. | N/A: observação obrigatória. Fotografia opcional. |
| **MAN.21** — Fotografias do interior e exterior do carregador e sua limpeza depois da tarefa executada (visualizar todos os componentes internos e externos) | Selecione OK, NOK ou N/A. | Fotografia obrigatória quando aplicável; N/A exige observação. |
| **MAN.22** — Observações gerais da manutenção | Observações, fotografias e vídeos opcionais. Pode ficar vazio. | Sem OK/NOK/N/A. |


### CARG - Testes de Carga

No grupo **CARG**, existe um ensaio **CARG.01.xx** por conector, mesmo quando vários conectores são do mesmo tipo. Os conectores de tipo 2 são apresentados como **AC Type 2**, incluindo carregadores importados anteriormente. Confirme a identificação do conector e registe o resultado e a fotografia na pergunta correspondente. Se escolher **N/A**, indique o motivo nas observações.

| Campo e verificação | Como preencher | Requisitos |
| --- | --- | --- |
| **CARG.01.xx** — Teste de carga de cada conector apresentado no relatório | Selecione OK, NOK ou N/A. | N/A: observação obrigatória. Fotografia obrigatória quando aplicável. |
| **CARG.04** — Verificar funcionamento do leitor RFID | Selecione OK, NOK ou N/A. | N/A: observação opcional. Fotografia opcional. |
| **CARG.05** — Verifique o correto funcionamento dos meters no carregador - tire fotografias | Selecione OK, NOK ou N/A. | N/A: observação obrigatória. Fotografia obrigatória quando aplicável. |
| **CARG.06** — Verifique o funcionamento das luzes e ventiladores | Selecione OK, NOK ou N/A. | N/A: observação opcional. Fotografia opcional. |
| **CARG.07** — Observações gerais dos ensaios de carregamento | Observações, fotografias e vídeos opcionais. Pode ficar vazio. | Sem OK/NOK/N/A. |


### FIN - Informações Gerais e Final

| Campo e verificação | Como preencher | Requisitos |
| --- | --- | --- |
| **FIN.01** — Fotografia da Estação de Carregamento após intervenção | Selecione OK, NOK ou N/A. | N/A: observação opcional. Fotografia obrigatória quando aplicável. |
| **FIN.02** — Nome e assinatura do técnico | Preencha o nome do técnico. Use o controlo próprio para assinar (secção 7). | Nome e assinatura obrigatórios para fechar. Observação opcional. Assine no controlo próprio, sem anexos gerais de fotografia ou vídeo. |
| **FIN.03** — Observações gerais finais | Observações, fotografias e vídeos opcionais. Pode ficar vazio. | Sem OK/NOK/N/A. |

## 16 Referências para o critério elétrico

**RTIEBT, Portaria n.º 949-A/2006, texto consolidado, secção 413.1.4.2:** [Texto consolidado](https://diariodarepublica.pt/dr/legislacao-consolidada/portaria/2006-70055500)

**Portaria n.º 252/2015, que acrescenta a secção 722 sobre alimentação de veículos elétricos:** [Diploma](https://diariodarepublica.pt/dr/detalhe/portaria/252-2015-70055493)

**Guia Técnico das Instalações Elétricas para Alimentação de Veículos Elétricos, CTE64, edição 3 (2023), secção 5.3.2, página 28:** [Guia técnico em PDF](https://www.iep.pt/wp-content/uploads/2023/10/Guia-VE-Ed.3-2023.pdf)
