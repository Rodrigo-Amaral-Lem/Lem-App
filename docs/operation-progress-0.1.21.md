# Estado da operação — 0.1.21

O ecrã **Estado da operação** mostra a tarefa atual e uma barra de progresso, além da mensagem mais recente e dos reports por sincronizar.

- Etapas identificadas: início de sessão, contratos, catálogo de carregadores, dados de reports, envio de respostas, envio/descarga/remoção de fotografias, PDF/ZIP e importação de Excel.
- Fotografias: identifica o carregador/report e o ponto de verificação; mostra os bytes enviados/recebidos e o total quando o protocolo o indica.
- Descarga em lote: barra do número de fotografias concluídas e indicadores individuais para até duas transferências em simultâneo.
- Sincronização: barra do número de reports processados no lote e indicação da fotografia atual; as alterações mais recentes podem continuar pendentes para o lote seguinte.
- Espera por processamento/confirmação do servidor e respostas de tamanho desconhecido: barra indeterminada, sem percentagem inventada. O envio completo dos bytes não é apresentado como confirmação de gravação no servidor.
- Respostas guardadas localmente: indicação separada quando a gravação está em curso ou precisa de nova tentativa.

As atualizações de bytes usam uma notificação própria, limitada a intervalos de aproximadamente 100 ms. Não notificam os modelos de perguntas nem reescrevem a cache. As linhas de transferências mantêm-se estáveis enquanto os valores mudam, e só são criadas quando o ecrã está aberto.

Os indicadores descrevem a comunicação observada pela app; não recebem uma percentagem interna do processamento do servidor. Durante a espera, mostram essa limitação explicitamente.

Validação: testes QML em dimensões Android e desktop para barra determinada/indeterminada e texto da tarefa; inspeção visual com duas transferências. Integração HTTP local com verificações das etapas, contexto das fotografias, limites do progresso, conclusão das transferências e regressões existentes de sincronização, falhas, cache e edição não bloqueante. Não foi executada uma compilação Android nesta revisão.

Atualizar apenas o cliente. Servidor e deployment mantêm-se inalterados.
